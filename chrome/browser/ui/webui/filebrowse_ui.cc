// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/filebrowse_ui.h"

#include <algorithm>
#include <vector>

#include "base/callback.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/memory/singleton.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop.h"
#include "base/path_service.h"
#include "base/string_piece.h"
#include "base/string_util.h"
#include "base/threading/thread.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/download/download_item.h"
#include "chrome/browser/download/download_manager.h"
#include "chrome/browser/download/download_util.h"
#include "chrome/browser/history/history_types.h"
#include "chrome/browser/metrics/user_metrics.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tabs/tab_strip_model.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/webui/favicon_source.h"
#include "chrome/browser/ui/webui/mediaplayer_ui.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/jstemplate_builder.h"
#include "chrome/common/net/url_fetcher.h"
#include "chrome/common/time_format.h"
#include "chrome/common/url_constants.h"
#include "content/browser/browser_thread.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "grit/browser_resources.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "net/base/escape.h"
#include "net/url_request/url_request_file_job.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/mount_library.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#endif

// Maximum number of search results to return in a given search. We should
// eventually remove this.
static const int kMaxSearchResults = 100;
static const char kPropertyPath[] = "path";
static const char kPropertyTitle[] = "title";
static const char kPropertyDirectory[] = "isDirectory";
static const char kMediaPath[] = "/media";
static const char kFilebrowseURLHash[] = "chrome://filebrowse#";
static const int kPopupLeft = 0;
static const int kPopupTop = 0;

class FileBrowseUIHTMLSource : public ChromeURLDataManager::DataSource {
 public:
  FileBrowseUIHTMLSource();

  // Called when the network layer has requested a resource underneath
  // the path we registered.
  virtual void StartDataRequest(const std::string& path,
                                bool is_incognito,
                                int request_id);
  virtual std::string GetMimeType(const std::string&) const {
    return "text/html";
  }

 private:
  ~FileBrowseUIHTMLSource() {}

  DISALLOW_COPY_AND_ASSIGN(FileBrowseUIHTMLSource);
};

class TaskProxy;

// The handler for Javascript messages related to the "filebrowse" view.
class FilebrowseHandler : public net::DirectoryLister::DirectoryListerDelegate,
                          public WebUIMessageHandler,
#if defined(OS_CHROMEOS)
                          public chromeos::MountLibrary::Observer,
#endif
                          public base::SupportsWeakPtr<FilebrowseHandler>,
                          public DownloadManager::Observer,
                          public DownloadItem::Observer {
 public:
  FilebrowseHandler();
  virtual ~FilebrowseHandler();

  // Init work after Attach.
  void Init();

  // DirectoryLister::DirectoryListerDelegate methods:
  virtual void OnListFile(
      const net::DirectoryLister::DirectoryListerData& data);
  virtual void OnListDone(int error);

  // WebUIMessageHandler implementation.
  virtual WebUIMessageHandler* Attach(WebUI* web_ui);
  virtual void RegisterMessages();

#if defined(OS_CHROMEOS)
  // chromeos::MountLibrary::Observer interface
  virtual void DiskChanged(chromeos::MountLibraryEventType event,
                           const chromeos::MountLibrary::Disk* disk);
  virtual void DeviceChanged(chromeos::MountLibraryEventType event,
                             const std::string& device_path);
#endif

  // DownloadItem::Observer interface
  virtual void OnDownloadUpdated(DownloadItem* download);
  virtual void OnDownloadFileCompleted(DownloadItem* download);
  virtual void OnDownloadOpened(DownloadItem* download) { }

  // DownloadManager::Observer interface
  virtual void ModelChanged();

  // Callback for the "getRoots" message.
  void HandleGetRoots(const ListValue* args);

  void GetChildrenForPath(const FilePath& path, bool is_refresh);

  // Callback for the "getChildren" message.
  void HandleGetChildren(const ListValue* args);
  // Callback for the "refreshDirectory" message.
  void HandleRefreshDirectory(const ListValue* args);
  void HandleIsAdvancedEnabled(const ListValue* args);

  // Callback for the "getMetadata" message.
  void HandleGetMetadata(const ListValue* args);

  // Callback for the "openNewWindow" message.
  void OpenNewFullWindow(const ListValue* args);
  void OpenNewPopupWindow(const ListValue* args);

  // Callback for the "getDownloads" message.
  void HandleGetDownloads(const ListValue* args);

  void HandleCreateNewFolder(const ListValue* args);

  void PlayMediaFile(const ListValue* args);
  void EnqueueMediaFile(const ListValue* args);

  void HandleDeleteFile(const ListValue* args);
  void HandleCopyFile(const ListValue* value);
  void CopyFile(const FilePath& src, const FilePath& dest, TaskProxy* task);
  void DeleteFile(const FilePath& path, TaskProxy* task);
  void FireDeleteComplete(const FilePath& path);
  void FireCopyComplete(const FilePath& src, const FilePath& dest);

  void HandlePauseToggleDownload(const ListValue* args);

  void HandleCancelDownload(const ListValue* args);
  void HandleAllowDownload(const ListValue* args);

  void CreateNewFolder(const FilePath& path) const;

  // Callback for the "validateSavePath" message.
  void HandleValidateSavePath(const ListValue* args);

  // Validate a save path on file thread.
  void ValidateSavePathOnFileThread(const FilePath& save_path, TaskProxy* task);

  // Fire save path validation result to JS onValidatedSavePath.
  void FireOnValidatedSavePathOnUIThread(bool valid, const FilePath& save_path);

 private:

  // Retrieves downloads from the DownloadManager and updates the page.
  void UpdateDownloadList();

  void OpenNewWindow(const ListValue* args, bool popup);

  // Clear all download items and their observers.
  void ClearDownloadItems();

  // Send the current list of downloads to the page.
  void SendCurrentDownloads();

  void SendNewDownload(DownloadItem* download);

  bool ValidateSaveDir(const FilePath& save_dir, bool exists) const;
  bool AccessDisabled(const FilePath& path) const;

  scoped_ptr<ListValue> filelist_value_;
  FilePath currentpath_;
  Profile* profile_;
  TabContents* tab_contents_;
  std::string current_file_contents_;
  TaskProxy* current_task_;
  scoped_refptr<net::DirectoryLister> lister_;
  bool is_refresh_;

  DownloadManager* download_manager_;
  typedef std::vector<DownloadItem*> DownloadList;
  DownloadList active_download_items_;
  DownloadList download_items_;
  bool got_first_download_list_;
  DISALLOW_COPY_AND_ASSIGN(FilebrowseHandler);
};

class TaskProxy : public base::RefCountedThreadSafe<TaskProxy> {
 public:
  TaskProxy(const base::WeakPtr<FilebrowseHandler>& handler,
            const FilePath& path, const FilePath& dest)
      : handler_(handler),
        src_(path),
        dest_(dest) {}
  TaskProxy(const base::WeakPtr<FilebrowseHandler>& handler,
            const FilePath& path)
      : handler_(handler),
        src_(path) {}

  // TaskProxy is created on the UI thread, so in some cases,
  // we need to post back to the UI thread for destruction.
  void DeleteOnUIThread() {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        NewRunnableMethod(this, &TaskProxy::DoNothing));
  }

  void DoNothing() {}

  void DeleteFileProxy() {
    if (handler_)
      handler_->DeleteFile(src_, this);
  }

  void CopyFileProxy() {
    if (handler_)
      handler_->CopyFile(src_, dest_, this);
  }

  void CreateNewFolderProxy() {
    if (handler_)
      handler_->CreateNewFolder(src_);
    DeleteOnUIThread();
  }

  void FireDeleteCompleteProxy() {
    if (handler_)
      handler_->FireDeleteComplete(src_);
  }
  void FireCopyCompleteProxy() {
    if (handler_)
      handler_->FireCopyComplete(src_, dest_);
  }

  void ValidateSavePathOnFileThread() {
    if (handler_)
      handler_->ValidateSavePathOnFileThread(src_, this);
  }

  void FireOnValidatedSavePathOnUIThread(bool valid) {
    if (handler_)
      handler_->FireOnValidatedSavePathOnUIThread(valid, src_);
  }

 private:
  base::WeakPtr<FilebrowseHandler> handler_;
  FilePath src_;
  FilePath dest_;
  friend class base::RefCountedThreadSafe<TaskProxy>;
  DISALLOW_COPY_AND_ASSIGN(TaskProxy);
};


////////////////////////////////////////////////////////////////////////////////
//
// FileBrowseHTMLSource
//
////////////////////////////////////////////////////////////////////////////////

FileBrowseUIHTMLSource::FileBrowseUIHTMLSource()
    : DataSource(chrome::kChromeUIFileBrowseHost, MessageLoop::current()) {
}

void FileBrowseUIHTMLSource::StartDataRequest(const std::string& path,
                                              bool is_incognito,
                                              int request_id) {
  DictionaryValue localized_strings;
  // TODO(dhg): Add stirings to localized strings, also add more strings
  // that are currently hardcoded.
  localized_strings.SetString("title",
      l10n_util::GetStringUTF16(IDS_FILEBROWSER_TITLE));
  localized_strings.SetString("pause",
      l10n_util::GetStringUTF16(IDS_FILEBROWSER_PAUSE));
  localized_strings.SetString("resume",
      l10n_util::GetStringUTF16(IDS_FILEBROWSER_RESUME));
  localized_strings.SetString("scanning",
      l10n_util::GetStringUTF16(IDS_FILEBROWSER_SCANNING));
  localized_strings.SetString("confirmdelete",
      l10n_util::GetStringUTF16(IDS_FILEBROWSER_CONFIRM_DELETE));
  localized_strings.SetString("confirmyes",
      l10n_util::GetStringUTF16(IDS_FILEBROWSER_CONFIRM_YES));
  localized_strings.SetString("confirmcancel",
      l10n_util::GetStringUTF16(IDS_FILEBROWSER_CONFIRM_CANCEL));
  localized_strings.SetString("allowdownload",
      l10n_util::GetStringUTF16(IDS_FILEBROWSER_CONFIRM_DOWNLOAD));
  localized_strings.SetString("filenameprompt",
      l10n_util::GetStringUTF16(IDS_FILEBROWSER_PROMPT_FILENAME));
  localized_strings.SetString("save",
      l10n_util::GetStringUTF16(IDS_FILEBROWSER_SAVE));
  localized_strings.SetString("newfolder",
      l10n_util::GetStringUTF16(IDS_FILEBROWSER_NEW_FOLDER));
  localized_strings.SetString("open",
      l10n_util::GetStringUTF16(IDS_FILEBROWSER_OPEN));
  localized_strings.SetString("delete",
      l10n_util::GetStringUTF16(IDS_FILEBROWSER_DELETE));
  localized_strings.SetString("enqueue",
      l10n_util::GetStringUTF16(IDS_FILEBROWSER_ENQUEUE));
  localized_strings.SetString("mediapath", kMediaPath);
  FilePath default_download_path;
  if (!PathService::Get(chrome::DIR_DEFAULT_DOWNLOADS,
                        &default_download_path)) {
    NOTREACHED();
  }
  // TODO(viettrungluu): this is wrong -- FilePath's need not be Unicode.
  localized_strings.SetString("downloadpath", default_download_path.value());
  localized_strings.SetString("error_unknown_file_type",
      l10n_util::GetStringUTF16(IDS_FILEBROWSER_ERROR_UNKNOWN_FILE_TYPE));
  SetFontAndTextDirection(&localized_strings);

  static const base::StringPiece filebrowse_html(
      ResourceBundle::GetSharedInstance().GetRawDataResource(
          IDR_FILEBROWSE_HTML));
  const std::string full_html = jstemplate_builder::GetI18nTemplateHtml(
      filebrowse_html, &localized_strings);

  scoped_refptr<RefCountedBytes> html_bytes(new RefCountedBytes);
  html_bytes->data.resize(full_html.size());
  std::copy(full_html.begin(), full_html.end(), html_bytes->data.begin());

  SendResponse(request_id, html_bytes);
}

////////////////////////////////////////////////////////////////////////////////
//
// FilebrowseHandler
//
////////////////////////////////////////////////////////////////////////////////
FilebrowseHandler::FilebrowseHandler()
    : profile_(NULL),
      tab_contents_(NULL),
      is_refresh_(false),
      download_manager_(NULL),
      got_first_download_list_(false) {
  lister_ = NULL;
#if defined(OS_CHROMEOS)
  chromeos::MountLibrary* lib =
      chromeos::CrosLibrary::Get()->GetMountLibrary();
  lib->AddObserver(this);
#endif
}

FilebrowseHandler::~FilebrowseHandler() {
#if defined(OS_CHROMEOS)
  chromeos::MountLibrary* lib =
      chromeos::CrosLibrary::Get()->GetMountLibrary();
  lib->RemoveObserver(this);
#endif
  if (lister_.get()) {
    lister_->Cancel();
    lister_->set_delegate(NULL);
  }

  ClearDownloadItems();
  download_manager_->RemoveObserver(this);
}

WebUIMessageHandler* FilebrowseHandler::Attach(WebUI* web_ui) {
  // Create our favicon data source.
  profile_ = web_ui->GetProfile();
  profile_->GetChromeURLDataManager()->AddDataSource(
      new FaviconSource(profile_));
  tab_contents_ = web_ui->tab_contents();
  return WebUIMessageHandler::Attach(web_ui);
}

void FilebrowseHandler::Init() {
  download_manager_ = profile_->GetDownloadManager();
  download_manager_->AddObserver(this);
}

void FilebrowseHandler::RegisterMessages() {
  web_ui_->RegisterMessageCallback("getRoots",
      NewCallback(this, &FilebrowseHandler::HandleGetRoots));
  web_ui_->RegisterMessageCallback("getChildren",
      NewCallback(this, &FilebrowseHandler::HandleGetChildren));
  web_ui_->RegisterMessageCallback("getMetadata",
      NewCallback(this, &FilebrowseHandler::HandleGetMetadata));
  web_ui_->RegisterMessageCallback("openNewPopupWindow",
      NewCallback(this, &FilebrowseHandler::OpenNewPopupWindow));
  web_ui_->RegisterMessageCallback("openNewFullWindow",
      NewCallback(this, &FilebrowseHandler::OpenNewFullWindow));
  web_ui_->RegisterMessageCallback("getDownloads",
      NewCallback(this, &FilebrowseHandler::HandleGetDownloads));
  web_ui_->RegisterMessageCallback("createNewFolder",
      NewCallback(this, &FilebrowseHandler::HandleCreateNewFolder));
  web_ui_->RegisterMessageCallback("playMediaFile",
      NewCallback(this, &FilebrowseHandler::PlayMediaFile));
  web_ui_->RegisterMessageCallback("enqueueMediaFile",
      NewCallback(this, &FilebrowseHandler::EnqueueMediaFile));
  web_ui_->RegisterMessageCallback("pauseToggleDownload",
      NewCallback(this, &FilebrowseHandler::HandlePauseToggleDownload));
  web_ui_->RegisterMessageCallback("deleteFile",
      NewCallback(this, &FilebrowseHandler::HandleDeleteFile));
  web_ui_->RegisterMessageCallback("copyFile",
      NewCallback(this, &FilebrowseHandler::HandleCopyFile));
  web_ui_->RegisterMessageCallback("cancelDownload",
      NewCallback(this, &FilebrowseHandler::HandleCancelDownload));
  web_ui_->RegisterMessageCallback("allowDownload",
      NewCallback(this, &FilebrowseHandler::HandleAllowDownload));
  web_ui_->RegisterMessageCallback("refreshDirectory",
      NewCallback(this, &FilebrowseHandler::HandleRefreshDirectory));
  web_ui_->RegisterMessageCallback("isAdvancedEnabled",
      NewCallback(this, &FilebrowseHandler::HandleIsAdvancedEnabled));
  web_ui_->RegisterMessageCallback("validateSavePath",
      NewCallback(this, &FilebrowseHandler::HandleValidateSavePath));
}


void FilebrowseHandler::FireDeleteComplete(const FilePath& path) {
  // We notify the UI by telling it to refresh its contents.
  FilePath dir_path = path.DirName();
  GetChildrenForPath(dir_path, true);
};

void FilebrowseHandler::FireCopyComplete(const FilePath& src,
                                         const FilePath& dest) {
  // Notify the UI somehow.
  FilePath dir_path = dest.DirName();
  GetChildrenForPath(dir_path, true);
};

#if defined(OS_CHROMEOS)
void FilebrowseHandler::DiskChanged(chromeos::MountLibraryEventType event,
                                    const chromeos::MountLibrary::Disk* disk) {
  if (event == chromeos::MOUNT_DISK_REMOVED ||
      event == chromeos::MOUNT_DISK_CHANGED) {
    web_ui_->CallJavascriptFunction("rootsChanged");
  }
}

void FilebrowseHandler::DeviceChanged(chromeos::MountLibraryEventType event,
                                      const std::string& device_path) {
}
#endif

void FilebrowseHandler::HandleGetRoots(const ListValue* args) {
  ListValue results_value;
  DictionaryValue info_value;
  // TODO(dhg): add other entries, make this more general
#if defined(OS_CHROMEOS)
  chromeos::MountLibrary* lib = chromeos::CrosLibrary::Get()->GetMountLibrary();
  for (chromeos::MountLibrary::DiskMap::const_iterator iter =
          lib->disks().begin();
       iter != lib->disks().end();
       ++iter) {
    const chromeos::MountLibrary::Disk* disk = iter->second;
    if (!disk->mount_path().empty()) {
      DictionaryValue* page_value = new DictionaryValue();
      page_value->SetString(kPropertyPath, disk->mount_path());
      FilePath currentpath(disk->mount_path());
      std::string filename;
      filename = currentpath.BaseName().value();
      page_value->SetString(kPropertyTitle, filename);
      page_value->SetBoolean(kPropertyDirectory, true);
      results_value.Append(page_value);
    }
  }
#else
  DictionaryValue* page_value = new DictionaryValue();
  page_value->SetString(kPropertyPath, "/media");
  page_value->SetString(kPropertyTitle, "Removeable");
  page_value->SetBoolean(kPropertyDirectory, true);
  results_value.Append(page_value);
#endif
  FilePath default_download_path;
  if (!PathService::Get(chrome::DIR_DEFAULT_DOWNLOADS,
                        &default_download_path)) {
    NOTREACHED();
  }

  DictionaryValue* download_value = new DictionaryValue();
  download_value->SetString(kPropertyPath, default_download_path.value());
  download_value->SetString(kPropertyTitle, "File Shelf");
  download_value->SetBoolean(kPropertyDirectory, true);

  results_value.Append(download_value);

  info_value.SetString("functionCall", "getRoots");
  info_value.SetString(kPropertyPath, "");
  web_ui_->CallJavascriptFunction("browseFileResult",
                                  info_value, results_value);
}

void FilebrowseHandler::HandleCreateNewFolder(const ListValue* args) {
#if defined(OS_CHROMEOS)
  std::string path = UTF16ToUTF8(ExtractStringValue(args));
  FilePath currentpath(path);

  scoped_refptr<TaskProxy> task = new TaskProxy(AsWeakPtr(), currentpath);
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      NewRunnableMethod(
          task.get(), &TaskProxy::CreateNewFolderProxy));
#endif
}

void FilebrowseHandler::CreateNewFolder(const FilePath& currentpath) const {
  if (!ValidateSaveDir(currentpath, false) ||
      !file_util::CreateDirectory(currentpath))
    LOG(ERROR) << "Unable to create directory " << currentpath.value();
}

void FilebrowseHandler::PlayMediaFile(const ListValue* args) {
#if defined(OS_CHROMEOS)
  std::string url = UTF16ToUTF8(ExtractStringValue(args));
  GURL gurl(url);

  Browser* browser = Browser::GetBrowserForController(
      &tab_contents_->controller(), NULL);
  MediaPlayer* mediaplayer = MediaPlayer::GetInstance();
  mediaplayer->ForcePlayMediaURL(gurl, browser);
#endif
}

void FilebrowseHandler::EnqueueMediaFile(const ListValue* args) {
#if defined(OS_CHROMEOS)
  std::string url = UTF16ToUTF8(ExtractStringValue(args));
  GURL gurl(url);

  Browser* browser = Browser::GetBrowserForController(
      &tab_contents_->controller(), NULL);
  MediaPlayer* mediaplayer = MediaPlayer::GetInstance();
  mediaplayer->EnqueueMediaURL(gurl, browser);
#endif
}

void FilebrowseHandler::HandleIsAdvancedEnabled(const ListValue* args) {
#if defined(OS_CHROMEOS)
  bool is_enabled = CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableAdvancedFileSystem);
  bool mp_enabled = CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableMediaPlayer);
  DictionaryValue info_value;
  info_value.SetBoolean("enabled", is_enabled);
  info_value.SetBoolean("mpEnabled", mp_enabled);
  web_ui_->CallJavascriptFunction("enabledResult",
                                  info_value);

#endif
}

void FilebrowseHandler::HandleRefreshDirectory(const ListValue* args) {
#if defined(OS_CHROMEOS)
  std::string path = UTF16ToUTF8(ExtractStringValue(args));
  FilePath currentpath(path);
  GetChildrenForPath(currentpath, true);
#endif
}

void FilebrowseHandler::HandlePauseToggleDownload(const ListValue* args) {
#if defined(OS_CHROMEOS)
  int id;
  ExtractIntegerValue(args, &id);
  if ((id - 1) >= static_cast<int>(active_download_items_.size())) {
    return;
  }
  DownloadItem* item = active_download_items_[id];
  item->TogglePause();
#endif
}

void FilebrowseHandler::HandleAllowDownload(const ListValue* args) {
#if defined(OS_CHROMEOS)
  int id;
  ExtractIntegerValue(args, &id);
  if ((id - 1) >= static_cast<int>(active_download_items_.size())) {
    return;
  }

  DownloadItem* item = active_download_items_[id];
  download_manager_->DangerousDownloadValidated(item);
#endif
}

void FilebrowseHandler::HandleCancelDownload(const ListValue* args) {
#if defined(OS_CHROMEOS)
  int id;
  ExtractIntegerValue(args, &id);
  if ((id - 1) >= static_cast<int>(active_download_items_.size())) {
    return;
  }
  DownloadItem* item = active_download_items_[id];
  FilePath path = item->full_path();
  item->Cancel(true);
  FilePath dir_path = path.DirName();
  item->Remove(true);
  GetChildrenForPath(dir_path, true);
#endif
}

void FilebrowseHandler::OpenNewFullWindow(const ListValue* args) {
  OpenNewWindow(args, false);
}

void FilebrowseHandler::OpenNewPopupWindow(const ListValue* args) {
  OpenNewWindow(args, true);
}

void FilebrowseHandler::OpenNewWindow(const ListValue* args, bool popup) {
  std::string url = UTF16ToUTF8(ExtractStringValue(args));
  Browser* browser = popup ?
      Browser::CreateForType(Browser::TYPE_APP_PANEL, profile_) :
      BrowserList::GetLastActive();
  browser::NavigateParams params(browser, GURL(url), PageTransition::LINK);
  params.disposition = NEW_FOREGROUND_TAB;
  browser::Navigate(&params);
  // TODO(beng): The following two calls should be automatic by Navigate().
  if (popup) {
    // TODO(dhg): Remove these from being hardcoded. Allow javascript
    // to specify.
    params.browser->window()->SetBounds(gfx::Rect(0, 0, 400, 300));
  }
  params.browser->window()->Show();
}


void FilebrowseHandler::GetChildrenForPath(const FilePath& path,
                                           bool is_refresh) {
  if (path.empty())
    return;

  filelist_value_.reset(new ListValue());
  currentpath_ = path;

  if (lister_.get()) {
    lister_->Cancel();
    lister_->set_delegate(NULL);
    lister_ = NULL;
  }

  is_refresh_ = is_refresh;

#if defined(OS_CHROMEOS)
  // Don't allow listing files in inaccessible dirs.
  if (AccessDisabled(path))
    return;
#endif

  FilePath default_download_path;
  if (!PathService::Get(chrome::DIR_DEFAULT_DOWNLOADS,
                        &default_download_path)) {
    NOTREACHED();
  }

  if (currentpath_ == default_download_path) {
    lister_ = new net::DirectoryLister(currentpath_,
                                       false,
                                       net::DirectoryLister::DATE,
                                       this);
  } else {
    lister_ = new net::DirectoryLister(currentpath_, this);
  }
  lister_->Start();
}

void FilebrowseHandler::HandleGetChildren(const ListValue* args) {
#if defined(OS_CHROMEOS)
  std::string path = UTF16ToUTF8(ExtractStringValue(args));
  FilePath currentpath(path);
  filelist_value_.reset(new ListValue());

  GetChildrenForPath(currentpath, false);
#endif
}

void FilebrowseHandler::OnListFile(
    const net::DirectoryLister::DirectoryListerData& data) {
#if defined(OS_WIN)
  if (data.info.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) {
    return;
  }
#elif defined(OS_POSIX)
  if (data.info.filename[0] == '.') {
    return;
  }

  // Suppress .crdownload files.
  static const char crdownload[] = (".crdownload");
  static const size_t crdownload_size = arraysize(crdownload);
  const std::string& filename = data.info.filename;
  if ((filename.size() > crdownload_size) &&
      (filename.rfind(crdownload) == (filename.size() - crdownload_size)))
    return;
#endif

  DictionaryValue* file_value = new DictionaryValue();

#if defined(OS_WIN)
  int64 size = (static_cast<int64>(data.info.nFileSizeHigh) << 32) |
      data.info.nFileSizeLow;
  file_value->SetString(kPropertyTitle, data.info.cFileName);
  file_value->SetString(kPropertyPath,
                        currentpath_.Append(data.info.cFileName).value());
  file_value->SetBoolean(kPropertyDirectory,
      (data.info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? true : false);
#elif defined(OS_POSIX)
  file_value->SetString(kPropertyTitle, data.info.filename);
  file_value->SetString(kPropertyPath,
                        currentpath_.Append(data.info.filename).value());
  file_value->SetBoolean(kPropertyDirectory, S_ISDIR(data.info.stat.st_mode));
#endif
  filelist_value_->Append(file_value);
}

void FilebrowseHandler::OnListDone(int error) {
  DictionaryValue info_value;
  if (is_refresh_) {
    info_value.SetString("functionCall", "refresh");
  } else {
    info_value.SetString("functionCall", "getChildren");
  }
  info_value.SetString(kPropertyPath, currentpath_.value());
  web_ui_->CallJavascriptFunction("browseFileResult",
                                  info_value, *(filelist_value_.get()));
}

void FilebrowseHandler::HandleGetMetadata(const ListValue* args) {
}

void FilebrowseHandler::HandleGetDownloads(const ListValue* args) {
  UpdateDownloadList();
}

void FilebrowseHandler::ModelChanged() {
  if (!currentpath_.empty())
    GetChildrenForPath(currentpath_, true);
  else
    UpdateDownloadList();
}

void FilebrowseHandler::UpdateDownloadList() {
  ClearDownloadItems();

  std::vector<DownloadItem*> downloads;
  download_manager_->GetAllDownloads(FilePath(), &downloads);

  std::vector<DownloadItem*> new_downloads;
  // Scan for any in progress downloads and add ourself to them as an observer.
  for (DownloadList::iterator it = downloads.begin();
       it != downloads.end(); ++it) {
    DownloadItem* download = *it;
    // We want to know what happens as the download progresses and be notified
    // when the user validates the dangerous download.
    if (download->IsInProgress() ||
        download->safety_state() == DownloadItem::DANGEROUS) {
      download->AddObserver(this);
      active_download_items_.push_back(download);
    }
    DownloadList::iterator item = find(download_items_.begin(),
                                       download_items_.end(),
                                       download);
    if (item == download_items_.end() && got_first_download_list_) {
      SendNewDownload(download);
    }
    new_downloads.push_back(download);
  }
  download_items_.swap(new_downloads);
  got_first_download_list_ = true;
  SendCurrentDownloads();
}

void FilebrowseHandler::SendNewDownload(DownloadItem* download) {
  ListValue results_value;
  results_value.Append(download_util::CreateDownloadItemValue(download, -1));
  web_ui_->CallJavascriptFunction("newDownload", results_value);
}

void FilebrowseHandler::DeleteFile(const FilePath& path, TaskProxy* task) {
  if (!file_util::Delete(path, true)) {
    LOG(ERROR) << "unable to delete directory";
  }
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      NewRunnableMethod(task, &TaskProxy::FireDeleteCompleteProxy));
}

void FilebrowseHandler::CopyFile(const FilePath& src,
                                 const FilePath& dest,
                                 TaskProxy* task) {
  if (file_util::DirectoryExists(src)) {
    if (!file_util::CopyDirectory(src, dest, true)) {
      LOG(ERROR) << "unable to copy directory:" << src.value();
    }
  } else {
    if (!file_util::CopyFile(src, dest)) {
      LOG(ERROR) << "unable to copy file" << src.value();
    }
  }
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      NewRunnableMethod(task, &TaskProxy::FireCopyCompleteProxy));
}

void FilebrowseHandler::HandleDeleteFile(const ListValue* args) {
#if defined(OS_CHROMEOS)
  std::string path = UTF16ToUTF8(ExtractStringValue(args));
  FilePath currentpath(path);

  // Don't allow file deletion in inaccessible dirs.
  if (AccessDisabled(currentpath))
    return;

  for (unsigned int x = 0; x < active_download_items_.size(); x++) {
    FilePath item = active_download_items_[x]->full_path();
    if (item == currentpath) {
      active_download_items_[x]->Cancel(true);
      active_download_items_[x]->Remove(true);
      FilePath dir_path = item.DirName();
      GetChildrenForPath(dir_path, true);
      return;
    }
  }
  scoped_refptr<TaskProxy> task = new TaskProxy(AsWeakPtr(), currentpath);
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      NewRunnableMethod(
          task.get(), &TaskProxy::DeleteFileProxy));
#endif
}

void FilebrowseHandler::HandleCopyFile(const ListValue* value) {
#if defined(OS_CHROMEOS)
  if (value && value->GetType() == Value::TYPE_LIST) {
    const ListValue* list_value = static_cast<const ListValue*>(value);
    std::string src;
    std::string dest;

    // Get path string.
    if (list_value->GetString(0, &src) &&
        list_value->GetString(1, &dest)) {
      FilePath SrcPath = FilePath(src);
      FilePath DestPath = FilePath(dest);

      // Don't allow file copy to inaccessible dirs.
      if (AccessDisabled(DestPath))
        return;

      scoped_refptr<TaskProxy> task = new TaskProxy(AsWeakPtr(),
                                                    SrcPath, DestPath);
      BrowserThread::PostTask(
          BrowserThread::FILE, FROM_HERE,
          NewRunnableMethod(
              task.get(), &TaskProxy::CopyFileProxy));
    } else {
      LOG(ERROR) << "Unable to get string";
      return;
    }
  }
#endif
}

void FilebrowseHandler::HandleValidateSavePath(const ListValue* args) {
  std::string string_path;
  if (!args || !args->GetString(0, &string_path)) {
    FireOnValidatedSavePathOnUIThread(false, FilePath());  // Invalid save path.
    return;
  }

  FilePath save_path(string_path);

#if defined(OS_CHROMEOS)
  scoped_refptr<TaskProxy> task = new TaskProxy(AsWeakPtr(), save_path);
  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
      NewRunnableMethod(task.get(), &TaskProxy::ValidateSavePathOnFileThread));
#else
  // No save path checking for non-ChromeOS platforms.
  FireOnValidatedSavePathOnUIThread(true, save_path);
#endif
}

void FilebrowseHandler::ValidateSavePathOnFileThread(
    const FilePath& save_path, TaskProxy* task) {
#if defined(OS_CHROMEOS)
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  const bool valid = ValidateSaveDir(save_path.DirName(), true);

  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
      NewRunnableMethod(task,
                        &TaskProxy::FireOnValidatedSavePathOnUIThread,
                        valid));
#endif
}

bool FilebrowseHandler::ValidateSaveDir(const FilePath& save_dir,
                                        bool exists) const {
#if defined(OS_CHROMEOS)
  FilePath default_download_path;
  if (!PathService::Get(chrome::DIR_DEFAULT_DOWNLOADS,
                        &default_download_path)) {
    NOTREACHED();
  }

  // Valid save dir must be inside default download dir.
  if (default_download_path == save_dir)
    return true;
  if (exists) {
      DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
      return file_util::ContainsPath(default_download_path, save_dir);
  } else {
    return default_download_path.IsParent(save_dir);
  }
#endif
  return false;
}

void FilebrowseHandler::FireOnValidatedSavePathOnUIThread(bool valid,
    const FilePath& save_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  FundamentalValue valid_value(valid);
  StringValue path_value(save_path.value());
  web_ui_->CallJavascriptFunction("onValidatedSavePath",
      valid_value, path_value);
}

void FilebrowseHandler::OnDownloadUpdated(DownloadItem* download) {
  DownloadList::iterator it = find(active_download_items_.begin(),
                                   active_download_items_.end(),
                                   download);
  if (it == active_download_items_.end())
    return;
  const int id = static_cast<int>(it - active_download_items_.begin());

  scoped_ptr<DictionaryValue> download_item(
      download_util::CreateDownloadItemValue(download, id));
  web_ui_->CallJavascriptFunction("downloadUpdated", *download_item.get());
}

void FilebrowseHandler::ClearDownloadItems() {
  for (DownloadList::iterator it = active_download_items_.begin();
      it != active_download_items_.end(); ++it) {
    (*it)->RemoveObserver(this);
  }
  active_download_items_.clear();
}

void FilebrowseHandler::SendCurrentDownloads() {
  ListValue results_value;
  for (DownloadList::iterator it = active_download_items_.begin();
      it != active_download_items_.end(); ++it) {
    int index = static_cast<int>(it - active_download_items_.begin());
    results_value.Append(download_util::CreateDownloadItemValue(*it, index));
  }

  web_ui_->CallJavascriptFunction("downloadsList", results_value);
}

void FilebrowseHandler::OnDownloadFileCompleted(DownloadItem* download) {
  GetChildrenForPath(currentpath_, true);
}

bool FilebrowseHandler::AccessDisabled(const FilePath& path) const {
  return !ValidateSaveDir(path, false) &&
        net::URLRequestFileJob::AccessDisabled(path);
}

////////////////////////////////////////////////////////////////////////////////
//
// FileBrowseUI
//
////////////////////////////////////////////////////////////////////////////////

FileBrowseUI::FileBrowseUI(TabContents* contents) : HtmlDialogUI(contents) {
  FilebrowseHandler* handler = new FilebrowseHandler();
  AddMessageHandler((handler)->Attach(this));
  handler->Init();
  FileBrowseUIHTMLSource* html_source = new FileBrowseUIHTMLSource();

  // Set up the chrome://filebrowse/ source.
  contents->profile()->GetChromeURLDataManager()->AddDataSource(html_source);
}

// static
Browser* FileBrowseUI::OpenPopup(Profile* profile,
                                 const std::string& hashArgument,
                                 int width,
                                 int height) {
  // Get existing pop up for given hashArgument.
  Browser* browser = GetPopupForPath(hashArgument, profile);

  // Create new browser if no matching pop up found.
  if (browser == NULL) {
    browser = Browser::CreateForType(Browser::TYPE_APP_PANEL, profile);
    std::string url;
    if (hashArgument.empty()) {
      url = chrome::kChromeUIFileBrowseURL;
    } else {
      url = kFilebrowseURLHash;
      url.append(hashArgument);
    }

    browser::NavigateParams params(browser, GURL(url), PageTransition::LINK);
    params.disposition = NEW_FOREGROUND_TAB;
    browser::Navigate(&params);
    // TODO(beng): The following two calls should be automatic by Navigate().
    params.browser->window()->SetBounds(gfx::Rect(kPopupLeft,
                                                  kPopupTop,
                                                  width,
                                                  height));

    params.browser->window()->Show();
  } else {
    browser->window()->Show();
  }

  return browser;
}

Browser* FileBrowseUI::GetPopupForPath(const std::string& path,
                                       Profile* profile) {
  std::string current_path = path;
  if (current_path.empty()) {
    bool is_enabled = CommandLine::ForCurrentProcess()->HasSwitch(
        switches::kEnableAdvancedFileSystem);
    if (!is_enabled) {
      FilePath default_download_path;
      if (!PathService::Get(chrome::DIR_DEFAULT_DOWNLOADS,
                            &default_download_path)) {
        NOTREACHED();
      }
      current_path = default_download_path.value();
    }
  }

  for (BrowserList::const_iterator it = BrowserList::begin();
       it != BrowserList::end(); ++it) {
    if (((*it)->type() == Browser::TYPE_APP_PANEL)) {
      TabContents* tab_contents = (*it)->GetSelectedTabContents();
      DCHECK(tab_contents);
      if (!tab_contents)
          continue;
      const GURL& url = tab_contents->GetURL();

      if (url.SchemeIs(chrome::kChromeUIScheme) &&
          url.host() == chrome::kChromeUIFileBrowseHost &&
          url.ref() == current_path &&
          (*it)->profile() == profile) {
        return (*it);
      }
    }
  }

  return NULL;
}

const int FileBrowseUI::kPopupWidth = 250;
const int FileBrowseUI::kPopupHeight = 300;
const int FileBrowseUI::kSmallPopupWidth = 250;
const int FileBrowseUI::kSmallPopupHeight = 50;
