// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/base/testing_profile.h"

#include "build/build_config.h"

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/message_loop_proxy.h"
#include "base/path_service.h"
#include "base/string_number_conversions.h"
#include "chrome/browser/autocomplete/autocomplete_classifier.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/content_settings/host_content_settings_map.h"
#include "chrome/browser/custom_handlers/protocol_handler_registry.h"
#include "chrome/browser/extensions/extension_pref_value_map.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_special_storage_policy.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/extension_system_factory.h"
#include "chrome/browser/extensions/test_extension_system.h"
#include "chrome/browser/favicon/favicon_service.h"
#include "chrome/browser/geolocation/chrome_geolocation_permission_context.h"
#include "chrome/browser/history/history.h"
#include "chrome/browser/history/history_backend.h"
#include "chrome/browser/history/top_sites.h"
#include "chrome/browser/net/proxy_service_factory.h"
#include "chrome/browser/notifications/desktop_notification_service.h"
#include "chrome/browser/notifications/desktop_notification_service_factory.h"
#include "chrome/browser/prefs/browser_prefs.h"
#include "chrome/browser/prefs/testing_pref_store.h"
#include "chrome/browser/prerender/prerender_manager.h"
#include "chrome/browser/profiles/profile_dependency_manager.h"
#include "chrome/browser/protector/protector_service_factory.h"
#include "chrome/browser/search_engines/template_url_fetcher_factory.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/speech/chrome_speech_recognition_preferences.h"
#include "chrome/browser/sync/profile_sync_service_mock.h"
#include "chrome/browser/ui/webui/chrome_url_data_manager.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/bookmark_load_observer.h"
#include "chrome/test/base/testing_pref_service.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "content/test/mock_resource_context.h"
#include "net/cookies/cookie_monster.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "webkit/database/database_tracker.h"
#include "webkit/fileapi/file_system_context.h"
#include "webkit/fileapi/file_system_options.h"
#include "webkit/quota/mock_quota_manager.h"
#include "webkit/quota/quota_manager.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/proxy_config_service_impl.h"
#endif  // defined(OS_CHROMEOS)

using base::Time;
using content::BrowserThread;
using content::DownloadManager;
using testing::NiceMock;
using testing::Return;

namespace {

// Task used to make sure history has finished processing a request. Intended
// for use with BlockUntilHistoryProcessesPendingRequests.

class QuittingHistoryDBTask : public HistoryDBTask {
 public:
  QuittingHistoryDBTask() {}

  virtual bool RunOnDBThread(history::HistoryBackend* backend,
                             history::HistoryDatabase* db) {
    return true;
  }

  virtual void DoneRunOnMainThread() {
    MessageLoop::current()->Quit();
  }

 private:
  ~QuittingHistoryDBTask() {}

  DISALLOW_COPY_AND_ASSIGN(QuittingHistoryDBTask);
};

class TestExtensionURLRequestContext : public net::URLRequestContext {
 public:
  TestExtensionURLRequestContext() {
    net::CookieMonster* cookie_monster = new net::CookieMonster(NULL, NULL);
    const char* schemes[] = {chrome::kExtensionScheme};
    cookie_monster->SetCookieableSchemes(schemes, 1);
    set_cookie_store(cookie_monster);
  }
};

class TestExtensionURLRequestContextGetter
    : public net::URLRequestContextGetter {
 public:
  virtual net::URLRequestContext* GetURLRequestContext() {
    if (!context_)
      context_ = new TestExtensionURLRequestContext();
    return context_.get();
  }
  virtual scoped_refptr<base::MessageLoopProxy> GetIOMessageLoopProxy() const {
    return BrowserThread::GetMessageLoopProxyForThread(BrowserThread::IO);
  }

 private:
  scoped_refptr<net::URLRequestContext> context_;
};

ProfileKeyedService* CreateTestDesktopNotificationService(Profile* profile) {
  return new DesktopNotificationService(profile, NULL);
}

}  // namespace

TestingProfile::TestingProfile()
    : start_time_(Time::Now()),
      testing_prefs_(NULL),
      incognito_(false),
      last_session_exited_cleanly_(true),
      profile_dependency_manager_(ProfileDependencyManager::GetInstance()),
      delegate_(NULL) {
  if (!temp_dir_.CreateUniqueTempDir()) {
    LOG(ERROR) << "Failed to create unique temporary directory.";

    // Fallback logic in case we fail to create unique temporary directory.
    FilePath system_tmp_dir;
    bool success = PathService::Get(base::DIR_TEMP, &system_tmp_dir);

    // We're severly screwed if we can't get the system temporary
    // directory. Die now to avoid writing to the filesystem root
    // or other bad places.
    CHECK(success);

    FilePath fallback_dir(system_tmp_dir.AppendASCII("TestingProfilePath"));
    file_util::Delete(fallback_dir, true);
    file_util::CreateDirectory(fallback_dir);
    if (!temp_dir_.Set(fallback_dir)) {
      // That shouldn't happen, but if it does, try to recover.
      LOG(ERROR) << "Failed to use a fallback temporary directory.";

      // We're screwed if this fails, see CHECK above.
      CHECK(temp_dir_.Set(system_tmp_dir));
    }
  }

  profile_path_ = temp_dir_.path();

  Init();
  FinishInit();
}

TestingProfile::TestingProfile(const FilePath& path)
    : start_time_(Time::Now()),
      testing_prefs_(NULL),
      incognito_(false),
      last_session_exited_cleanly_(true),
      profile_path_(path),
      profile_dependency_manager_(ProfileDependencyManager::GetInstance()),
      delegate_(NULL) {
  Init();
  FinishInit();
}

TestingProfile::TestingProfile(const FilePath& path,
                               Delegate* delegate)
    : start_time_(Time::Now()),
      testing_prefs_(NULL),
      incognito_(false),
      last_session_exited_cleanly_(true),
      profile_path_(path),
      profile_dependency_manager_(ProfileDependencyManager::GetInstance()),
      delegate_(delegate) {
  Init();
  if (delegate_) {
    MessageLoop::current()->PostTask(FROM_HERE,
                                     base::Bind(&TestingProfile::FinishInit,
                                                base::Unretained(this)));
  } else {
    FinishInit();
  }
}

void TestingProfile::Init() {
  ExtensionSystemFactory::GetInstance()->SetTestingFactory(
      this, TestExtensionSystem::Build);

  profile_dependency_manager_->CreateProfileServices(this, true);

#if defined(ENABLE_NOTIFICATIONS)
  // Install profile keyed service factory hooks for dummy/test services
  DesktopNotificationServiceFactory::GetInstance()->SetTestingFactory(
      this, CreateTestDesktopNotificationService);
#endif
}

void TestingProfile::FinishInit() {
  DCHECK(content::NotificationService::current());
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_PROFILE_CREATED,
      content::Source<Profile>(static_cast<Profile*>(this)),
      content::NotificationService::NoDetails());

  if (delegate_)
    delegate_->OnProfileCreated(this, true, false);
}

TestingProfile::~TestingProfile() {
  DCHECK(content::NotificationService::current());
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_PROFILE_DESTROYED,
      content::Source<Profile>(static_cast<Profile*>(this)),
      content::NotificationService::NoDetails());

  profile_dependency_manager_->DestroyProfileServices(this);

  if (host_content_settings_map_)
    host_content_settings_map_->ShutdownOnUIThread();

  DestroyTopSites();
  DestroyHistoryService();
  // FaviconService depends on HistoryServce so destroying it later.
  DestroyFaviconService();
  DestroyWebDataService();

  if (pref_proxy_config_tracker_.get())
    pref_proxy_config_tracker_->DetachFromPrefService();
}

void TestingProfile::CreateFaviconService() {
  favicon_service_.reset(new FaviconService(this));
}

void TestingProfile::CreateHistoryService(bool delete_file, bool no_db) {
  DestroyHistoryService();
  if (delete_file) {
    FilePath path = GetPath();
    path = path.Append(chrome::kHistoryFilename);
    file_util::Delete(path, false);
  }
  history_service_ = new HistoryService(this);
  history_service_->Init(GetPath(), bookmark_bar_model_.get(), no_db);
}

void TestingProfile::DestroyHistoryService() {
  if (!history_service_.get())
    return;

  history_service_->NotifyRenderProcessHostDestruction(0);
  history_service_->SetOnBackendDestroyTask(MessageLoop::QuitClosure());
  history_service_->Cleanup();
  history_service_ = NULL;

  // Wait for the backend class to terminate before deleting the files and
  // moving to the next test. Note: if this never terminates, somebody is
  // probably leaking a reference to the history backend, so it never calls
  // our destroy task.
  MessageLoop::current()->Run();

  // Make sure we don't have any event pending that could disrupt the next
  // test.
  MessageLoop::current()->PostTask(FROM_HERE, MessageLoop::QuitClosure());
  MessageLoop::current()->Run();
}

void TestingProfile::CreateTopSites() {
  DestroyTopSites();
  top_sites_ = new history::TopSites(this);
  FilePath file_name = GetPath().Append(chrome::kTopSitesFilename);
  top_sites_->Init(file_name);
}

void TestingProfile::DestroyTopSites() {
  if (top_sites_.get()) {
    top_sites_->Shutdown();
    top_sites_ = NULL;
    // TopSites::Shutdown schedules some tasks (from TopSitesBackend) that need
    // to be run to properly shutdown. Run all pending tasks now. This is
    // normally handled by browser_process shutdown.
    if (MessageLoop::current())
      MessageLoop::current()->RunAllPending();
  }
}

void TestingProfile::DestroyFaviconService() {
  favicon_service_.reset();
}

void TestingProfile::CreateBookmarkModel(bool delete_file) {
  // Nuke the model first, that way we're sure it's done writing to disk.
  bookmark_bar_model_.reset(NULL);

  if (delete_file) {
    FilePath path = GetPath();
    path = path.Append(chrome::kBookmarksFileName);
    file_util::Delete(path, false);
  }
  bookmark_bar_model_.reset(new BookmarkModel(this));
  if (history_service_.get()) {
    history_service_->history_backend_->bookmark_service_ =
        bookmark_bar_model_.get();
    history_service_->history_backend_->expirer_.bookmark_service_ =
        bookmark_bar_model_.get();
  }
  bookmark_bar_model_->Load();
}

void TestingProfile::CreateAutocompleteClassifier() {
  autocomplete_classifier_.reset(new AutocompleteClassifier(this));
}

void TestingProfile::CreateProtocolHandlerRegistry() {
  protocol_handler_registry_ = new ProtocolHandlerRegistry(this,
      new ProtocolHandlerRegistry::Delegate());
}

void TestingProfile::CreateWebDataService(bool delete_file) {
  if (web_data_service_.get())
    web_data_service_->Shutdown();

  if (delete_file) {
    FilePath path = GetPath();
    path = path.Append(chrome::kWebDataFilename);
    file_util::Delete(path, false);
  }

  web_data_service_ = new WebDataService;
  if (web_data_service_.get())
    web_data_service_->Init(GetPath());
}

void TestingProfile::BlockUntilBookmarkModelLoaded() {
  DCHECK(bookmark_bar_model_.get());
  if (bookmark_bar_model_->IsLoaded())
    return;
  BookmarkLoadObserver observer;
  bookmark_bar_model_->AddObserver(&observer);
  MessageLoop::current()->Run();
  bookmark_bar_model_->RemoveObserver(&observer);
  DCHECK(bookmark_bar_model_->IsLoaded());
}

// TODO(phajdan.jr): Doesn't this hang if Top Sites are already loaded?
void TestingProfile::BlockUntilTopSitesLoaded() {
  ui_test_utils::WindowedNotificationObserver top_sites_loaded_observer(
      chrome::NOTIFICATION_TOP_SITES_LOADED,
      content::NotificationService::AllSources());
  if (!GetHistoryService(Profile::EXPLICIT_ACCESS))
    GetTopSites()->HistoryLoaded();
  top_sites_loaded_observer.Wait();
}

static ProfileKeyedService* BuildTemplateURLService(Profile* profile) {
  return new TemplateURLService(profile);
}

void TestingProfile::CreateTemplateURLService() {
  TemplateURLServiceFactory::GetInstance()->SetTestingFactoryAndUse(
      this, BuildTemplateURLService);
}

void TestingProfile::BlockUntilTemplateURLServiceLoaded() {
  TemplateURLService* turl_model =
      TemplateURLServiceFactory::GetForProfile(this);
  if (turl_model->loaded())
    return;

  ui_test_utils::WindowedNotificationObserver turl_service_load_observer(
      chrome::NOTIFICATION_TEMPLATE_URL_SERVICE_LOADED,
      content::NotificationService::AllSources());
  turl_model->Load();
  turl_service_load_observer.Wait();
}

FilePath TestingProfile::GetPath() {
  return profile_path_;
}

TestingPrefService* TestingProfile::GetTestingPrefService() {
  if (!prefs_.get())
    CreateTestingPrefService();
  DCHECK(testing_prefs_);
  return testing_prefs_;
}

TestingProfile* TestingProfile::AsTestingProfile() {
  return this;
}

std::string TestingProfile::GetProfileName() {
  return std::string("testing_profile");
}

bool TestingProfile::IsOffTheRecord() const {
  return incognito_;
}

void TestingProfile::SetOffTheRecordProfile(Profile* profile) {
  incognito_profile_.reset(profile);
}

Profile* TestingProfile::GetOffTheRecordProfile() {
  return incognito_profile_.get();
}

GAIAInfoUpdateService* TestingProfile::GetGAIAInfoUpdateService() {
  return NULL;
}

bool TestingProfile::HasOffTheRecordProfile() {
  return incognito_profile_.get() != NULL;
}

Profile* TestingProfile::GetOriginalProfile() {
  return this;
}

VisitedLinkMaster* TestingProfile::GetVisitedLinkMaster() {
  return NULL;
}

ExtensionPrefValueMap* TestingProfile::GetExtensionPrefValueMap() {
  return NULL;
}

ExtensionService* TestingProfile::GetExtensionService() {
  return ExtensionSystemFactory::GetForProfile(this)->extension_service();
}

UserScriptMaster* TestingProfile::GetUserScriptMaster() {
  return ExtensionSystemFactory::GetForProfile(this)->user_script_master();
}

ExtensionProcessManager* TestingProfile::GetExtensionProcessManager() {
  return ExtensionSystemFactory::GetForProfile(this)->process_manager();
}

ExtensionEventRouter* TestingProfile::GetExtensionEventRouter() {
  return ExtensionSystemFactory::GetForProfile(this)->event_router();
}

void TestingProfile::SetExtensionSpecialStoragePolicy(
    ExtensionSpecialStoragePolicy* extension_special_storage_policy) {
  extension_special_storage_policy_ = extension_special_storage_policy;
}

ExtensionSpecialStoragePolicy*
TestingProfile::GetExtensionSpecialStoragePolicy() {
  if (!extension_special_storage_policy_.get())
    extension_special_storage_policy_ = new ExtensionSpecialStoragePolicy(NULL);
  return extension_special_storage_policy_.get();
}

FaviconService* TestingProfile::GetFaviconService(ServiceAccessType access) {
  return favicon_service_.get();
}

HistoryService* TestingProfile::GetHistoryService(ServiceAccessType access) {
  return history_service_.get();
}

HistoryService* TestingProfile::GetHistoryServiceWithoutCreating() {
  return history_service_.get();
}

net::CookieMonster* TestingProfile::GetCookieMonster() {
  if (!GetRequestContext())
    return NULL;
  return GetRequestContext()->GetURLRequestContext()->cookie_store()->
      GetCookieMonster();
}

AutocompleteClassifier* TestingProfile::GetAutocompleteClassifier() {
  return autocomplete_classifier_.get();
}

history::ShortcutsBackend* TestingProfile::GetShortcutsBackend() {
  return NULL;
}

WebDataService* TestingProfile::GetWebDataService(ServiceAccessType access) {
  return web_data_service_.get();
}

WebDataService* TestingProfile::GetWebDataServiceWithoutCreating() {
  return web_data_service_.get();
}

void TestingProfile::SetPrefService(PrefService* prefs) {
#if defined(ENABLE_PROTECTOR_SERVICE)
  // ProtectorService binds itself very closely to the PrefService at the moment
  // of Profile creation and watches pref changes to update their backup.
  // For tests that replace the PrefService after TestingProfile creation,
  // ProtectorService is disabled to prevent further invalid memory accesses.
  protector::ProtectorServiceFactory::GetInstance()->
      SetTestingFactory(this, NULL);
#endif
  prefs_.reset(prefs);
}

void TestingProfile::CreateTestingPrefService() {
  DCHECK(!prefs_.get());
  testing_prefs_ = new TestingPrefService();
  prefs_.reset(testing_prefs_);
  Profile::RegisterUserPrefs(prefs_.get());
  browser::RegisterUserPrefs(prefs_.get());
}

PrefService* TestingProfile::GetPrefs() {
  if (!prefs_.get()) {
    CreateTestingPrefService();
  }
  return prefs_.get();
}

history::TopSites* TestingProfile::GetTopSites() {
  return top_sites_.get();
}

history::TopSites* TestingProfile::GetTopSitesWithoutCreating() {
  return top_sites_.get();
}

DownloadManager* TestingProfile::GetDownloadManager() {
  return NULL;
}

net::URLRequestContextGetter* TestingProfile::GetRequestContext() {
  return request_context_.get();
}

net::URLRequestContextGetter* TestingProfile::GetRequestContextForRenderProcess(
    int renderer_child_id) {
  ExtensionService* extension_service =
      ExtensionSystemFactory::GetForProfile(this)->extension_service();
  if (extension_service) {
    const Extension* installed_app = extension_service->
        GetInstalledAppForRenderer(renderer_child_id);
    if (installed_app != NULL && installed_app->is_storage_isolated())
      return GetRequestContextForIsolatedApp(installed_app->id());
  }

  return GetRequestContext();
}

void TestingProfile::CreateRequestContext() {
  if (!request_context_)
    request_context_ =
        new TestURLRequestContextGetter(
            BrowserThread::GetMessageLoopProxyForThread(BrowserThread::IO));
}

void TestingProfile::ResetRequestContext() {
  // Any objects holding live URLFetchers should be deleted before the request
  // context is shut down.
  TemplateURLFetcherFactory::ShutdownForProfile(this);

  request_context_ = NULL;
}

net::URLRequestContextGetter* TestingProfile::GetRequestContextForMedia() {
  return NULL;
}

net::URLRequestContextGetter* TestingProfile::GetRequestContextForExtensions() {
  if (!extensions_request_context_)
      extensions_request_context_ = new TestExtensionURLRequestContextGetter();
  return extensions_request_context_.get();
}

net::SSLConfigService* TestingProfile::GetSSLConfigService() {
  return NULL;
}

UserStyleSheetWatcher* TestingProfile::GetUserStyleSheetWatcher() {
  return NULL;
}

net::URLRequestContextGetter* TestingProfile::GetRequestContextForIsolatedApp(
    const std::string& app_id) {
  // We don't test isolated app storage here yet, so returning the same dummy
  // context is sufficient for now.
  return GetRequestContext();
}

content::ResourceContext* TestingProfile::GetResourceContext() {
  if (!resource_context_.get())
    resource_context_.reset(new content::MockResourceContext());
  return resource_context_.get();
}

HostContentSettingsMap* TestingProfile::GetHostContentSettingsMap() {
  if (!host_content_settings_map_.get()) {
    host_content_settings_map_ = new HostContentSettingsMap(
        GetPrefs(), GetExtensionService(), false);
  }
  return host_content_settings_map_.get();
}

content::GeolocationPermissionContext*
TestingProfile::GetGeolocationPermissionContext() {
  if (!geolocation_permission_context_.get()) {
    geolocation_permission_context_ =
        new ChromeGeolocationPermissionContext(this);
  }
  return geolocation_permission_context_.get();
}

content::SpeechRecognitionPreferences*
    TestingProfile::GetSpeechRecognitionPreferences() {
#if defined(ENABLE_INPUT_SPEECH)
  if (!speech_recognition_preferences_.get())
    speech_recognition_preferences_ =
        new ChromeSpeechRecognitionPreferences(GetPrefs());
  return speech_recognition_preferences_.get();
#else
  return NULL;
#endif
}

std::wstring TestingProfile::GetName() {
  return std::wstring();
}

std::wstring TestingProfile::GetID() {
  return id_;
}

void TestingProfile::SetID(const std::wstring& id) {
  id_ = id;
}

bool TestingProfile::DidLastSessionExitCleanly() {
  return last_session_exited_cleanly_;
}

BookmarkModel* TestingProfile::GetBookmarkModel() {
  return bookmark_bar_model_.get();
}

bool TestingProfile::IsSameProfile(Profile *p) {
  return this == p;
}

base::Time TestingProfile::GetStartTime() const {
  return start_time_;
}

ProtocolHandlerRegistry* TestingProfile::GetProtocolHandlerRegistry() {
  return protocol_handler_registry_.get();
}

FilePath TestingProfile::last_selected_directory() {
  return last_selected_directory_;
}

void TestingProfile::set_last_selected_directory(const FilePath& path) {
  last_selected_directory_ = path;
}

PrefProxyConfigTracker* TestingProfile::GetProxyConfigTracker() {
  if (!pref_proxy_config_tracker_.get()) {
    pref_proxy_config_tracker_.reset(
        ProxyServiceFactory::CreatePrefProxyConfigTracker(GetPrefs()));
  }
  return pref_proxy_config_tracker_.get();
}

void TestingProfile::BlockUntilHistoryProcessesPendingRequests() {
  DCHECK(history_service_.get());
  DCHECK(MessageLoop::current());

  CancelableRequestConsumer consumer;
  history_service_->ScheduleDBTask(new QuittingHistoryDBTask(), &consumer);
  MessageLoop::current()->Run();
}

ChromeURLDataManager* TestingProfile::GetChromeURLDataManager() {
  if (!chrome_url_data_manager_.get())
    chrome_url_data_manager_.reset(
        new ChromeURLDataManager(
            base::Callback<ChromeURLDataManagerBackend*(void)>()));
  return chrome_url_data_manager_.get();
}

chrome_browser_net::Predictor* TestingProfile::GetNetworkPredictor() {
  return NULL;
}

void TestingProfile::ClearNetworkingHistorySince(base::Time time) {
  NOTIMPLEMENTED();
}

GURL TestingProfile::GetHomePage() {
  return GURL(chrome::kChromeUINewTabURL);
}

PrefService* TestingProfile::GetOffTheRecordPrefs() {
  return NULL;
}

quota::SpecialStoragePolicy* TestingProfile::GetSpecialStoragePolicy() {
  return GetExtensionSpecialStoragePolicy();
}

void TestingProfile::DestroyWebDataService() {
  if (!web_data_service_.get())
    return;

  web_data_service_->Shutdown();
}

bool TestingProfile::WasCreatedByVersionOrLater(const std::string& version) {
  return true;
}
