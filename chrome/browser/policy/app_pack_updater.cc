// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/app_pack_updater.h"

#include "base/bind.h"
#include "base/file_util.h"
#include "base/location.h"
#include "base/stl_util.h"
#include "base/string_util.h"
#include "base/values.h"
#include "base/version.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/cros_settings.h"
#include "chrome/browser/chromeos/cros_settings_names.h"
#include "chrome/browser/extensions/external_extension_loader.h"
#include "chrome/browser/extensions/external_extension_provider_impl.h"
#include "chrome/browser/extensions/updater/extension_downloader.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/extensions/extension.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_details.h"

// TODO(joaodasilva): remove files from the cache when the crx fails validation.

using content::BrowserThread;
using file_util::FileEnumerator;

namespace policy {

namespace {

// Directory where the AppPack extensions are cached.
const char kAppPackCacheDir[] = "/var/cache/app_pack";

// File name extension for CRX files (not case sensitive).
const char kCRXFileExtension[] = ".crx";

}  // namespace

const char AppPackUpdater::kExtensionId[] = "extension-id";
const char AppPackUpdater::kUpdateUrl[]   = "update-url";

// A custom ExternalExtensionLoader that the AppPackUpdater creates and uses to
// publish AppPack updates to the extensions system.
class AppPackExternalExtensionLoader
    : public ExternalExtensionLoader,
      public base::SupportsWeakPtr<AppPackExternalExtensionLoader> {
 public:
  AppPackExternalExtensionLoader() {}
  virtual ~AppPackExternalExtensionLoader() {}

  // Used by the AppPackUpdater to update the current list of extensions.
  // The format of |prefs| is detailed in the ExternalExtensionLoader/Provider
  // headers.
  void SetCurrentAppPackExtensions(scoped_ptr<base::DictionaryValue> prefs) {
    app_pack_prefs_.Swap(prefs.get());
    StartLoading();
  }

  // Implementation of ExternalExtensionLoader:
  virtual void StartLoading() OVERRIDE {
    prefs_.reset(app_pack_prefs_.DeepCopy());
    VLOG(1) << "AppPack extension loader publishing "
            << app_pack_prefs_.size() << " crx files.";
    LoadFinished();
  }

 private:
  base::DictionaryValue app_pack_prefs_;

  DISALLOW_COPY_AND_ASSIGN(AppPackExternalExtensionLoader);
};

AppPackUpdater::AppPackUpdater(net::URLRequestContextGetter* request_context,
                               BrowserPolicyConnector* connector)
    : ALLOW_THIS_IN_INITIALIZER_LIST(weak_ptr_factory_(this)),
      created_extension_loader_(false),
      request_context_(request_context) {
  if (connector->GetDeviceMode() == DEVICE_MODE_KIOSK) {
    // Already in Kiosk mode, start loading.
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                            base::Bind(&AppPackUpdater::Init,
                                       weak_ptr_factory_.GetWeakPtr()));
  } else if (connector->GetDeviceMode() == DEVICE_MODE_UNKNOWN &&
             connector->device_cloud_policy_subsystem()) {
    // Not enrolled yet, listen for enrollment.
    registrar_.reset(new CloudPolicySubsystem::ObserverRegistrar(
        connector->device_cloud_policy_subsystem(), this));
  } else {
    // Linger as a stub.
  }
}

AppPackUpdater::~AppPackUpdater() {
  chromeos::CrosSettings::Get()->RemoveSettingsObserver(
      chromeos::kAppPack, this);
}

ExternalExtensionLoader* AppPackUpdater::CreateExternalExtensionLoader() {
  if (created_extension_loader_) {
    NOTREACHED();
    return NULL;
  }
  created_extension_loader_ = true;
  AppPackExternalExtensionLoader* loader = new AppPackExternalExtensionLoader();
  extension_loader_ = loader->AsWeakPtr();

  // The cache may have been already checked. In that case, load the current
  // extensions into the loader immediately.
  UpdateExtensionLoader();

  return loader;
}

void AppPackUpdater::SetScreenSaverUpdateCallback(
    const AppPackUpdater::ScreenSaverUpdateCallback& callback) {
  screen_saver_update_callback_ = callback;
  if (!screen_saver_update_callback_.is_null() && !screen_saver_path_.empty()) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(screen_saver_update_callback_, screen_saver_path_));
  }
}

void AppPackUpdater::Init() {
  worker_pool_token_ = BrowserThread::GetBlockingPool()->GetSequenceToken();
  chromeos::CrosSettings::Get()->AddSettingsObserver(chromeos::kAppPack, this);
  LoadPolicy();
}

void AppPackUpdater::OnPolicyStateChanged(
    CloudPolicySubsystem::PolicySubsystemState state,
    CloudPolicySubsystem::ErrorDetails error_details) {
  if (state == CloudPolicySubsystem::SUCCESS) {
    registrar_.reset();
    Init();
  }
}

void AppPackUpdater::Observe(int type,
                             const content::NotificationSource& source,
                             const content::NotificationDetails& details) {
  DCHECK_EQ(type, chrome::NOTIFICATION_SYSTEM_SETTING_CHANGED);
  DCHECK_EQ(chromeos::kAppPack,
            *content::Details<const std::string>(details).ptr());
  LoadPolicy();
}

void AppPackUpdater::LoadPolicy() {
  chromeos::CrosSettings* settings = chromeos::CrosSettings::Get();
  if (!settings->PrepareTrustedValues(
          base::Bind(&AppPackUpdater::LoadPolicy,
                     weak_ptr_factory_.GetWeakPtr()))) {
    return;
  }

  app_pack_extensions_.clear();
  const base::Value* value = settings->GetPref(chromeos::kAppPack);
  const base::ListValue* list = NULL;
  if (value && value->GetAsList(&list)) {
    for (base::ListValue::const_iterator it = list->begin();
         it != list->end(); ++it) {
      base::DictionaryValue* dict = NULL;
      if (!(*it)->GetAsDictionary(&dict)) {
        LOG(WARNING) << "AppPack entry is not a dictionary, ignoring.";
        continue;
      }
      std::string id;
      std::string update_url;
      if (dict->GetString(kExtensionId, &id) &&
          dict->GetString(kUpdateUrl, &update_url)) {
        app_pack_extensions_[id] = update_url;
      } else {
        LOG(WARNING) << "Failed to read required fields for an AppPack entry, "
                     << "ignoring.";
      }
    }
  }

  VLOG(1) << "Refreshed AppPack policy, got " << app_pack_extensions_.size()
          << " entries.";

  value = settings->GetPref(chromeos::kScreenSaverExtensionId);
  if (!value || !value->GetAsString(&screen_saver_id_)) {
    screen_saver_id_.clear();
    SetScreenSaverPath(FilePath());
  }

  CheckCacheNow();
}

void AppPackUpdater::CheckCacheNow() {
  std::set<std::string>* valid_ids = new std::set<std::string>();
  for (PolicyEntryMap::iterator it = app_pack_extensions_.begin();
       it != app_pack_extensions_.end(); ++it) {
    valid_ids->insert(it->first);
  }
  PostBlockingTask(FROM_HERE,
                   base::Bind(&AppPackUpdater::BlockingCheckCache,
                              weak_ptr_factory_.GetWeakPtr(),
                              base::Owned(valid_ids)));
}

// static
void AppPackUpdater::BlockingCheckCache(
    base::WeakPtr<AppPackUpdater> app_pack_updater,
    const std::set<std::string>* valid_ids) {
  CacheEntryMap* entries = new CacheEntryMap();
  BlockingCheckCacheInternal(valid_ids, entries);
  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                          base::Bind(&AppPackUpdater::OnCacheUpdated,
                                     app_pack_updater,
                                     base::Owned(entries)));
}

void AppPackUpdater::BlockingCheckCacheInternal(
    const std::set<std::string>* valid_ids,
    CacheEntryMap* entries) {
  // Start by verifying that the cache dir exists.
  FilePath dir(kAppPackCacheDir);
  if (!file_util::DirectoryExists(dir)) {
    // Create it now.
    if (!file_util::CreateDirectory(dir))
      LOG(ERROR) << "Failed to create AppPack directory at " << dir.value();
    // Nothing else to do.
    return;
  }

  // Enumerate all the files in the cache |dir|, including directories
  // and symlinks. Each unrecognized file will be erased.
  FileEnumerator::FileType types = static_cast<FileEnumerator::FileType>(
      FileEnumerator::FILES | FileEnumerator::DIRECTORIES |
      FileEnumerator::SHOW_SYM_LINKS);
  FileEnumerator enumerator(dir, false /* recursive */, types);

  for (FilePath path = enumerator.Next();
       !path.empty(); path = enumerator.Next()) {
    FileEnumerator::FindInfo info;
    enumerator.GetFindInfo(&info);
    std::string basename = path.BaseName().value();

    if (FileEnumerator::IsDirectory(info) || FileEnumerator::IsLink(info)) {
      LOG(ERROR) << "Erasing bad file in AppPack directory: " << basename;
      file_util::Delete(path, true /* recursive */);
      continue;
    }

    // crx files in the cache are named <extension-id>-<version>.crx.
    std::string id;
    std::string version;

    if (EndsWith(basename, kCRXFileExtension, false /* case-sensitive */)) {
      size_t n = basename.find('-');
      if (n != std::string::npos && n + 1 < basename.size() - 4) {
        id = basename.substr(0, n);
        // Size of |version| = total size - "<id>" - "-" - ".crx"
        version = basename.substr(n + 1, basename.size() - 5 - id.size());
      }
    }

    if (!Extension::IdIsValid(id)) {
      LOG(ERROR) << "Bad AppPack extension id in cache: " << id;
      id.clear();
    } else if (!ContainsKey(*valid_ids, id)) {
      LOG(WARNING) << basename << " is in the cache but is not configured by "
                   << "the AppPack policy, and will be erased.";
      id.clear();
    }

    if (!Version(version).IsValid()) {
      LOG(ERROR) << "Bad AppPack extension version in cache: " << version;
      version.clear();
    }

    if (id.empty() || version.empty()) {
      LOG(ERROR) << "Invalid file in AppPack cache, erasing: " << basename;
      file_util::Delete(path, true /* recursive */);
      continue;
    }

    // Enforce a lower-case id.
    id = StringToLowerASCII(id);

    // File seems good so far. Make sure there isn't another entry with the
    // same id but a different version.

    if (ContainsKey(*entries, id)) {
      LOG(ERROR) << "Found two AppPack files for the same extension, will "
                    "erase the oldest version";
      CacheEntry& entry = (*entries)[id];
      Version vEntry(entry.cached_version);
      Version vCurrent(version);
      DCHECK(vEntry.IsValid());
      DCHECK(vCurrent.IsValid());
      if (vEntry.CompareTo(vCurrent) < 0) {
        file_util::Delete(FilePath(entry.path), true /* recursive */);
        entry.path = path.value();
      } else {
        file_util::Delete(path, true /* recursive */);
      }
      continue;
    }

    // This is the only file for this |id| so far, add it.

    CacheEntry& entry = (*entries)[id];
    entry.path = path.value();
    entry.cached_version = version;
  }
}

void AppPackUpdater::OnCacheUpdated(CacheEntryMap* cache_entries) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  cached_extensions_.swap(*cache_entries);

  CacheEntryMap::iterator it = cached_extensions_.find(screen_saver_id_);
  if (it != cached_extensions_.end()) {
    SetScreenSaverPath(FilePath(it->second.path));
    cached_extensions_.erase(it);
  } else {
    SetScreenSaverPath(FilePath());
  }

  VLOG(1) << "Updated AppPack cache, there are " << cached_extensions_.size()
          << " extensions cached and "
          << (screen_saver_path_.empty() ? "no" : "the") << " screensaver";
  UpdateExtensionLoader();
  DownloadMissingExtensions();
}

void AppPackUpdater::UpdateExtensionLoader() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (!extension_loader_) {
    VLOG(1) << "No AppPack loader created yet, not pushing extensions.";
    return;
  }

  // Build a DictionaryValue with the format that ExternalExtensionProviderImpl
  // expects, containing info about the locally cached extensions.

  scoped_ptr<base::DictionaryValue> prefs(new base::DictionaryValue());
  for (CacheEntryMap::iterator it = cached_extensions_.begin();
       it != cached_extensions_.end(); ++it) {
    base::DictionaryValue* dict = new base::DictionaryValue();
    dict->SetString(ExternalExtensionProviderImpl::kExternalCrx,
                    it->second.path);
    dict->SetString(ExternalExtensionProviderImpl::kExternalVersion,
                    it->second.cached_version);
    prefs->Set(it->first, dict);

    VLOG(1) << "Updating AppPack extension loader, added " << it->second.path;
  }

  extension_loader_->SetCurrentAppPackExtensions(prefs.Pass());
}

void AppPackUpdater::DownloadMissingExtensions() {
  // Check for updates for all extensions configured by the policy. Some of
  // them may already be in the cache; only those with updated version will be
  // downloaded, in that case.
  if (!downloader_.get()) {
    downloader_.reset(new extensions::ExtensionDownloader(this,
                                                          request_context_));
  }
  for (PolicyEntryMap::iterator it = app_pack_extensions_.begin();
       it != app_pack_extensions_.end(); ++it) {
    downloader_->AddPendingExtension(it->first, GURL(it->second));
  }
  downloader_->StartAllPending();
}

void AppPackUpdater::OnExtensionDownloadFailed(
    const std::string& id,
    extensions::ExtensionDownloaderDelegate::Error error,
    const extensions::ExtensionDownloaderDelegate::PingResult& ping_result) {
  if (error != NO_UPDATE_AVAILABLE) {
    LOG(ERROR) << "AppPack failed to download extension " << id
               << ", error " << error;
  }
}

void AppPackUpdater::OnExtensionDownloadFinished(
    const std::string& id,
    const FilePath& path,
    const GURL& download_url,
    const std::string& version,
    const extensions::ExtensionDownloaderDelegate::PingResult& ping_result) {
  // The explicit copy ctors are to make sure that Bind() binds a copy and not
  // a reference to the arguments.
  PostBlockingTask(FROM_HERE,
                   base::Bind(&AppPackUpdater::BlockingInstallCacheEntry,
                              weak_ptr_factory_.GetWeakPtr(),
                              std::string(id),
                              FilePath(path),
                              std::string(version)));
}

void AppPackUpdater::OnBlacklistDownloadFinished(
    const std::string& data,
    const std::string& package_hash,
    const std::string& version,
    const extensions::ExtensionDownloaderDelegate::PingResult& ping_result) {
  NOTREACHED();
}

bool AppPackUpdater::IsExtensionPending(const std::string& id) {
  // Pending means that there is no installed version yet.
  return ContainsKey(app_pack_extensions_, id) &&
         !ContainsKey(cached_extensions_, id);
}

bool AppPackUpdater::GetExtensionExistingVersion(const std::string& id,
                                                 std::string* version) {
  if (!ContainsKey(app_pack_extensions_, id) ||
      !ContainsKey(cached_extensions_, id)) {
    return false;
  }

  *version = cached_extensions_[id].cached_version;
  return true;
}

// static
void AppPackUpdater::BlockingInstallCacheEntry(
    base::WeakPtr<AppPackUpdater> app_pack_updater,
    const std::string& id,
    const FilePath& path,
    const std::string& version) {
  Version version_validator(version);
  if (!version_validator.IsValid()) {
    LOG(ERROR) << "AppPack downloaded extension " << id << " but got bad "
               << "version: " << version;
    file_util::Delete(path, true /* recursive */);
    return;
  }

  std::string basename = id + "-" + version + kCRXFileExtension;
  FilePath cache_dir(kAppPackCacheDir);
  FilePath cached_crx_path = cache_dir.Append(basename);

  if (file_util::PathExists(cached_crx_path)) {
    LOG(WARNING) << "AppPack downloaded a crx whose filename will overwrite "
                 << "an existing cached crx.";
    file_util::Delete(cached_crx_path, true /* recursive */);
  }

  if (!file_util::DirectoryExists(cache_dir)) {
    LOG(ERROR) << "AppPack cache directory does not exist, creating now: "
               << cache_dir.value();
    if (!file_util::CreateDirectory(cache_dir)) {
      LOG(ERROR) << "Failed to create the AppPack cache dir!";
      file_util::Delete(path, true /* recursive */);
      return;
    }
  }

  if (!file_util::Move(path, cached_crx_path)) {
    LOG(ERROR) << "Failed to move AppPack crx from " << path.value()
               << " to " << cached_crx_path.value();
    file_util::Delete(path, true /* recursive */);
    return;
  }

  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                          base::Bind(&AppPackUpdater::OnCacheEntryInstalled,
                                     app_pack_updater,
                                     std::string(id),
                                     cached_crx_path.value(),
                                     std::string(version)));
}

void AppPackUpdater::OnCacheEntryInstalled(const std::string& id,
                                           const std::string& path,
                                           const std::string& version) {
  VLOG(1) << "AppPack installed a new extension in the cache: " << path;

  if (id == screen_saver_id_) {
    VLOG(1) << "AppPack got the screen saver extension at " << path;
    SetScreenSaverPath(FilePath(path));
  } else {
    // Add to the list of cached extensions.
    CacheEntry& entry = cached_extensions_[id];
    entry.path = path;
    entry.cached_version = version;
    UpdateExtensionLoader();
  }
}

void AppPackUpdater::PostBlockingTask(const tracked_objects::Location& location,
                                      const base::Closure& task) {
  BrowserThread::GetBlockingPool()->PostSequencedWorkerTaskWithShutdownBehavior(
      worker_pool_token_, location, task,
      base::SequencedWorkerPool::SKIP_ON_SHUTDOWN);
}

void AppPackUpdater::SetScreenSaverPath(const FilePath& path) {
  // Don't invoke the callback if the path isn't changing.
  if (path != screen_saver_path_) {
    screen_saver_path_ = path;
    if (!screen_saver_update_callback_.is_null())
      screen_saver_update_callback_.Run(screen_saver_path_);
  }
}

}  // namespace policy
