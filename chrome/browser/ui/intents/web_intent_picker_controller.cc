// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/intents/web_intent_picker_controller.h"

#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "chrome/browser/extensions/webstore_installer.h"
#include "chrome/browser/favicon/favicon_service.h"
#include "chrome/browser/intents/default_web_intent_service.h"
#include "chrome/browser/intents/web_intents_registry_factory.h"
#include "chrome/browser/intents/cws_intents_registry_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "chrome/browser/tabs/tab_strip_model.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/intents/web_intent_picker.h"
#include "chrome/browser/ui/intents/web_intent_picker_model.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/browser/webdata/web_data_service.h"
#include "chrome/common/chrome_notification_types.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_intents_dispatcher.h"
#include "content/public/common/url_fetcher.h"
#include "content/public/common/url_fetcher_delegate.h"
#include "net/base/load_flags.h"
#include "skia/ext/image_operations.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/favicon_size.h"
#include "ui/gfx/image/image.h"
#include "webkit/glue/web_intent_service_data.h"

namespace {

// Gets the favicon service for the profile in |tab_contents|.
FaviconService* GetFaviconService(TabContentsWrapper* wrapper) {
  return wrapper->profile()->GetFaviconService(Profile::EXPLICIT_ACCESS);
}

// Gets the web intents registry for the profile in |tab_contents|.
WebIntentsRegistry* GetWebIntentsRegistry(TabContentsWrapper* wrapper) {
  return WebIntentsRegistryFactory::GetForProfile(wrapper->profile());
}

// Gets the Chrome web store intents registry for the profile in |tab_contents|.
CWSIntentsRegistry* GetCWSIntentsRegistry(TabContentsWrapper* wrapper) {
  return CWSIntentsRegistryFactory::GetForProfile(wrapper->profile());
}

WebIntentPickerModel::Disposition ConvertDisposition(
    webkit_glue::WebIntentServiceData::Disposition disposition) {
  switch (disposition) {
    case webkit_glue::WebIntentServiceData::DISPOSITION_INLINE:
      return WebIntentPickerModel::DISPOSITION_INLINE;
    case webkit_glue::WebIntentServiceData::DISPOSITION_WINDOW:
      return WebIntentPickerModel::DISPOSITION_WINDOW;
    default:
      NOTREACHED();
      return WebIntentPickerModel::DISPOSITION_WINDOW;
  }
}

// Self-deleting trampoline that forwards a WebIntentsRegistry response to a
// callback.
class WebIntentsRegistryTrampoline : public WebIntentsRegistry::Consumer {
 public:
  typedef std::vector<webkit_glue::WebIntentServiceData> IntentServices;
  typedef base::Callback<void(const IntentServices&)> ForwardingCallback;

  explicit WebIntentsRegistryTrampoline(const ForwardingCallback& callback);
  ~WebIntentsRegistryTrampoline();

  // WebIntentsRegistry::Consumer implementation.
  virtual void OnIntentsQueryDone(
      WebIntentsRegistry::QueryID,
      const std::vector<webkit_glue::WebIntentServiceData>& services) OVERRIDE;
  virtual void OnIntentsDefaultsQueryDone(
      WebIntentsRegistry::QueryID,
      const DefaultWebIntentService& default_service) OVERRIDE {}

 private:
  // Forwarding callback from |OnIntentsQueryDone|.
  ForwardingCallback callback_;
};

WebIntentsRegistryTrampoline::WebIntentsRegistryTrampoline(
    const ForwardingCallback& callback)
    : callback_(callback) {
}

WebIntentsRegistryTrampoline::~WebIntentsRegistryTrampoline() {
}

void WebIntentsRegistryTrampoline::OnIntentsQueryDone(
    WebIntentsRegistry::QueryID,
    const std::vector<webkit_glue::WebIntentServiceData>& services) {
  DCHECK(!callback_.is_null());
  callback_.Run(services);
  delete this;
}

// Self-deleting trampoline that forwards A URLFetcher response to a callback.
class URLFetcherTrampoline : public content::URLFetcherDelegate {
 public:
  typedef base::Callback<void(const content::URLFetcher* source)>
      ForwardingCallback;

  explicit URLFetcherTrampoline(const ForwardingCallback& callback);
  ~URLFetcherTrampoline();

  // content::URLFetcherDelegate implementation.
  virtual void OnURLFetchComplete(const content::URLFetcher* source) OVERRIDE;

 private:
  // Fowarding callback from |OnURLFetchComplete|.
  ForwardingCallback callback_;
};

URLFetcherTrampoline::URLFetcherTrampoline(const ForwardingCallback& callback)
    : callback_(callback) {
}

URLFetcherTrampoline::~URLFetcherTrampoline() {
}

void URLFetcherTrampoline::OnURLFetchComplete(
    const content::URLFetcher* source) {
  DCHECK(!callback_.is_null());
  callback_.Run(source);
  delete source;
  delete this;
}

}  // namespace

WebIntentPickerController::WebIntentPickerController(
    TabContentsWrapper* wrapper)
    : wrapper_(wrapper),
      picker_(NULL),
      picker_model_(new WebIntentPickerModel()),
      pending_async_count_(0),
      picker_shown_(false),
      intents_dispatcher_(NULL),
      service_tab_(NULL),
      weak_ptr_factory_(this) {
  content::NavigationController* controller =
      &wrapper->web_contents()->GetController();
  registrar_.Add(this, content::NOTIFICATION_LOAD_START,
                 content::Source<content::NavigationController>(controller));
  registrar_.Add(this, content::NOTIFICATION_TAB_CLOSING,
                 content::Source<content::NavigationController>(controller));
}

WebIntentPickerController::~WebIntentPickerController() {
}

void WebIntentPickerController::SetIntentsDispatcher(
    content::WebIntentsDispatcher* intents_dispatcher) {
  intents_dispatcher_ = intents_dispatcher;
  intents_dispatcher_->RegisterReplyNotification(
      base::Bind(&WebIntentPickerController::OnSendReturnMessage,
                 weak_ptr_factory_.GetWeakPtr()));
}

void WebIntentPickerController::ShowDialog(Browser* browser,
                                           const string16& action,
                                           const string16& type) {
  // Only show a picker once.
  if (picker_shown_)
    return;

  // TODO(binji): Figure out what to do when intents are invoked from incognito
  // mode.
  if (wrapper_->profile()->IsOffTheRecord())
    return;

  picker_model_->Clear();
  picker_model_->set_action(action);
  picker_model_->set_mimetype(type);

  // If picker is non-NULL, it was set by a test.
  if (picker_ == NULL) {
    picker_ = WebIntentPicker::Create(browser, wrapper_, this,
                                      picker_model_.get());
  }

  picker_shown_ = true;
  pending_async_count_+= 2;
  GetWebIntentsRegistry(wrapper_)->GetIntentServices(
      action, type,
      // WebIntentsRegistryTrampoline is self-deleting.
      new WebIntentsRegistryTrampoline(
          base::Bind(&WebIntentPickerController::OnWebIntentServicesAvailable,
              weak_ptr_factory_.GetWeakPtr())));
  GetCWSIntentsRegistry(wrapper_)->GetIntentServices(
      action, type,
      base::Bind(&WebIntentPickerController::OnCWSIntentServicesAvailable,
                 weak_ptr_factory_.GetWeakPtr()));
}

void WebIntentPickerController::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK(type == content::NOTIFICATION_LOAD_START ||
         type == content::NOTIFICATION_TAB_CLOSING);
  ClosePicker();
}

void WebIntentPickerController::OnServiceChosen(const GURL& url,
                                                Disposition disposition) {
  switch (disposition) {
    case WebIntentPickerModel::DISPOSITION_INLINE:
      // Set the model to inline disposition. It will notify the picker which
      // will respond (via OnInlineDispositionWebContentsCreated) with the
      // WebContents to dispatch the intent to.
      picker_model_->SetInlineDisposition(url);
      break;

    case WebIntentPickerModel::DISPOSITION_WINDOW: {
      // TODO(gbillock): This really only handles the 'window' disposition in a
      // quite prototype way. We need to flesh out what happens to the picker
      // during the lifetime of the service url context, and that may mean we
      // need to pass more information into the injector to find the picker
      // again and close it.
      int index = TabStripModel::kNoTab;
      Browser* browser = Browser::GetBrowserForController(
          &wrapper_->web_contents()->GetController(), &index);
      TabContentsWrapper* contents = Browser::TabContentsFactory(
          browser->profile(),
          tab_util::GetSiteInstanceForNewTab(
              NULL, browser->profile(), url),
          MSG_ROUTING_NONE, NULL, NULL);

      intents_dispatcher_->DispatchIntent(contents->web_contents());
      service_tab_ = contents->web_contents();

      // This call performs all the tab strip manipulation, notifications, etc.
      // Since we're passing in a target_contents, it assumes that we will
      // navigate the page ourselves, though.
      browser::NavigateParams params(browser,
                                     url,
                                     content::PAGE_TRANSITION_AUTO_BOOKMARK);
      params.target_contents = contents;
      params.disposition = NEW_FOREGROUND_TAB;
      params.profile = wrapper_->profile();
      browser::Navigate(&params);

      service_tab_->GetController().LoadURL(
          url, content::Referrer(),
          content::PAGE_TRANSITION_AUTO_BOOKMARK, std::string());

      ClosePicker();
      break;
    }

    default:
      NOTREACHED();
      break;
  }
}

void WebIntentPickerController::OnInlineDispositionWebContentsCreated(
    content::WebContents* web_contents) {
  if (web_contents)
    intents_dispatcher_->DispatchIntent(web_contents);
}

void WebIntentPickerController::OnExtensionInstallRequested(
    const std::string& id) {
  scoped_refptr<WebstoreInstaller> installer = new WebstoreInstaller(
      wrapper_->profile(), this, &wrapper_->web_contents()->GetController(), id,
      WebstoreInstaller::FLAG_INLINE_INSTALL);

  pending_async_count_++;
  installer->Start();
}

void WebIntentPickerController::OnCancelled() {
  if (!intents_dispatcher_)
    return;

  if (service_tab_) {
    intents_dispatcher_->SendReplyMessage(
        webkit_glue::WEB_INTENT_SERVICE_TAB_CLOSED, string16());
  } else {
    intents_dispatcher_->SendReplyMessage(
        webkit_glue::WEB_INTENT_PICKER_CANCELLED, string16());
  }

  ClosePicker();
}

void WebIntentPickerController::OnClosing() {
  picker_shown_ = false;
  picker_ = NULL;
}

void WebIntentPickerController::OnExtensionInstallSuccess(
    const std::string& id) {
  picker_->OnExtensionInstallSuccess(id);
  pending_async_count_++;
  GetWebIntentsRegistry(wrapper_)->GetIntentServicesForExtensionFilter(
      picker_model_->action(),
      picker_model_->mimetype(),
      id,
      new WebIntentsRegistryTrampoline(
          base::Bind(
              &WebIntentPickerController::OnExtensionInstallServiceAvailable,
              weak_ptr_factory_.GetWeakPtr())));
  AsyncOperationFinished();
}

void WebIntentPickerController::OnExtensionInstallFailure(
    const std::string& id,
    const std::string& error) {
  picker_->OnExtensionInstallFailure(id);
  AsyncOperationFinished();
}

void WebIntentPickerController::OnSendReturnMessage(
    webkit_glue::WebIntentReplyType reply_type) {
  ClosePicker();

  if (service_tab_ &&
      reply_type != webkit_glue::WEB_INTENT_SERVICE_TAB_CLOSED) {
    int index = TabStripModel::kNoTab;
    Browser* browser = Browser::GetBrowserForController(
        &service_tab_->GetController(), &index);
    if (browser) {
      browser->tabstrip_model()->CloseTabContentsAt(
          index, TabStripModel::CLOSE_CREATE_HISTORICAL_TAB);

      // Activate source tab.
      Browser* source_browser =
          BrowserList::FindBrowserWithWebContents(wrapper_->web_contents());
      if (source_browser) {
        int source_index =
            source_browser->tabstrip_model()->GetIndexOfTabContents(wrapper_);
        source_browser->ActivateTabAt(source_index, false);
      }
    }
    service_tab_ = NULL;
  }

  intents_dispatcher_ = NULL;
}

void WebIntentPickerController::OnWebIntentServicesAvailable(
    const std::vector<webkit_glue::WebIntentServiceData>& services) {
  FaviconService* favicon_service = GetFaviconService(wrapper_);
  for (size_t i = 0; i < services.size(); ++i) {
    picker_model_->AddInstalledService(
        services[i].title,
        services[i].service_url,
        ConvertDisposition(services[i].disposition));

    pending_async_count_++;
    FaviconService::Handle handle = favicon_service->GetFaviconForURL(
        services[i].service_url,
        history::FAVICON,
        &favicon_consumer_,
        base::Bind(
            &WebIntentPickerController::OnFaviconDataAvailable,
            weak_ptr_factory_.GetWeakPtr()));
    favicon_consumer_.SetClientData(favicon_service, handle, i);
  }

  AsyncOperationFinished();
}

void WebIntentPickerController::OnFaviconDataAvailable(
    FaviconService::Handle handle, history::FaviconData favicon_data) {
  size_t index = favicon_consumer_.GetClientDataForCurrentRequest();
  if (favicon_data.is_valid()) {
    SkBitmap icon_bitmap;

    if (gfx::PNGCodec::Decode(favicon_data.image_data->front(),
                              favicon_data.image_data->size(),
                              &icon_bitmap)) {
      gfx::Image icon_image(new SkBitmap(icon_bitmap));
      picker_model_->UpdateFaviconAt(index, icon_image);
      return;
    }
  }

  AsyncOperationFinished();
}

void WebIntentPickerController::OnCWSIntentServicesAvailable(
    const CWSIntentsRegistry::IntentExtensionList& extensions) {
  for (size_t i = 0; i < extensions.size(); ++i) {
    const CWSIntentsRegistry::IntentExtensionInfo& info = extensions[i];
    picker_model_->AddSuggestedExtension(
        info.name,
        info.id,
        info.average_rating);

    pending_async_count_++;
    content::URLFetcher* icon_url_fetcher = content::URLFetcher::Create(
        0,
        info.icon_url,
        content::URLFetcher::GET,
        new URLFetcherTrampoline(
            base::Bind(
                &WebIntentPickerController::OnExtensionIconURLFetchComplete,
                weak_ptr_factory_.GetWeakPtr(), info.id)));

    icon_url_fetcher->SetLoadFlags(
        net::LOAD_DO_NOT_SEND_COOKIES | net::LOAD_DO_NOT_SAVE_COOKIES);
    icon_url_fetcher->SetRequestContext(
        wrapper_->profile()->GetRequestContext());
    icon_url_fetcher->Start();
  }

  AsyncOperationFinished();
}

void WebIntentPickerController::OnExtensionIconURLFetchComplete(
    const string16& extension_id, const content::URLFetcher* source) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  if (source->GetResponseCode() != 200) {
    AsyncOperationFinished();
    return;
  }

  scoped_ptr<std::string> response(new std::string);
  if (!source->GetResponseAsString(response.get())) {
    AsyncOperationFinished();
    return;
  }

  // I'd like to have the worker thread post a task directly to the UI thread
  // to call OnExtensionIcon[Un]Available, but this doesn't work: To do so
  // would require DecodeExtensionIconAndResize to be a member function (so it
  // has access to |this|) but a weak pointer cannot be dereferenced on a
  // thread other than the thread where the WeakPtrFactory was created. Since
  // the stored |this| pointer is weak, DecodeExtensionIconAndResize asserts
  // before it even starts.
  //
  // Instead, I package up the callbacks that I want the worker thread to call,
  // and make DecodeExtensionIconAndResize static. The stored weak |this|
  // pointers are not dereferenced until invocation (on the UI thread).
  ExtensionIconAvailableCallback available_callback =
      base::Bind(
          &WebIntentPickerController::OnExtensionIconAvailable,
          weak_ptr_factory_.GetWeakPtr(),
          extension_id);
  base::Closure unavailable_callback =
      base::Bind(
          &WebIntentPickerController::OnExtensionIconUnavailable,
          weak_ptr_factory_.GetWeakPtr(),
          extension_id);

  // Decode PNG and resize on worker thread.
  content::BrowserThread::PostBlockingPoolTask(
      FROM_HERE,
      base::Bind(&DecodeExtensionIconAndResize,
                 base::Passed(&response),
                 available_callback,
                 unavailable_callback));
}

// static
void WebIntentPickerController::DecodeExtensionIconAndResize(
    scoped_ptr<std::string> icon_response,
    const ExtensionIconAvailableCallback& callback,
    const base::Closure& unavailable_callback) {
  SkBitmap icon_bitmap;
  if (gfx::PNGCodec::Decode(
        reinterpret_cast<const unsigned char*>(icon_response->data()),
        icon_response->length(),
        &icon_bitmap)) {
    SkBitmap resized_icon = skia::ImageOperations::Resize(
        icon_bitmap,
        skia::ImageOperations::RESIZE_BEST,
        gfx::kFaviconSize, gfx::kFaviconSize);
    gfx::Image icon_image(new SkBitmap(resized_icon));

    content::BrowserThread::PostTask(
        content::BrowserThread::UI,
        FROM_HERE,
        base::Bind(callback, icon_image));
  } else {
    content::BrowserThread::PostTask(
        content::BrowserThread::UI,
        FROM_HERE,
        unavailable_callback);
  }
}

void WebIntentPickerController::OnExtensionIconAvailable(
    const string16& extension_id,
    const gfx::Image& icon_image) {
  picker_model_->SetSuggestedExtensionIconWithId(extension_id, icon_image);
  AsyncOperationFinished();
}

void WebIntentPickerController::OnExtensionIconUnavailable(
    const string16& extension_id) {
  AsyncOperationFinished();
}

void WebIntentPickerController::OnExtensionInstallServiceAvailable(
    const std::vector<webkit_glue::WebIntentServiceData>& services) {
  DCHECK(services.size() > 0);

  // TODO(binji): We're going to need to disambiguate if there are multiple
  // services. For now, just choose the first.
  const webkit_glue::WebIntentServiceData& service_data = services[0];
  OnServiceChosen(
      service_data.service_url,
      ConvertDisposition(service_data.disposition));
  AsyncOperationFinished();
}

void WebIntentPickerController::AsyncOperationFinished() {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  if (--pending_async_count_ == 0) {
    if (picker_)
      picker_->OnPendingAsyncCompleted();
  }
}

void WebIntentPickerController::ClosePicker() {
  if (picker_)
    picker_->Close();
}
