// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NET_CHROME_NETWORK_DELEGATE_H_
#define CHROME_BROWSER_NET_CHROME_NETWORK_DELEGATE_H_
#pragma once

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/time.h"
#include "net/base/network_delegate.h"

class CookieSettings;
class ExtensionEventRouterForwarder;
class ExtensionInfoMap;
class PrefService;
template<class T> class PrefMember;

typedef PrefMember<bool> BooleanPrefMember;

namespace policy {
class URLBlacklistManager;
}

namespace net {
class DnsRRResolver;
}

// ChromeNetworkDelegate is the central point from within the chrome code to
// add hooks into the network stack.
class ChromeNetworkDelegate : public net::NetworkDelegate {
 public:
  // If |profile| is NULL, events will be broadcasted to all profiles,
  // otherwise they will only be sent to the specified profile.
  // |enable_referrers| should be initialized on the UI thread (see below)
  // beforehand. This object's owner is responsible for cleaning it up at
  // shutdown. If |cookie_settings| is NULL, all cookies are enabled,
  // otherwise, the settings are enforced on all observed network requests.
  ChromeNetworkDelegate(
      ExtensionEventRouterForwarder* event_router,
      ExtensionInfoMap* extension_info_map,
      const policy::URLBlacklistManager* url_blacklist_manager,
      void* profile,
      CookieSettings* cookie_settings,
      BooleanPrefMember* enable_referrers);
  virtual ~ChromeNetworkDelegate();

  // Binds |enable_referrers| to |pref_service| and moves it to the IO thread.
  // This method should be called on the UI thread.
  static void InitializeReferrersEnabled(BooleanPrefMember* enable_referrers,
                                         PrefService* pref_service);

  static void EnableComodoDNSExperiment();

 private:
  // NetworkDelegate implementation.
  virtual int OnBeforeURLRequest(net::URLRequest* request,
                                 const net::CompletionCallback& callback,
                                 GURL* new_url) OVERRIDE;
  virtual int OnBeforeSendHeaders(net::URLRequest* request,
                                  const net::CompletionCallback& callback,
                                  net::HttpRequestHeaders* headers) OVERRIDE;
  virtual void OnSendHeaders(net::URLRequest* request,
                             const net::HttpRequestHeaders& headers) OVERRIDE;
  virtual int OnHeadersReceived(
      net::URLRequest* request,
      const net::CompletionCallback& callback,
      net::HttpResponseHeaders* original_response_headers,
      scoped_refptr<net::HttpResponseHeaders>* override_response_headers)
      OVERRIDE;
  virtual void OnBeforeRedirect(net::URLRequest* request,
                                const GURL& new_location) OVERRIDE;
  virtual void OnResponseStarted(net::URLRequest* request) OVERRIDE;
  virtual void OnRawBytesRead(const net::URLRequest& request,
                              int bytes_read) OVERRIDE;
  virtual void OnCompleted(net::URLRequest* request, bool started) OVERRIDE;
  virtual void OnURLRequestDestroyed(net::URLRequest* request) OVERRIDE;
  virtual void OnPACScriptError(int line_number,
                                const string16& error) OVERRIDE;
  virtual net::NetworkDelegate::AuthRequiredResponse OnAuthRequired(
      net::URLRequest* request,
      const net::AuthChallengeInfo& auth_info,
      const AuthCallback& callback,
      net::AuthCredentials* credentials) OVERRIDE;
  virtual bool CanGetCookies(const net::URLRequest* request,
                             const net::CookieList& cookie_list) OVERRIDE;
  virtual bool CanSetCookie(const net::URLRequest* request,
                            const std::string& cookie_line,
                            net::CookieOptions* options) OVERRIDE;

  scoped_refptr<ExtensionEventRouterForwarder> event_router_;
  void* profile_;
  scoped_refptr<CookieSettings> cookie_settings_;

  scoped_refptr<ExtensionInfoMap> extension_info_map_;

  // Weak, owned by our owner.
  BooleanPrefMember* enable_referrers_;

  // Weak, owned by our owner.
  const policy::URLBlacklistManager* url_blacklist_manager_;

  scoped_ptr<net::DnsRRResolver> dnsrr_resolver_;
  base::TimeTicks last_comodo_resolution_time_;

  DISALLOW_COPY_AND_ASSIGN(ChromeNetworkDelegate);
};

#endif  // CHROME_BROWSER_NET_CHROME_NETWORK_DELEGATE_H_
