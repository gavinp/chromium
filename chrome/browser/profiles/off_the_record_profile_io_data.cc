// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/profiles/off_the_record_profile_io_data.h"

#include "base/command_line.h"
#include "base/logging.h"
#include "base/stl_util-inl.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/io_thread.h"
#include "chrome/browser/net/chrome_cookie_policy.h"
#include "chrome/browser/net/chrome_dns_cert_provenance_checker_factory.h"
#include "chrome/browser/net/chrome_net_log.h"
#include "chrome/browser/net/chrome_network_delegate.h"
#include "chrome/browser/net/chrome_url_request_context.h"
#include "chrome/browser/net/proxy_service_factory.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/url_constants.h"
#include "content/browser/browser_thread.h"
#include "net/ftp/ftp_network_layer.h"
#include "net/http/http_cache.h"

OffTheRecordProfileIOData::Handle::Handle(Profile* profile)
    : io_data_(new OffTheRecordProfileIOData),
      profile_(profile),
      initialized_(false) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(profile);
  DCHECK(!io_data_->lazy_params_.get());
  LazyParams* lazy_params = new LazyParams;
  lazy_params->io_thread = g_browser_process->io_thread();
  io_data_->lazy_params_.reset(lazy_params);
}

OffTheRecordProfileIOData::Handle::~Handle() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (main_request_context_getter_)
    main_request_context_getter_->CleanupOnUIThread();
  if (extensions_request_context_getter_)
    extensions_request_context_getter_->CleanupOnUIThread();

  // Clean up all isolated app request contexts.
  for (ChromeURLRequestContextGetterMap::iterator iter =
           app_request_context_getter_map_.begin();
       iter != app_request_context_getter_map_.end();
       ++iter) {
    iter->second->CleanupOnUIThread();
  }
}

scoped_refptr<ChromeURLRequestContextGetter>
OffTheRecordProfileIOData::Handle::GetMainRequestContextGetter() const {
  // TODO(oshima): Re-enable when ChromeOS only accesses the profile on the UI
  // thread.
#if !defined(OS_CHROMEOS)
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
#endif  // defined(OS_CHROMEOS)
  LazyInitialize();
  if (!main_request_context_getter_) {
    main_request_context_getter_ =
        ChromeURLRequestContextGetter::CreateOffTheRecord(profile_, io_data_);
  }
  return main_request_context_getter_;
}

scoped_refptr<ChromeURLRequestContextGetter>
OffTheRecordProfileIOData::Handle::GetExtensionsRequestContextGetter() const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  LazyInitialize();
  if (!extensions_request_context_getter_) {
    extensions_request_context_getter_ =
        ChromeURLRequestContextGetter::CreateOffTheRecordForExtensions(
            profile_, io_data_);
  }
  return extensions_request_context_getter_;
}

scoped_refptr<ChromeURLRequestContextGetter>
OffTheRecordProfileIOData::Handle::GetIsolatedAppRequestContextGetter(
    const std::string& app_id) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!app_id.empty());
  LazyInitialize();

  // Keep a map of request context getters, one per requested app ID.
  ChromeURLRequestContextGetterMap::iterator iter =
      app_request_context_getter_map_.find(app_id);
  if (iter != app_request_context_getter_map_.end())
    return iter->second;

  ChromeURLRequestContextGetter* context =
      ChromeURLRequestContextGetter::CreateOffTheRecordForIsolatedApp(
          profile_, io_data_, app_id);
  app_request_context_getter_map_[app_id] = context;

  return context;
}

void OffTheRecordProfileIOData::Handle::LazyInitialize() const {
  if (!initialized_) {
    InitializeProfileParams(profile_, &io_data_->lazy_params_->profile_params);
    initialized_ = true;
  }
}

OffTheRecordProfileIOData::LazyParams::LazyParams() : io_thread(NULL) {}
OffTheRecordProfileIOData::LazyParams::~LazyParams() {}

OffTheRecordProfileIOData::OffTheRecordProfileIOData()
    : ProfileIOData(true),
      initialized_(false) {}
OffTheRecordProfileIOData::~OffTheRecordProfileIOData() {
  STLDeleteValues(&app_http_factory_map_);
}

void OffTheRecordProfileIOData::LazyInitializeInternal() const {
  main_request_context_ = new RequestContext;
  extensions_request_context_ = new RequestContext;

  IOThread* const io_thread = lazy_params_->io_thread;
  IOThread::Globals* const io_thread_globals = io_thread->globals();
  const ProfileParams& profile_params = lazy_params_->profile_params;
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();

  ApplyProfileParamsToContext(profile_params, main_request_context_);
  ApplyProfileParamsToContext(profile_params, extensions_request_context_);
  profile_params.appcache_service->set_request_context(main_request_context_);

  cookie_policy_.reset(
      new ChromeCookiePolicy(profile_params.host_content_settings_map));
  main_request_context_->set_cookie_policy(cookie_policy_.get());
  extensions_request_context_->set_cookie_policy(cookie_policy_.get());

  main_request_context_->set_net_log(lazy_params_->io_thread->net_log());
  extensions_request_context_->set_net_log(lazy_params_->io_thread->net_log());

  network_delegate_.reset(new ChromeNetworkDelegate(
      io_thread_globals->extension_event_router_forwarder.get(),
      profile_params.profile_id,
      profile_params.protocol_handler_registry));
  main_request_context_->set_network_delegate(network_delegate_.get());

  main_request_context_->set_host_resolver(
      io_thread_globals->host_resolver.get());
  main_request_context_->set_cert_verifier(
      io_thread_globals->cert_verifier.get());
  main_request_context_->set_dnsrr_resolver(
      io_thread_globals->dnsrr_resolver.get());
  main_request_context_->set_http_auth_handler_factory(
      io_thread_globals->http_auth_handler_factory.get());

  dns_cert_checker_.reset(
      CreateDnsCertProvenanceChecker(io_thread_globals->dnsrr_resolver.get(),
                                     main_request_context_));
  main_request_context_->set_dns_cert_checker(dns_cert_checker_.get());

  main_request_context_->set_proxy_service(
      ProxyServiceFactory::CreateProxyService(
          io_thread->net_log(),
          io_thread_globals->proxy_script_fetcher_context.get(),
          lazy_params_->profile_params.proxy_config_service.release(),
          command_line));

  main_request_context_->set_cookie_store(
      new net::CookieMonster(NULL, profile_params.cookie_monster_delegate));
  // All we care about for extensions is the cookie store. For incognito, we
  // use a non-persistent cookie store.

  net::CookieMonster* extensions_cookie_store =
      new net::CookieMonster(NULL, NULL);
  // Enable cookies for devtools and extension URLs.
  const char* schemes[] = {chrome::kChromeDevToolsScheme,
                           chrome::kExtensionScheme};
  extensions_cookie_store->SetCookieableSchemes(schemes, 2);

  extensions_request_context_->set_cookie_store(
      new net::CookieMonster(NULL, NULL));

  net::HttpCache::BackendFactory* main_backend =
      net::HttpCache::DefaultBackend::InMemory(0);
  net::HttpCache* cache =
      new net::HttpCache(main_request_context_->host_resolver(),
                         main_request_context_->cert_verifier(),
                         main_request_context_->dnsrr_resolver(),
                         main_request_context_->dns_cert_checker(),
                         main_request_context_->proxy_service(),
                         main_request_context_->ssl_config_service(),
                         main_request_context_->http_auth_handler_factory(),
                         main_request_context_->network_delegate(),
                         main_request_context_->net_log(),
                         main_backend);

  main_http_factory_.reset(cache);
  main_request_context_->set_http_transaction_factory(cache);
  main_request_context_->set_ftp_transaction_factory(
      new net::FtpNetworkLayer(main_request_context_->host_resolver()));
}

scoped_refptr<ProfileIOData::RequestContext>
OffTheRecordProfileIOData::InitializeAppRequestContext(
    scoped_refptr<ChromeURLRequestContext> main_context,
    const std::string& app_id) const {
  scoped_refptr<ProfileIOData::RequestContext> context = new RequestContext;

  // Copy most state from the main context.
  context->CopyFrom(main_context);

  // Use a separate in-memory cookie store for the app.
  // TODO(creis): We should have a cookie delegate for notifying the cookie
  // extensions API, but we need to update it to understand isolated apps first.
  context->set_cookie_store(
      new net::CookieMonster(NULL, NULL));

  // Use a separate in-memory cache for the app.
  net::HttpCache::BackendFactory* app_backend =
      net::HttpCache::DefaultBackend::InMemory(0);
  net::HttpNetworkSession* main_network_session =
      main_http_factory_->GetSession();
  net::HttpCache* app_http_cache =
      new net::HttpCache(main_network_session, app_backend);

  // Keep track of app_http_cache to delete it when we go away.
  DCHECK(!app_http_factory_map_[app_id]);
  app_http_factory_map_[app_id] = app_http_cache;
  context->set_http_transaction_factory(app_http_cache);

  return context;
}

scoped_refptr<ChromeURLRequestContext>
OffTheRecordProfileIOData::AcquireMainRequestContext() const {
  DCHECK(main_request_context_);
  scoped_refptr<ChromeURLRequestContext> context = main_request_context_;
  main_request_context_->set_profile_io_data(this);
  main_request_context_ = NULL;
  return context;
}

scoped_refptr<ChromeURLRequestContext>
OffTheRecordProfileIOData::AcquireMediaRequestContext() const {
  NOTREACHED();
  return NULL;
}

scoped_refptr<ChromeURLRequestContext>
OffTheRecordProfileIOData::AcquireExtensionsRequestContext() const {
  DCHECK(extensions_request_context_);
  scoped_refptr<ChromeURLRequestContext> context = extensions_request_context_;
  extensions_request_context_->set_profile_io_data(this);
  extensions_request_context_ = NULL;
  return context;
}

scoped_refptr<ChromeURLRequestContext>
OffTheRecordProfileIOData::AcquireIsolatedAppRequestContext(
    scoped_refptr<ChromeURLRequestContext> main_context,
    const std::string& app_id) const {
  // We create per-app contexts on demand, unlike the others above.
  scoped_refptr<RequestContext> app_request_context =
      InitializeAppRequestContext(main_context, app_id);
  DCHECK(app_request_context);
  app_request_context->set_profile_io_data(this);
  return app_request_context;
}
