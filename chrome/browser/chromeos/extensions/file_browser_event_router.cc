// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/extensions/file_browser_event_router.h"

#include "base/bind.h"
#include "base/json/json_writer.h"
#include "base/message_loop.h"
#include "base/stl_util.h"
#include "base/values.h"
#include "chrome/browser/chromeos/extensions/file_browser_notifications.h"
#include "chrome/browser/chromeos/extensions/file_manager_util.h"
#include "chrome/browser/chromeos/gdata/gdata_system_service.h"
#include "chrome/browser/chromeos/gdata/gdata_util.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/extensions/extension_event_names.h"
#include "chrome/browser/extensions/extension_event_router.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_dependency_manager.h"
#include "chrome/common/chrome_notification_types.h"
#include "content/public/browser/notification_source.h"
#include "grit/generated_resources.h"
#include "webkit/fileapi/file_system_types.h"
#include "webkit/fileapi/file_system_util.h"

using chromeos::disks::DiskMountManager;
using chromeos::disks::DiskMountManagerEventType;
using content::BrowserThread;
using gdata::GDataFileSystem;
using gdata::GDataSystemService;
using gdata::GDataSystemServiceFactory;

namespace {
  const char kDiskAddedEventType[] = "added";
  const char kDiskRemovedEventType[] = "removed";

  const char kPathChanged[] = "changed";
  const char kPathWatchError[] = "error";

  DictionaryValue* DiskToDictionaryValue(
      const DiskMountManager::Disk* disk) {
    DictionaryValue* result = new DictionaryValue();
    result->SetString("mountPath", disk->mount_path());
    result->SetString("devicePath", disk->device_path());
    result->SetString("label", disk->device_label());
    result->SetString("deviceType",
        DiskMountManager::DeviceTypeToString(disk->device_type()));
    result->SetInteger("totalSizeKB", disk->total_size_in_bytes() / 1024);
    result->SetBoolean("readOnly", disk->is_read_only());
    return result;
  }
}

const char* MountErrorToString(chromeos::MountError error) {
  switch (error) {
    case chromeos::MOUNT_ERROR_NONE:
      return "success";
    case chromeos::MOUNT_ERROR_UNKNOWN:
      return "error_unknown";
    case chromeos::MOUNT_ERROR_INTERNAL:
      return "error_internal";
    case chromeos::MOUNT_ERROR_UNKNOWN_FILESYSTEM:
      return "error_unknown_filesystem";
    case chromeos::MOUNT_ERROR_UNSUPORTED_FILESYSTEM:
      return "error_unsuported_filesystem";
    case chromeos::MOUNT_ERROR_INVALID_ARCHIVE:
      return "error_invalid_archive";
    case chromeos::MOUNT_ERROR_LIBRARY_NOT_LOADED:
      return "error_libcros_missing";
    case chromeos::MOUNT_ERROR_NOT_AUTHENTICATED:
      return "error_authentication";
    case chromeos::MOUNT_ERROR_NETWORK_ERROR:
      return "error_libcros_missing";
    case chromeos::MOUNT_ERROR_PATH_UNMOUNTED:
      return "error_path_unmounted";
    default:
      NOTREACHED();
  }
  return "";
}

FileBrowserEventRouter::FileBrowserEventRouter(
    Profile* profile)
    : delegate_(new FileBrowserEventRouter::FileWatcherDelegate(this)),
      notifications_(new FileBrowserNotifications(profile)),
      profile_(profile) {
}

FileBrowserEventRouter::~FileBrowserEventRouter() {
}

void FileBrowserEventRouter::ShutdownOnUIThread() {
  DCHECK(file_watchers_.empty());
  STLDeleteValues(&file_watchers_);
  if (!profile_) {
    NOTREACHED();
    return;
  }
  DiskMountManager::GetInstance()->RemoveObserver(this);

  GDataSystemService* system_service =
      GDataSystemServiceFactory::FindForProfile(profile_);
  if (system_service) {
    system_service->file_system()->RemoveObserver(this);
    system_service->file_system()->RemoveOperationObserver(this);
  }

  profile_ = NULL;
}

void FileBrowserEventRouter::ObserveFileSystemEvents() {
  if (!profile_) {
    NOTREACHED();
    return;
  }
  if (!chromeos::UserManager::Get()->IsUserLoggedIn())
    return;

  DiskMountManager* disk_mount_manager = DiskMountManager::GetInstance();
  disk_mount_manager->RemoveObserver(this);
  disk_mount_manager->AddObserver(this);
  disk_mount_manager->RequestMountInfoRefresh();

  GDataSystemService* system_service =
      GDataSystemServiceFactory::GetForProfile(profile_);
  if (!system_service) {
    NOTREACHED();
    return;
  }
  system_service->file_system()->AddOperationObserver(this);
  system_service->file_system()->AddObserver(this);
}

// File watch setup routines.
bool FileBrowserEventRouter::AddFileWatch(
    const FilePath& local_path,
    const FilePath& virtual_path,
    const std::string& extension_id) {
  base::AutoLock lock(lock_);
  FilePath watch_path = local_path;
  bool is_remote_watch = false;
  // Tweak watch path for remote sources - we need to drop leading /special
  // directory from there in order to be able to pair these events with
  // their change notifications.
  if (gdata::util::GetSpecialRemoteRootPath().IsParent(watch_path)) {
    watch_path = gdata::util::ExtractGDataPath(watch_path);
    is_remote_watch = true;
  }

  WatcherMap::iterator iter = file_watchers_.find(watch_path);
  if (iter == file_watchers_.end()) {
    scoped_ptr<FileWatcherExtensions>
        watch(new FileWatcherExtensions(virtual_path,
                                        extension_id,
                                        is_remote_watch));

    if (watch->Watch(watch_path, delegate_.get()))
      file_watchers_[watch_path] = watch.release();
    else
      return false;
  } else {
    iter->second->AddExtension(extension_id);
  }
  return true;
}

void FileBrowserEventRouter::RemoveFileWatch(
    const FilePath& local_path,
    const std::string& extension_id) {
  base::AutoLock lock(lock_);
  WatcherMap::iterator iter = file_watchers_.find(local_path);
  if (iter == file_watchers_.end())
    return;
  // Remove the renderer process for this watch.
  iter->second->RemoveExtension(extension_id);
  if (iter->second->GetRefCount() == 0) {
    delete iter->second;
    file_watchers_.erase(iter);
  }
}

void FileBrowserEventRouter::DiskChanged(
    DiskMountManagerEventType event,
    const DiskMountManager::Disk* disk) {
  // Disregard hidden devices.
  if (disk->is_hidden())
    return;
  if (event == chromeos::disks::MOUNT_DISK_ADDED) {
    OnDiskAdded(disk);
  } else if (event == chromeos::disks::MOUNT_DISK_REMOVED) {
    OnDiskRemoved(disk);
  }
}

void FileBrowserEventRouter::DeviceChanged(
    DiskMountManagerEventType event,
    const std::string& device_path) {
  if (event == chromeos::disks::MOUNT_DEVICE_ADDED) {
    OnDeviceAdded(device_path);
  } else if (event == chromeos::disks::MOUNT_DEVICE_REMOVED) {
    OnDeviceRemoved(device_path);
  } else if (event == chromeos::disks::MOUNT_DEVICE_SCANNED) {
    OnDeviceScanned(device_path);
  } else if (event == chromeos::disks::MOUNT_FORMATTING_STARTED) {
  // TODO(tbarzic): get rid of '!'.
    if (device_path[0] == '!') {
      OnFormattingStarted(device_path.substr(1), false);
    } else {
      OnFormattingStarted(device_path, true);
    }
  } else if (event == chromeos::disks::MOUNT_FORMATTING_FINISHED) {
    if (device_path[0] == '!') {
      OnFormattingFinished(device_path.substr(1), false);
    } else {
      OnFormattingFinished(device_path, true);
    }
  }
}

void FileBrowserEventRouter::MountCompleted(
    DiskMountManager::MountEvent event_type,
    chromeos::MountError error_code,
    const DiskMountManager::MountPointInfo& mount_info) {
  DispatchMountCompletedEvent(event_type, error_code, mount_info);

  if (mount_info.mount_type == chromeos::MOUNT_TYPE_DEVICE &&
      event_type == DiskMountManager::MOUNTING) {
    DiskMountManager* disk_mount_manager = DiskMountManager::GetInstance();
    DiskMountManager::DiskMap::const_iterator disk_it =
        disk_mount_manager->disks().find(mount_info.source_path);
    if (disk_it == disk_mount_manager->disks().end()) {
      return;
    }
    DiskMountManager::Disk* disk = disk_it->second;

     notifications_->ManageNotificationsOnMountCompleted(
        disk->system_path_prefix(), disk->drive_label(), disk->is_parent(),
        error_code == chromeos::MOUNT_ERROR_NONE,
        error_code == chromeos::MOUNT_ERROR_UNSUPORTED_FILESYSTEM);
  }
}

void FileBrowserEventRouter::OnProgressUpdate(
    const std::vector<gdata::GDataOperationRegistry::ProgressStatus>& list) {
  scoped_ptr<ListValue> event_list(
      file_manager_util::ProgressStatusVectorToListValue(
          profile_,
          file_manager_util::GetFileBrowserExtensionUrl().GetOrigin(),
          list));

  ListValue args;
  args.Append(event_list.release());

  std::string args_json;
  base::JSONWriter::Write(&args,
                          &args_json);

  profile_->GetExtensionEventRouter()->DispatchEventToExtension(
      std::string(kFileBrowserDomain),
      extension_event_names::kOnFileTransfersUpdated, args_json,
      NULL, GURL());
}

void FileBrowserEventRouter::OnDirectoryChanged(
    const FilePath& directory_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  HandleFileWatchNotification(directory_path, false);
}

void FileBrowserEventRouter::HandleFileWatchNotification(
    const FilePath& local_path, bool got_error) {
  base::AutoLock lock(lock_);
  WatcherMap::const_iterator iter = file_watchers_.find(local_path);
  if (iter == file_watchers_.end()) {
    return;
  }
  DispatchFolderChangeEvent(iter->second->GetVirtualPath(), got_error,
                            iter->second->GetExtensions());
}

void FileBrowserEventRouter::DispatchFolderChangeEvent(
    const FilePath& virtual_path, bool got_error,
    const FileBrowserEventRouter::ExtensionUsageRegistry& extensions) {
  if (!profile_) {
    NOTREACHED();
    return;
  }

  for (ExtensionUsageRegistry::const_iterator iter = extensions.begin();
       iter != extensions.end(); ++iter) {
    GURL target_origin_url(Extension::GetBaseURLFromExtensionId(
        iter->first));
    GURL base_url = fileapi::GetFileSystemRootURI(target_origin_url,
        fileapi::kFileSystemTypeExternal);
    GURL target_file_url = GURL(base_url.spec() + virtual_path.value());
    ListValue args;
    DictionaryValue* watch_info = new DictionaryValue();
    args.Append(watch_info);
    watch_info->SetString("fileUrl", target_file_url.spec());
    watch_info->SetString("eventType",
                          got_error ? kPathWatchError : kPathChanged);

    std::string args_json;
    base::JSONWriter::Write(&args, &args_json);

    profile_->GetExtensionEventRouter()->DispatchEventToExtension(
        iter->first, extension_event_names::kOnFileChanged, args_json,
        NULL, GURL());
  }
}

void FileBrowserEventRouter::DispatchDiskEvent(
    const DiskMountManager::Disk* disk, bool added) {
  if (!profile_) {
    NOTREACHED();
    return;
  }

  ListValue args;
  DictionaryValue* mount_info = new DictionaryValue();
  args.Append(mount_info);
  mount_info->SetString("eventType",
                        added ? kDiskAddedEventType : kDiskRemovedEventType);
  DictionaryValue* disk_info = DiskToDictionaryValue(disk);
  mount_info->Set("volumeInfo", disk_info);

  std::string args_json;
  base::JSONWriter::Write(&args, &args_json);
  profile_->GetExtensionEventRouter()->DispatchEventToRenderers(
      extension_event_names::kOnFileBrowserDiskChanged, args_json, NULL,
      GURL());
}

void FileBrowserEventRouter::DispatchMountCompletedEvent(
    DiskMountManager::MountEvent event,
    chromeos::MountError error_code,
    const DiskMountManager::MountPointInfo& mount_info) {
  if (!profile_ || mount_info.mount_type == chromeos::MOUNT_TYPE_INVALID) {
    NOTREACHED();
    return;
  }

  ListValue args;
  DictionaryValue* mount_info_value = new DictionaryValue();
  args.Append(mount_info_value);
  mount_info_value->SetString("eventType",
      event == DiskMountManager::MOUNTING ? "mount" : "unmount");
  mount_info_value->SetString("status", MountErrorToString(error_code));
  mount_info_value->SetString(
      "mountType",
      DiskMountManager::MountTypeToString(mount_info.mount_type));

  if (mount_info.mount_type == chromeos::MOUNT_TYPE_ARCHIVE ||
      mount_info.mount_type == chromeos::MOUNT_TYPE_GDATA) {
    GURL source_url;
    if (file_manager_util::ConvertFileToFileSystemUrl(profile_,
            FilePath(mount_info.source_path),
            file_manager_util::GetFileBrowserExtensionUrl().GetOrigin(),
            &source_url)) {
      mount_info_value->SetString("sourceUrl", source_url.spec());
    } else {
      // If mounting of gdata moutn point failed, we may not be able to convert
      // source path to source url, so let just send empty string.
      DCHECK(mount_info.mount_type == chromeos::MOUNT_TYPE_GDATA &&
             error_code != chromeos::MOUNT_ERROR_NONE);
      mount_info_value->SetString("sourceUrl", "");
    }
  } else {
    mount_info_value->SetString("sourceUrl", mount_info.source_path);
  }

  FilePath relative_mount_path;
  bool relative_mount_path_set = false;

  // If there were no error or some special conditions occured, add mountPath
  // to the event.
  if (error_code == chromeos::MOUNT_ERROR_NONE ||
      mount_info.mount_condition) {
    // Convert mount point path to relative path with the external file system
    // exposed within File API.
    if (file_manager_util::ConvertFileToRelativeFileSystemPath(profile_,
            FilePath(mount_info.mount_path),
            &relative_mount_path)) {
      mount_info_value->SetString("mountPath",
                                  "/" + relative_mount_path.value());
      relative_mount_path_set = true;
    }
  }

  std::string args_json;
  base::JSONWriter::Write(&args, &args_json);
  profile_->GetExtensionEventRouter()->DispatchEventToRenderers(
      extension_event_names::kOnFileBrowserMountCompleted, args_json, NULL,
      GURL());

  if (relative_mount_path_set &&
      mount_info.mount_type == chromeos::MOUNT_TYPE_DEVICE &&
      !mount_info.mount_condition &&
      event == DiskMountManager::MOUNTING) {
    file_manager_util::ViewRemovableDrive(FilePath(mount_info.mount_path));
  }
}

void FileBrowserEventRouter::OnDiskAdded(
    const DiskMountManager::Disk* disk) {
  VLOG(1) << "Disk added: " << disk->device_path();
  if (disk->device_path().empty()) {
    VLOG(1) << "Empty system path for " << disk->device_path();
    return;
  }

  // If disk is not mounted yet, give it a try.
  if (disk->mount_path().empty()) {
    // Initiate disk mount operation.
    DiskMountManager::GetInstance()->MountPath(
        disk->device_path(), chromeos::MOUNT_TYPE_DEVICE);
  }
  DispatchDiskEvent(disk, true);
}

void FileBrowserEventRouter::OnDiskRemoved(
    const DiskMountManager::Disk* disk) {
  VLOG(1) << "Disk removed: " << disk->device_path();

  if (!disk->mount_path().empty()) {
    DiskMountManager::GetInstance()->UnmountPath(disk->mount_path());
  }
  DispatchDiskEvent(disk, false);
}

void FileBrowserEventRouter::OnDeviceAdded(
    const std::string& device_path) {
  VLOG(1) << "Device added : " << device_path;

  notifications_->RegisterDevice(device_path);
  notifications_->ShowNotificationDelayed(FileBrowserNotifications::DEVICE,
                                          device_path,
                                          4000);
}

void FileBrowserEventRouter::OnDeviceRemoved(
    const std::string& device_path) {
  VLOG(1) << "Device removed : " << device_path;
  notifications_->HideNotification(FileBrowserNotifications::DEVICE,
                                   device_path);
  notifications_->HideNotification(FileBrowserNotifications::DEVICE_FAIL,
                                   device_path);
  notifications_->UnregisterDevice(device_path);
}

void FileBrowserEventRouter::OnDeviceScanned(
    const std::string& device_path) {
  VLOG(1) << "Device scanned : " << device_path;
}

void FileBrowserEventRouter::OnFormattingStarted(
    const std::string& device_path, bool success) {
  if (success) {
    notifications_->ShowNotification(FileBrowserNotifications::FORMAT_START,
                                     device_path);
  } else {
    notifications_->ShowNotification(
        FileBrowserNotifications::FORMAT_START_FAIL, device_path);
  }
}

void FileBrowserEventRouter::OnFormattingFinished(
    const std::string& device_path, bool success) {
  if (success) {
    notifications_->HideNotification(FileBrowserNotifications::FORMAT_START,
                                     device_path);
    notifications_->ShowNotification(FileBrowserNotifications::FORMAT_SUCCESS,
                                     device_path);
    // Hide it after a couple of seconds.
    notifications_->HideNotificationDelayed(
        FileBrowserNotifications::FORMAT_SUCCESS, device_path, 4000);

    DiskMountManager::GetInstance()->MountPath(device_path,
                                               chromeos::MOUNT_TYPE_DEVICE);
  } else {
    notifications_->HideNotification(FileBrowserNotifications::FORMAT_START,
                                     device_path);
    notifications_->ShowNotification(FileBrowserNotifications::FORMAT_FAIL,
                                     device_path);
  }
}

// FileBrowserEventRouter::WatcherDelegate methods.
FileBrowserEventRouter::FileWatcherDelegate::FileWatcherDelegate(
    FileBrowserEventRouter* router) : router_(router) {
}

void FileBrowserEventRouter::FileWatcherDelegate::OnFilePathChanged(
    const FilePath& local_path) {
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&FileWatcherDelegate::HandleFileWatchOnUIThread,
                 this, local_path, false));
}

void FileBrowserEventRouter::FileWatcherDelegate::OnFilePathError(
    const FilePath& local_path) {
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
          base::Bind(&FileWatcherDelegate::HandleFileWatchOnUIThread,
                     this, local_path, true));
}

void
FileBrowserEventRouter::FileWatcherDelegate::HandleFileWatchOnUIThread(
     const FilePath& local_path, bool got_error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  router_->HandleFileWatchNotification(local_path, got_error);
}


FileBrowserEventRouter::FileWatcherExtensions::FileWatcherExtensions(
    const FilePath& path, const std::string& extension_id,
    bool is_remote_file_system)
    : ref_count_(0),
      is_remote_file_system_(is_remote_file_system) {
  if (!is_remote_file_system_)
    file_watcher_.reset(new base::files::FilePathWatcher());

  virtual_path_ = path;
  AddExtension(extension_id);
}

void FileBrowserEventRouter::FileWatcherExtensions::AddExtension(
    const std::string& extension_id) {
  ExtensionUsageRegistry::iterator it = extensions_.find(extension_id);
  if (it != extensions_.end()) {
    it->second++;
  } else {
    extensions_.insert(ExtensionUsageRegistry::value_type(extension_id, 1));
  }

  ref_count_++;
}

void FileBrowserEventRouter::FileWatcherExtensions::RemoveExtension(
    const std::string& extension_id) {
  ExtensionUsageRegistry::iterator it = extensions_.find(extension_id);

  if (it != extensions_.end()) {
    // If entry found - decrease it's count and remove if necessary
    if (0 == it->second--) {
      extensions_.erase(it);
    }

    ref_count_--;
  } else {
    // Might be reference counting problem - e.g. if some component of
    // extension subscribes/unsubscribes correctly, but other component
    // only unsubscribes, developer of first one might receive this message
    LOG(FATAL) << " Extension [" << extension_id
        << "] tries to unsubscribe from folder [" << local_path_.value()
        << "] it isn't subscribed";
  }
}

const FileBrowserEventRouter::ExtensionUsageRegistry&
FileBrowserEventRouter::FileWatcherExtensions::GetExtensions() const {
  return extensions_;
}

unsigned int
FileBrowserEventRouter::FileWatcherExtensions::GetRefCount() const {
  return ref_count_;
}

const FilePath&
FileBrowserEventRouter::FileWatcherExtensions::GetVirtualPath() const {
  return virtual_path_;
}

bool FileBrowserEventRouter::FileWatcherExtensions::Watch
    (const FilePath& path, FileWatcherDelegate* delegate) {
  if (is_remote_file_system_)
    return true;

  return file_watcher_->Watch(path, delegate);
}

// static
scoped_refptr<FileBrowserEventRouter>
FileBrowserEventRouterFactory::GetForProfile(Profile* profile) {
  return static_cast<FileBrowserEventRouter*>(
      GetInstance()->GetServiceForProfile(profile, true).get());
}

// static
FileBrowserEventRouterFactory*
FileBrowserEventRouterFactory::GetInstance() {
  return Singleton<FileBrowserEventRouterFactory>::get();
}

FileBrowserEventRouterFactory::FileBrowserEventRouterFactory()
    : RefcountedProfileKeyedServiceFactory("FileBrowserEventRouter",
          ProfileDependencyManager::GetInstance()) {
  DependsOn(GDataSystemServiceFactory::GetInstance());
}

FileBrowserEventRouterFactory::~FileBrowserEventRouterFactory() {
}

scoped_refptr<RefcountedProfileKeyedService>
FileBrowserEventRouterFactory::BuildServiceInstanceFor(Profile* profile) const {
  return scoped_refptr<RefcountedProfileKeyedService>(
      new FileBrowserEventRouter(profile));
}

bool FileBrowserEventRouterFactory::ServiceHasOwnInstanceInIncognito() {
  // Explicitly and always allow this router in guest login mode.   see
  // chrome/browser/profiles/profile_keyed_base_factory.h comment
  // for the details.
  return true;
}
