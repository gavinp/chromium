// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Setting the src of an img to an empty string can crash the browser, so we
// use an empty 1x1 gif instead.

/**
 * FileManager constructor.
 *
 * FileManager objects encapsulate the functionality of the file selector
 * dialogs, as well as the full screen file manager application (though the
 * latter is not yet implemented).
 *
 * @param {HTMLElement} dialogDom The DOM node containing the prototypical
 *     dialog UI.
 * @param {DOMFileSystem} filesystem The HTML5 filesystem object representing
 *     the root filesystem for the new FileManager.
 */
function FileManager(dialogDom) {
  this.dialogDom_ = dialogDom;
  this.filesystem_ = null;
  this.params_ = location.search ?
                 JSON.parse(decodeURIComponent(location.search.substr(1))) :
                 {};

  this.listType_ = null;

  this.selection = null;

  this.butterTimer_ = null;
  this.currentButter_ = null;
  this.butterLastShowTime_ = 0;

  this.watchedDirectoryUrl_ = null;

  this.commands_ = {};

  this.thumbnailUrlCache_ = {};

  this.document_ = dialogDom.ownerDocument;
  this.dialogType_ = this.params_.type || FileManager.DialogType.FULL_PAGE;

  metrics.recordEnum('Create', this.dialogType_,
      [FileManager.DialogType.SELECT_FOLDER,
      FileManager.DialogType.SELECT_SAVEAS_FILE,
      FileManager.DialogType.SELECT_OPEN_FILE,
      FileManager.DialogType.SELECT_OPEN_MULTI_FILE,
      FileManager.DialogType.FULL_PAGE]);

  // TODO(dgozman): This will be changed to LocaleInfo.
  this.locale_ = new v8Locale(navigator.language);

  this.initFileSystem_();
  this.initDom_();
  this.initDialogType_();
  this.dialogDom_.style.opacity = '1';
}

FileManager.prototype = {
  __proto__: cr.EventTarget.prototype
};

// Anonymous "namespace".
(function() {

  // Private variables and helper functions.

  /**
   * Location of the FAQ about the downloads directory.
   */
  const DOWNLOADS_FAQ_URL = 'http://www.google.com/support/chromeos/bin/' +
      'answer.py?hl=en&answer=1061547';

  /**
  * Location of the FAQ about the file actions.
  */
  const NO_ACTION_FOR_FILE_URL = 'http://support.google.com/chromeos/bin/' +
      'answer.py?hl=en&answer=1700055&topic=29026&ctx=topic';

  /**
   * Mnemonics for the recurse parameter of the copyFiles method.
   */
  const CP_RECURSE = true;
  const CP_NO_RECURSE = false;

  /**
   * Maximum amount of thumbnails in the preview pane.
   */
  const MAX_PREVIEW_THUMBAIL_COUNT = 4;

  /**
   * Maximum width or height of an image what pops up when the mouse hovers
   * thumbnail in the bottom panel (in pixels).
   */
  const IMAGE_HOVER_PREVIEW_SIZE = 200;

  /**
   * The minimum about of time to display the butter bar for, in ms.
   * Justification is 1000ms for minimum display time plus 300ms for transition
   * duration.
   */
  const MINIMUM_BUTTER_DISPLAY_TIME_MS = 1300;

  /**
   * Translated strings.
   */
  var localStrings;

  /**
   * Item for the Grid View.
   * @constructor
   */
  function GridItem(fileManager, entry) {
    var li = fileManager.document_.createElement('li');
    GridItem.decorate(li, fileManager, entry);
    return li;
  }

  GridItem.prototype = {
    __proto__: cr.ui.ListItem.prototype,

    get label() {
      return this.querySelector('filename-label').textContent;
    },
    set label(value) {
      // Grid sets it to entry. Ignore.
    }
  };

  GridItem.decorate = function(li, fileManager, entry) {
    li.__proto__ = GridItem.prototype;
    fileManager.decorateThumbnail_(li, entry);
  };


  /**
   * Return a translated string.
   *
   * Wrapper function to make dealing with translated strings more concise.
   * Equivilant to localStrings.getString(id).
   *
   * @param {string} id The id of the string to return.
   * @return {string} The translated string.
   */
  function str(id) {
    return localStrings.getString(id) || ('UNLOCALIZED STRING ' + id);
  }

  /**
   * Return a translated string with arguments replaced.
   *
   * Wrapper function to make dealing with translated strings more concise.
   * Equivilant to localStrings.getStringF(id, ...).
   *
   * @param {string} id The id of the string to return.
   * @param {...string} The values to replace into the string.
   * @return {string} The translated string with replaced values.
   */
  function strf(id, var_args) {
    return localStrings.getStringF.apply(localStrings, arguments);
  }

  /**
   * Checks if |parent_path| is parent file path of |child_path|.
   *
   * @param {string} parent_path The parent path.
   * @param {string} child_path The child path.
   */
  function isParentPath(parent_path, child_path) {
    if (!parent_path || parent_path.length == 0 ||
        !child_path || child_path.length == 0)
      return false;

    if (parent_path[parent_path.length -1] != '/')
      parent_path += '/';

    if (child_path[child_path.length -1] != '/')
      child_path += '/';

    return child_path.indexOf(parent_path) == 0;
  }

  /**
   * Returns parent folder path of file path.
   *
   * @param {string} path The file path.
   */
  function getParentPath(path) {
    var parent = path.replace(/[\/]?[^\/]+[\/]?$/,'');
    if (parent.length == 0)
      parent = '/';
    return parent;
  }

 /**
  * Normalizes path not to start with /
  *
  * @param {string} path The file path.
  */
  function normalizeAbsolutePath(x) {
    if (x[0] == '/')
      return x.slice(1);
    else
      return x;
  }


  /**
   * Call an asynchronous method on dirEntry, batching multiple callers.
   *
   * This batches multiple callers into a single invocation, calling all
   * interested parties back when the async call completes.
   *
   * The Entry method to be invoked should take two callbacks as parameters
   * (one for success and one for failure), and it should invoke those
   * callbacks with a single parameter representing the result of the call.
   * Example methods are Entry.getMetadata() and FileEntry.file().
   *
   * Warning: Because this method caches the first result, subsequent changes
   * to the entry will not be visible to callers.
   *
   * Error results are never cached.
   *
   * @param {DirectoryEntry} dirEntry The DirectoryEntry to apply the method
   *     to.
   * @param {string} methodName The name of the method to dispatch.
   * @param {function(*)} successCallback The function to invoke if the method
   *     succeeds.  The result of the method will be the one parameter to this
   *     callback.
   * @param {function(*)} opt_errorCallback The function to invoke if the
   *     method fails.  The result of the method will be the one parameter to
   *     this callback.  If not provided, the default errorCallback will throw
   *     an exception.
   */
  function batchAsyncCall(entry, methodName, successCallback,
                          opt_errorCallback) {
    var resultCache = methodName + '_resultCache_';

    if (entry[resultCache]) {
      // The result cache for this method already exists.  Just invoke the
      // successCallback with the result of the previuos call.
      // Callback via a setTimeout so the sync/async semantics don't change
      // based on whether or not the value is cached.
      setTimeout(function() { successCallback(entry[resultCache]) }, 0);
      return;
    }

    if (!opt_errorCallback) {
      opt_errorCallback = util.ferr('Error calling ' + methodName + ' for: ' +
                                    entry.fullPath);
    }

    var observerList = methodName + '_observers_';

    if (entry[observerList]) {
      // The observer list already exists, indicating we have a pending call
      // out to this method.  Add this caller to the list of observers and
      // bail out.
      entry[observerList].push([successCallback, opt_errorCallback]);
      return;
    }

    entry[observerList] = [[successCallback, opt_errorCallback]];

    function onComplete(success, result) {
      if (success)
        entry[resultCache] = result;

      for (var i = 0; i < entry[observerList].length; i++) {
        entry[observerList][i][success ? 0 : 1](result);
      }

      delete entry[observerList];
    };

    entry[methodName](function(rv) { onComplete(true, rv) },
                      function(rv) { onComplete(false, rv) });
  }

  /**
   * Invoke callback in sync/async manner.
   * @param {function(*)?} callback The callback. If null, nothing is called.
   * @param {boolean} sync True iff the callback should be called synchronously.
   * @param {*} callback_args... The rest are callback arguments.
   */
  function invokeCallback(callback, sync, callback_args) {
    if (!callback)
      return;
    var args = Array.prototype.slice.call(arguments, 2);
    if (sync) {
      callback.apply(null, args);
    } else {
      setTimeout(function() { callback.apply(null, args); }, 0);
    }
  }

  /**
   * Get the size and mtime of a file/directory, caching the result.
   *
   * When this method completes, the fileEntry object will get a
   * 'cachedSize_' and 'cachedMtime_' properties (if it doesn't already
   * have one) containing the last modified time of the entry as a Date object.
   *
   * @param {Entry} entry An HTML5 Entry object.
   * @param {function(Entry)} successCallback The function to invoke once the
   *     mtime is known.
   * @param {function=} opt_errorCallback Error callback.
   * @param {boolean=} opt_sync True, if callback should be called sync instead
   *     of async.
   */
  function cacheEntryDateAndSize(entry, successCallback, opt_errorCallback,
      opt_sync) {
    if ('cachedSize_' in entry) {
      invokeCallback(successCallback, !!opt_sync, entry);
      return;
    }

    function callback(success, metadata) {
      entry.cachedMtime_ = metadata.modificationTime;
      entry.cachedSize_ = (entry.isFile && metadata.size) || -1;
      if (success && successCallback) successCallback(entry);
      if (!success && opt_errorCallback) opt_errorCallback();
    }

    batchAsyncCall(entry, 'getMetadata',
        callback.bind(null, true),
        callback.bind(null, false, {}));
  }

  function cacheGDataProps(entry, successCallback,
                           opt_errorCallback, opt_sync) {
    if ('gdata_' in entry) {
      invokeCallback(successCallback, !!opt_sync, entry);
      return;
    }

    entry.getGDataFileProperties = entry.getGDataFileProperties ||
                                   function(callback) {
      var queue = cacheGDataProps.queue_;
      queue.callbacks.push(callback);
      queue.urls.push(entry.toURL());
      if (!queue.scheduled) {
        queue.scheduled = true;
        setTimeout(function() {
          queue.scheduled = false;
          var callbacks = queue.callbacks;
          var urls = queue.urls;
          chrome.fileBrowserPrivate.getGDataFileProperties(urls,
            function(props) {
              for (var i = 0; i < callbacks.length; i++) {
                callbacks[i](props[i]);
              }
            });
          queue.callbacks = [];
          queue.urls = [];
        }, 0);
      }
    };

    batchAsyncCall(entry, 'getGDataFileProperties', function(props) {
      entry.gdata_ = props;
      if (successCallback)
          successCallback(entry);
    }, opt_errorCallback);
  }

  cacheGDataProps.queue_ = {
    callbacks: [],
    urls: [],
    scheduled: false
  };

  function removeChildren(element) {
    element.textContent = '';
  }

  // Public statics.

  /**
   * List of dialog types.
   *
   * Keep this in sync with FileManagerDialog::GetDialogTypeAsString, except
   * FULL_PAGE which is specific to this code.
   *
   * @enum {string}
   */
  FileManager.DialogType = {
    SELECT_FOLDER: 'folder',
    SELECT_SAVEAS_FILE: 'saveas-file',
    SELECT_OPEN_FILE: 'open-file',
    SELECT_OPEN_MULTI_FILE: 'open-multi-file',
    FULL_PAGE: 'full-page'
  };

  FileManager.DialogType.isModal = function(type) {
    return type == FileManager.DialogType.SELECT_FOLDER ||
        type == FileManager.DialogType.SELECT_SAVEAS_FILE ||
        type == FileManager.DialogType.SELECT_OPEN_FILE ||
        type == FileManager.DialogType.SELECT_OPEN_MULTI_FILE;
  };

  FileManager.ListType = {
    DETAIL: 'detail',
    THUMBNAIL: 'thumb'
  };

  /**
   * Load translated strings.
   */
  FileManager.initStrings = function(callback) {
    chrome.fileBrowserPrivate.getStrings(function(strings) {
      localStrings = new LocalStrings(strings);
      if (callback)
        callback();
    });
  };

  // Instance methods.

  /**
   * Request local file system, resolve roots and init_ after that.
   * @private
   */
  FileManager.prototype.initFileSystem_ = function() {
    util.installFileErrorToString();
    // Replace the default unit in util to translated unit.
    util.units_ = [str('SIZE_B'),
                   str('SIZE_KB'),
                   str('SIZE_MB'),
                   str('SIZE_GB'),
                   str('SIZE_TB'),
                   str('SIZE_PB')];

    metrics.startInterval('Load.FileSystem');

    var self = this;

    // The list of active mount points to distinct them from other directories.
    chrome.fileBrowserPrivate.getMountPoints(function(mountPoints) {
      self.setMountPoints_(mountPoints);
      onDone();
    });

    function onDone() {
      if (self.mountPoints_ && self.filesystem_)
        self.init_();
    }

    chrome.fileBrowserPrivate.requestLocalFileSystem(function(filesystem) {
      metrics.recordInterval('Load.FileSystem');
      self.filesystem_ = filesystem;
      onDone();
    });
  };

  FileManager.prototype.setMountPoints_ = function(mountPoints) {
    this.mountPoints_ = mountPoints;
    // Add gdata mount info if present.
    if (this.gdataMounted_)
      this.mountPoints_.push(this.gdataMountInfo_);

    this.updateVolumeMetadata_();
  };

  /**
   * Continue initializing the file manager after resolving roots.
   */
  FileManager.prototype.init_ = function() {
    metrics.startInterval('Load.DOM');
    this.initCommands_();

    // TODO(rginda): 6/22/11: Remove this test when createDateTimeFormat is
    // available in all chrome trunk builds.
    if ('createDateTimeFormat' in this.locale_) {
      this.shortDateFormatter_ =
        this.locale_.createDateTimeFormat({'dateType': 'medium'});
    } else {
      this.shortDateFormatter_ = {
        format: function(d) {
          return (d.getMonth() + 1) + '/' + d.getDate() + '/' + d.getFullYear();
        }
      };
    }

    // TODO(rginda): 6/22/11: Remove this test when createCollator is
    // available in all chrome trunk builds.
    if ('createCollator' in this.locale_) {
      this.collator_ = this.locale_.createCollator({
        'numeric': true, 'ignoreCase': true, 'ignoreAccents': true});
    } else {
      this.collator_ = {
        compare: function(a, b) {
          if (a > b) return 1;
          if (a < b) return -1;
          return 0;
        }
      };
    }

    // Optional list of file types.
    this.fileTypes_ = this.params_.typeList;

    this.showCheckboxes_ =
        (this.dialogType_ == FileManager.DialogType.FULL_PAGE ||
         this.dialogType_ == FileManager.DialogType.SELECT_OPEN_MULTI_FILE);

    this.table_.startBatchUpdates();
    this.grid_.startBatchUpdates();

    this.initFileList_();
    this.initDialogs_();

    this.copyManager_ = new FileCopyManager();
    this.copyManager_.addEventListener('copy-progress',
                                       this.onCopyProgress_.bind(this));

    window.addEventListener('popstate', this.onPopState_.bind(this));
    window.addEventListener('unload', this.onUnload_.bind(this));

    var offlineHandler = this.onOnlineOffline_.bind(this);
    window.addEventListener('online', offlineHandler);
    window.addEventListener('offline', offlineHandler);
    offlineHandler();  // Sync with the current state.

    this.directoryModel_.addEventListener('directory-changed',
                                          this.onDirectoryChanged_.bind(this));
    var self = this;
    this.directoryModel_.addEventListener('begin-update-files', function() {
      self.currentList_.startBatchUpdates();
    });
    this.directoryModel_.addEventListener('end-update-files', function() {
      self.restoreItemBeingRenamed_();
      self.currentList_.endBatchUpdates();
    });
    this.directoryModel_.addEventListener('scan-started',
        this.showSpinnerLater_.bind(this));
    this.directoryModel_.addEventListener('scan-completed',
        this.showSpinner_.bind(this, false));
    this.addEventListener('selection-summarized',
                          this.onSelectionSummarized_.bind(this));

    // The list of archives requested to mount. We will show contents once
    // archive is mounted, but only for mounts from within this filebrowser tab.
    this.mountRequests_ = [];
    this.unmountRequests_ = [];
    chrome.fileBrowserPrivate.onMountCompleted.addListener(
        this.onMountCompleted_.bind(this));

    chrome.fileBrowserPrivate.onFileChanged.addListener(
        this.onFileChanged_.bind(this));

    // The list of callbacks to be invoked during the directory rescan after
    // all paste tasks are complete.
    this.pasteSuccessCallbacks_ = [];

    var path = this.getPathFromUrlOrParams_();
    if (path &&
        DirectoryModel.getRootType(path) == DirectoryModel.RootType.GDATA) {
      // We are opening on a GData path. Mount GData and show
      // "Loading Google Docs" message until the directory content loads.
      this.dialogContainer_.setAttribute('unmounted', true);
      this.initGData_(true /* dirChanged */);
      // This is a one-time handler (will be nulled out on the first call).
      this.setupCurrentDirectoryPostponed_ = function(event) {
        this.directoryModel_.removeEventListener('directory-changed',
           this.setupCurrentDirectoryPostponed_);
        this.setupCurrentDirectoryPostponed_ = null;
        if (event) // If called as an event handler just exit silently.
          return;
        this.setupCurrentDirectory_(false /* blankWhileOpeningAFile */);
      }.bind(this);
      this.directoryModel_.addEventListener('directory-changed',
          this.setupCurrentDirectoryPostponed_);
    } else {
      this.setupCurrentDirectory_(true /* blankWhileOpeningAFile */);
    }

    this.summarizeSelection_();

    var sortField =
        window.localStorage['sort-field-' + this.dialogType_] || 'cachedMtime_';
    var sortDirection =
        window.localStorage['sort-direction-' + this.dialogType_] || 'desc';
    this.directoryModel_.fileList.sort(sortField, sortDirection);

    this.refocus();

    this.metadataProvider_ =
        new MetadataProvider(this.filesystem_.root.toURL());

    // PyAuto tests monitor this state by polling this variable
    this.__defineGetter__('workerInitialized_', function() {
       return self.getMetadataProvider().isInitialized();
    });

    this.table_.endBatchUpdates();
    this.grid_.endBatchUpdates();

    metrics.recordInterval('Load.DOM');
    metrics.recordInterval('Load.Total');
  };

  /**
   * One-time initialization of commands.
   */
  FileManager.prototype.initCommands_ = function() {
    var commands = this.dialogDom_.querySelectorAll('command');
    for (var i = 0; i < commands.length; i++) {
      var command = commands[i];
      cr.ui.Command.decorate(command);
      this.commands_[command.id] = command;
    }

    this.fileContextMenu_ = this.dialogDom_.querySelector('.file-context-menu');
    cr.ui.Menu.decorate(this.fileContextMenu_);

    this.document_.addEventListener(
      'contextmenu', this.updateCommands_.bind(this), true);

    this.rootsContextMenu_ =
        this.dialogDom_.querySelector('.roots-context-menu');
    cr.ui.Menu.decorate(this.rootsContextMenu_);

    this.document_.addEventListener('command', this.onCommand_.bind(this));
  }

  /**
   * One-time initialization of dialogs.
   */
  FileManager.prototype.initDialogs_ = function() {
    var d = cr.ui.dialogs;
    d.BaseDialog.OK_LABEL = str('OK_LABEL');
    d.BaseDialog.CANCEL_LABEL = str('CANCEL_LABEL');
    this.alert = new d.AlertDialog(this.dialogDom_);
    this.confirm = new d.ConfirmDialog(this.dialogDom_);
    this.prompt = new d.PromptDialog(this.dialogDom_);
  };

  /**
   * One-time initialization of various DOM nodes.
   */
  FileManager.prototype.initDom_ = function() {
    // Cache nodes we'll be manipulating.
    this.previewThumbnails_ =
        this.dialogDom_.querySelector('.preview-thumbnails');
    this.previewPanel_ = this.dialogDom_.querySelector('.preview-panel');
    this.previewFilename_ = this.dialogDom_.querySelector('.preview-filename');
    this.previewSummary_ = this.dialogDom_.querySelector('.preview-summary');
    this.filenameInput_ = this.dialogDom_.querySelector('.filename-input');
    this.taskItems_ = this.dialogDom_.querySelector('.tasks');
    this.okButton_ = this.dialogDom_.querySelector('.ok');
    this.cancelButton_ = this.dialogDom_.querySelector('.cancel');
    this.deleteButton_ = this.dialogDom_.querySelector('.delete-button');
    this.table_ = this.dialogDom_.querySelector('.detail-table');
    this.grid_ = this.dialogDom_.querySelector('.thumbnail-grid');
    this.spinner_ = this.dialogDom_.querySelector('.spinner');
    this.showSpinner_(false);
    this.butter_ = this.dialogDom_.querySelector('.butter-bar');
    this.unmountedPanel_ = this.dialogDom_.querySelector('.unmounted-panel');

    cr.ui.Table.decorate(this.table_);
    cr.ui.Grid.decorate(this.grid_);

    this.downloadsWarning_ =
        this.dialogDom_.querySelector('.downloads-warning');
    var html = util.htmlUnescape(str('DOWNLOADS_DIRECTORY_WARNING'));
    this.downloadsWarning_.lastElementChild.innerHTML = html;
    var link = this.downloadsWarning_.querySelector('a');
    link.addEventListener('click', this.onDownloadsWarningClick_.bind(this));

    this.document_.addEventListener('keydown', this.onKeyDown_.bind(this));
    this.document_.addEventListener('copy', this.onCopy_.bind(this));
    this.document_.addEventListener('beforecopy',
                                    this.onBeforeCopy_.bind(this));
    // Disable the default browser context menu.
    this.document_.addEventListener('contextmenu',
                                    function (e) { e.preventDefault() });

    this.document_.addEventListener('paste', this.onPaste_.bind(this));
    // HACK(serya): 'paste'/'beforepaste' must be used.
    // But 'beforepaste' is apparently broken.
    var iframe = this.document_.querySelector('#command-dispatcher');
    this.commandProbbingDoc_ = iframe.contentDocument;
    this.commandProbbingDoc_.addEventListener('paste',
        this.onBeforePaste_.bind(this));
    this.document_.addEventListener('cut', this.onCut_.bind(this));
    this.document_.addEventListener('beforecut', this.onBeforeCut_.bind(this));

    this.renameInput_ = this.document_.createElement('input');
    this.renameInput_.className = 'rename';

    this.renameInput_.addEventListener(
        'keydown', this.onRenameInputKeyDown_.bind(this));
    this.renameInput_.addEventListener(
        'blur', this.onRenameInputBlur_.bind(this));

    this.filenameInput_.addEventListener(
        'keyup', this.onFilenameInputKeyUp_.bind(this));
    this.filenameInput_.addEventListener(
        'focus', this.onFilenameInputFocus_.bind(this));

    var listContainer = this.dialogDom_.querySelector('.list-container');
    listContainer.addEventListener('keydown', this.onListKeyDown_.bind(this));
    listContainer.addEventListener('keypress', this.onListKeyPress_.bind(this));
    this.okButton_.addEventListener('click', this.onOk_.bind(this));
    this.cancelButton_.addEventListener('click', this.onCancel_.bind(this));

    this.dialogDom_.querySelector('div.open-sidebar').addEventListener(
        'click', this.onToggleSidebar_.bind(this));
    this.dialogDom_.querySelector('div.open-sidebar').addEventListener(
        'keypress', this.onToggleSidebarPress_.bind(this));
    this.dialogDom_.querySelector('div.close-sidebar').addEventListener(
        'click', this.onToggleSidebar_.bind(this));
    this.dialogDom_.querySelector('div.close-sidebar').addEventListener(
        'keypress', this.onToggleSidebarPress_.bind(this));
    this.dialogContainer_ = this.dialogDom_.querySelector('.dialog-container');
    this.dialogDom_.querySelector('button.detail-view').addEventListener(
        'click', this.onDetailViewButtonClick_.bind(this));
    this.dialogDom_.querySelector('button.thumbnail-view').addEventListener(
        'click', this.onThumbnailViewButtonClick_.bind(this));

    cr.ui.ComboButton.decorate(this.taskItems_);
    this.taskItems_.addEventListener('select',
        this.onTaskItemClicked_.bind(this));

    this.dialogDom_.ownerDocument.defaultView.addEventListener(
        'resize', this.onResize_.bind(this));

    var ary = this.dialogDom_.querySelectorAll('[visibleif]');
    for (var i = 0; i < ary.length; i++) {
      var expr = ary[i].getAttribute('visibleif');
      if (!eval(expr))
        ary[i].style.display = 'none';
    }

    this.filePopup_ = null;

    // Populate the static localized strings.
    i18nTemplate.process(this.document_, localStrings.templateData);
  };

  /**
   * Constructs table and grid (heavy operation).
   **/
  FileManager.prototype.initFileList_ = function() {
    // Always sharing the data model between the detail/thumb views confuses
    // them.  Instead we maintain this bogus data model, and hook it up to the
    // view that is not in use.
    this.emptyDataModel_ = new cr.ui.ArrayDataModel([]);
    this.emptySelectionModel_ = new cr.ui.ListSelectionModel();

    var sigleSelection =
        this.dialogType_ == FileManager.DialogType.SELECT_OPEN_FILE ||
        this.dialogType_ == FileManager.DialogType.SELECT_FOLDER ||
        this.dialogType_ == FileManager.DialogType.SELECT_SAVEAS_FILE;

    this.directoryModel_ = new DirectoryModel(
        this.filesystem_.root,
        sigleSelection,
        str('ENABLE_GDATA') == '1');

    var dataModel = this.directoryModel_.fileList;
    var collator = this.collator_;
    dataModel.setCompareFunction('name', function(a, b) {
      return collator.compare(a.name, b.name);
    });
    dataModel.setCompareFunction('cachedMtime_',
                                 this.compareMtime_.bind(this));
    dataModel.setCompareFunction('cachedSize_',
                                 this.compareSize_.bind(this));
    dataModel.setCompareFunction('type',
                                 this.compareType_.bind(this));

    dataModel.addEventListener('splice',
                               this.onDataModelSplice_.bind(this));
    dataModel.addEventListener('permuted',
                               this.onDataModelPermuted_.bind(this));

    this.directoryModel_.fileListSelection.addEventListener(
        'change', this.onSelectionChanged_.bind(this));

    this.directoryModel_.autoSelectIndex =
        this.dialogType_ == FileManager.DialogType.SELECT_SAVEAS_FILE ? -1 : 0;

    // TODO(serya): temporary solution.
    this.directoryModel_.cacheEntryDateAndSize = cacheEntryDateAndSize;
    this.directoryModel_.cacheEntryFileType =
        this.cacheEntryFileType.bind(this);
    this.directoryModel_.cacheEntryIconType =
        this.cacheEntryIconType.bind(this);

    this.initTable_();
    this.initGrid_();
    this.initRootsList_();

    var listType = FileManager.ListType.DETAIL;
    if (FileManager.DialogType.isModal(this.dialogType_))
      listType = window.localStorage['listType-' + this.dialogType_] ||
          FileManager.ListType.DETAIL;
    this.setListType(listType);

    this.textSearchState_ = {text: '', date: new Date()};
  };

  FileManager.prototype.initRootsList_ = function() {
    this.rootsList_ = this.dialogDom_.querySelector('.roots-list');
    cr.ui.List.decorate(this.rootsList_);
    this.rootsList_.startBatchUpdates();

    var self = this;
    this.rootsList_.itemConstructor = function(entry) {
      return self.renderRoot_(entry);
    };

    this.rootsList_.selectionModel = this.directoryModel_.rootsListSelection;

    // TODO(dgozman): add "Add a drive" item.
    this.rootsList_.dataModel = this.directoryModel_.rootsList;
    this.directoryModel_.updateRoots(function() {
        self.rootsList_.endBatchUpdates();
    }, false);
  };

  /**
   * @param {boolean} dirChanged True if we just changed to GData directory,
   *                             False if "Retry" button clicked.
   */
  FileManager.prototype.initGData_ = function(dirChanged) {
    this.initGDataUnmountedPanel_();

    this.unmountedPanel_.removeAttribute('error');
    if (dirChanged) {
      // When changing to GData directory we want to see a clear panel.
      this.unmountedPanel_.removeAttribute('retry');
      if (this.gdataLoadingTimer_ ) {  // Show immediately if already loading.
        this.unmountedPanel_.setAttribute('loading', true);
      } else {
        this.unmountedPanel_.removeAttribute('loading');
        setTimeout(function() {
          if (this.gdataLoadingTimer_) {  // Still loading.
            this.unmountedPanel_.setAttribute('loading', true);
          }
        }.bind(this), 500);
      }
    } else {
      // When retrying we do not hide "Retry" and "Learn more".
      this.unmountedPanel_.setAttribute('loading', true);
    }

    // If the user changed to another directory and then back to GData we
    // re-enter this method while the timer is still active. In this case
    // we only update the UI but do not request the mount again.
    if (this.gdataLoadingTimer_)
      return;

    metrics.startInterval('Load.GData');
    chrome.fileBrowserPrivate.addMount('', 'gdata', {});

    // This timer could fire before the mount succeeds. We will silently
    // replace the error message with the correct directory contents.
    this.gdataLoadingTimer_ = setTimeout(function() {
          this.gdataLoadingTimer_ = null;
          this.onGDataUnreachable_('GData load timeout');
        }.bind(this),
        10 * 1000) ;
  };

  FileManager.prototype.clearGDataLoadingTimer_ = function(message) {
    if (this.gdataLoadingTimer_) {
      clearTimeout(this.gdataLoadingTimer_);
      this.gdataLoadingTimer_ = null;
    }
  };

  FileManager.prototype.onGDataUnreachable_ = function(message) {
    console.warn(message);
    if (this.isOnGData()) {
      this.unmountedPanel_.removeAttribute('loading');
      this.unmountedPanel_.setAttribute('error', true);
      this.unmountedPanel_.setAttribute('retry', true);
    }
  };

  FileManager.prototype.initGDataUnmountedPanel_ = function() {
    if (this.unmountedPanel_.firstElementChild)
      return;

    var loading = this.document_.createElement('div');
    loading.className = 'gdata loading';
    loading.textContent = strf('GDATA_LOADING', str('GDATA_PRODUCT_NAME'));
    this.unmountedPanel_.appendChild(loading);

    var error = this.document_.createElement('div');
    error.className = 'gdata error';
    error.textContent = strf('GDATA_CANNOT_REACH', str('GDATA_PRODUCT_NAME'));
    this.unmountedPanel_.appendChild(error);

    var retry = this.document_.createElement('button');
    retry.className = 'gdata retry';
    retry.textContent = str('GDATA_RETRY');
    retry.onclick = this.initGData_.bind(this, false /* retry */);
    this.unmountedPanel_.appendChild(retry);

    var learnMore = this.document_.createElement('div');
    learnMore.className = 'gdata learn-more';
    this.unmountedPanel_.appendChild(learnMore);

    var learnMoreLink = this.document_.createElement('a');
    learnMoreLink.textContent = str('GDATA_LEARN_MORE');
    learnMoreLink.href = 'javascript://';  // TODO: Set a proper link URL.
    learnMoreLink.className = 'gdata learn-more';
    learnMore.appendChild(learnMoreLink);
  };

  /**
   * Get the icon type for a given Entry.
   *
   * @param {Entry} entry An Entry subclass (FileEntry or DirectoryEntry).
   * @return {string}
   */
  FileManager.prototype.getIconType = function(entry) {
    if (!('cachedIconType_' in entry))
      entry.cachedIconType_ = this.computeIconType_(entry);
    return entry.cachedIconType_;
  };

  FileManager.prototype.computeIconType_ = function(entry) {
    if (entry.isDirectory)
      return 'folder';

    var fileType = FileType.getType(entry.name);
    return fileType.icon || fileType.type;
  };

  /**
   * Get the file type for a given Entry.
   *
   * @param {Entry} entry An Entry subclass (FileEntry or DirectoryEntry).
   * @return {string} One of the values from FileType.typeInfo, 'FOLDER',
   *                  'DEVICE' or undefined.
   */
  FileManager.prototype.getFileType = function(entry) {
    if (!('cachedFileType_' in entry))
      entry.cachedFileType_ = this.computeFileType_(entry);
    return entry.cachedFileType_;
  };

  FileManager.prototype.computeFileType_ = function(entry) {
    if (entry.isDirectory) {
      return {name: 'FOLDER', type: '.folder'};
    }

    return FileType.getType(entry.name);
  };

  /**
   * Get the icon type of a file, caching the result.
   *
   * When this method completes, the entry object will get a
   * 'cachedIconType_' property (if it doesn't already have one) containing the
   * icon type of the file as a string.
   *
   * @param {Entry} entry An HTML5 Entry object.
   * @param {function(Entry)} successCallback The function to invoke once the
   *     icon type is known.
   */
  FileManager.prototype.cacheEntryIconType = function(entry, successCallback) {
    this.getIconType(entry);
    if (successCallback)
      setTimeout(function() { successCallback(entry) }, 0);
  };

  FileManager.prototype.onDataModelSplice_ = function(event) {
    var checkbox = this.document_.querySelector('#select-all-checkbox');
    if (checkbox)
      this.updateSelectAllCheckboxState_(checkbox);
  };

  FileManager.prototype.onDataModelPermuted_ = function(event) {
    var sortStatus = this.directoryModel_.fileList.sortStatus;
    window.localStorage['sort-field-' + this.dialogType_] = sortStatus.field;
    window.localStorage['sort-direction-' + this.dialogType_] =
        sortStatus.direction;
  };

  /**
   * Get the file type of the entry, caching the result.
   *
   * When this method completes, the entry object will get a
   * 'cachedIconType_' property (if it doesn't already have one) containing the
   * icon type of the file as a string.
   *
   * @param {Entry} entry An HTML5 Entry object.
   * @param {function(Entry)} successCallback The function to invoke once the
   *     file size is known.
   * @param {boolean=} opt_sync If true, we are safe to do synchronous callback.
   */
  FileManager.prototype.cacheEntryFileType = function(
      entry, successCallback, opt_sync) {
    this.getFileType(entry);
    invokeCallback(successCallback, !!opt_sync, entry);
  };

  /**
   * Compare by mtime first, then by name.
   */
  FileManager.prototype.compareMtime_ = function(a, b) {
    if (a.cachedMtime_ > b.cachedMtime_)
      return 1;

    if (a.cachedMtime_ < b.cachedMtime_)
      return -1;

    return this.collator_.compare(a.name, b.name);
  };

  /**
   * Compare by size first, then by name.
   */
  FileManager.prototype.compareSize_ = function(a, b) {
    if (a.cachedSize_ > b.cachedSize_)
      return 1;

    if (a.cachedSize_ < b.cachedSize_)
      return -1;

    return this.collator_.compare(a.name, b.name);
  };

  /**
   * Compare by type first, then by subtype and then by name.
   */
  FileManager.prototype.compareType_ = function(a, b) {
    // Files of unknows type follows all the others.
    var result = this.collator_.compare(a.cachedFileType_.type || 'Z',
                                        b.cachedFileType_.type || 'Z');
    if (result != 0)
      return result;

    // If types are same both subtypes are defined of both are undefined.
    result = this.collator_.compare(a.cachedFileType_.subtype || '',
                                    b.cachedFileType_.subtype || '');
    if (result != 0)
      return result;

    return this.collator_.compare(a.name, b.name);
  };

  FileManager.prototype.refocus = function() {
    if (this.dialogType_ == FileManager.DialogType.SELECT_SAVEAS_FILE)
      this.filenameInput_.focus();
    else
      this.currentList_.focus();
  };

  FileManager.prototype.showButter = function(message, opt_options) {
    var butter = this.butter_;
    if (opt_options) {
      if ('actions' in opt_options) {
        var actions = butter.querySelector('.actions');
        while (actions.childNodes.length)
          actions.removeChild(actions.firstChild);
        for (var label in opt_options.actions) {
          var link = this.document_.createElement('a');
          link.setAttribute('href', 'javascript://' + label);
          link.addEventListener('click', function () {
              opt_options.actions[label]();
              return false;
          });
          actions.appendChild(link);
        }
        actions.classList.remove('hide-in-butter');
      }
      if ('progress' in opt_options) {
        butter.querySelector('.progress-bar')
            .classList.remove('hide-in-butter');
      }
    }

    var self = this;

    setTimeout(function () {
      self.currentButter_ = butter;
      self.updateButter(message, opt_options);
      self.butterLastShowTime_ = new Date();
    });

    return butter;
  };

  FileManager.prototype.showButterError = function(message, opt_options) {
    var butter = this.showButter(message, opt_options);
    butter.classList.add('error');
    return butter;
  };

  FileManager.prototype.updateButter = function(message, opt_options) {
    if (!opt_options)
      opt_options = {};

    var timeout;
    if ('timeout' in opt_options) {
      timeout = opt_options.timeout;
    } else {
      timeout = 10 * 1000;
    }

    if (this.butterTimer_)
      clearTimeout(this.butterTimer_);

    if (timeout) {
      var self = this;
      this.butterTimer_ = setTimeout(function() {
          self.hideButter();
          self.butterTimer_ = null;
      }, timeout);
    }

    var butter = this.currentButter_;
    butter.querySelector('.butter-message').textContent = message;
    if (message) {
      // The butter bar is made visible on the first non-empty message.
      butter.classList.remove('before-show');
    }
    if (opt_options && 'progress' in opt_options) {
      butter.querySelector('.progress-track').style.width =
          (opt_options.progress*100) + '%';
    }

    butter.style.left = ((this.dialogDom_.clientWidth -
                          butter.clientWidth) / 2) + 'px';
  };

  FileManager.prototype.hideButter = function() {
    if (this.currentButter_) {
      var delay = Math.max(MINIMUM_BUTTER_DISPLAY_TIME_MS -
          (new Date() - this.butterLastShowTime_), 0);

      var butter = this.currentButter_;

      setTimeout(function() {
        butter.classList.add('after-show');
      }, delay);

      setTimeout(function() {
          butter.classList.remove('error');
          butter.classList.remove('after-show');
          butter.classList.add('before-show');
          butter.querySelector('.actions').classList.add('hide-in-butter');
          butter.querySelector('.progress-bar').classList.add('hide-in-butter');
      }, delay + 1000);

      this.currentButter_ = null;
    }
  };

  /**
   * "Save a file" dialog is supposed to have a combo box with available
   * file types. Selecting an item filters files by extension and specifies how
   * file should be saved.
   * @return {intener} Index of selected type from this.fileTypes_ + 1. 0
   *                   means value is not specified.
   */
  FileManager.prototype.getSelectedFilterIndex_= function(fileName) {
    // TODO(serya): Implement the combo box
    // For now try to guess choice by file extension.
    if (!this.fileTypes_ || this.fileTypes_.length == 0) return 0;

    var extension = /\.[^\.]+$/.exec(fileName);
    extension = extension ? extension[0].substring(1).toLowerCase() : "";
    var result = 0;  // Use first type by default.
    for (var i = 0; i < this.fileTypes_.length; i++) {
      if (this.fileTypes_[i].extensions.indexOf(extension)) {
        result = i;
      }
    }
    return result + 1;  // 1-based index.
  };

  /**
   * Force the canExecute events to be dispatched.
   */
  FileManager.prototype.updateCommands_ = function() {
    for (var key in this.commands_)
      this.commands_[key].disabled = !this.canExecute_(key);
  };

  /**
   * @param {string} commandId Command identifier.
   * @return {boolean} True if the command can be executed for current
   *                   selection.
   */
  FileManager.prototype.canExecute_ = function(commandId) {
    var readonly = this.directoryModel_.readonly;
    var path = this.directoryModel_.currentEntry.fullPath;
    switch (commandId) {
      case 'copy':
      case 'cut':
        return this.document_.queryCommandEnabled(commandId);

      case 'paste':
        // HACK(serya): return this.document_.queryCommandEnabled('paste')
        // should be used.
        this.commandProbbingDoc_.execCommand('paste');
        return !this.commands_.paste.disabled;

      case 'rename':
        return (// Initialized to the point where we have a current directory
                !readonly &&
                // Rename not in progress.
                !this.isRenamingInProgress() &&
                // Only one file selected.
                this.selection &&
                this.selection.totalCount == 1);

      case 'delete':
        return (// Initialized to the point where we have a current directory
                !readonly &&
                // Rename not in progress.
                !this.isRenamingInProgress() &&
                this.selection &&
                this.selection.totalCount > 0);

      case 'newfolder':
        return !readonly &&
               (this.dialogType_ == FileManager.DialogType.SELECT_SAVEAS_FILE ||
                this.dialogType_ == FileManager.DialogType.FULL_PAGE);

      case 'unmount':
        return true;

      case 'format':
        var entry =
            this.getRootEntry_(this.rootsList_.selectionModel.selectedIndex);

        return entry && DirectoryModel.getRootType(entry.fullPath) ==
                        DirectoryModel.RootType.REMOVABLE;
    }
  };

  FileManager.prototype.getRootEntry_ = function(index) {
    if (index == -1)
      return null;

    return this.rootsList_.dataModel.item(index);
  };

  FileManager.prototype.updateCommonActionButtons_ = function() {
    if (this.deleteButton_)
      this.deleteButton_.disabled = !this.canExecute_('delete');
  };

  FileManager.prototype.setListType = function(type) {
    if (type && type == this.listType_)
      return;

    if (FileManager.DialogType.isModal(this.dialogType_))
      window.localStorage['listType-' + this.dialogType_] = type;

    this.table_.list.startBatchUpdates();
    this.grid_.startBatchUpdates();

    // TODO(dzvorygin): style.display and dataModel setting order shouldn't
    // cause any UI bugs. Currently, the only right way is first to set display
    // style and only then set dataModel.

    if (type == FileManager.ListType.DETAIL) {
      this.table_.dataModel = this.directoryModel_.fileList;
      this.table_.selectionModel = this.directoryModel_.fileListSelection;
      this.table_.style.display = '';
      this.grid_.style.display = 'none';
      this.grid_.selectionModel = this.emptySelectionModel_;
      this.grid_.dataModel = this.emptyDataModel_;
      this.table_.style.display = '';
      /** @type {cr.ui.List} */
      this.currentList_ = this.table_.list;
      this.dialogDom_.querySelector('button.detail-view').disabled = true;
      this.dialogDom_.querySelector('button.thumbnail-view').disabled = false;
    } else if (type == FileManager.ListType.THUMBNAIL) {
      this.grid_.dataModel = this.directoryModel_.fileList;
      this.grid_.selectionModel = this.directoryModel_.fileListSelection;
      this.grid_.style.display = '';
      this.table_.style.display = 'none';
      this.table_.selectionModel = this.emptySelectionModel_;
      this.table_.dataModel = this.emptyDataModel_;
      this.grid_.style.display = '';
      /** @type {cr.ui.List} */
      this.currentList_ = this.grid_;
      this.dialogDom_.querySelector('button.thumbnail-view').disabled = true;
      this.dialogDom_.querySelector('button.detail-view').disabled = false;
    } else {
      throw new Error('Unknown list type: ' + type);
    }

    this.listType_ = type;
    this.updateColumnModel_();
    this.onResize_();

    this.table_.list.endBatchUpdates();
    this.grid_.endBatchUpdates();
  };

  /**
   * Initialize the file thumbnail grid.
   */
  FileManager.prototype.initGrid_ = function() {
    var self = this;
    this.grid_.itemConstructor = GridItem.bind(null, this);

    this.grid_.addEventListener(
        'dblclick', this.onDetailDoubleClick_.bind(this));
    cr.ui.contextMenuHandler.setContextMenu(this.grid_, this.fileContextMenu_);
    this.grid_.addEventListener('mousedown',
                                this.onGridOrTableMouseDown_.bind(this));
    this.setupDragAndDrop_(this.grid_);
  };

  /**
   * Initialize the file list table.
   */
  FileManager.prototype.initTable_ = function() {
    var fullPage = (this.dialogType_ == FileManager.DialogType.FULL_PAGE);

    var columns = [
        new cr.ui.table.TableColumn('name', str('NAME_COLUMN_LABEL'),
                                    fullPage ? 64 : 324),
        new cr.ui.table.TableColumn('cachedSize_', str('SIZE_COLUMN_LABEL'),
                                    fullPage ? 15.5 : 92, true),
        new cr.ui.table.TableColumn('type', str('TYPE_COLUMN_LABEL'),
                                     fullPage ? 15.5 : 160),
        new cr.ui.table.TableColumn('cachedMtime_', str('DATE_COLUMN_LABEL'),
                                     fullPage ? 21 : 210)
    ];

    columns[0].renderFunction = this.renderName_.bind(this);
    columns[1].renderFunction = this.renderSize_.bind(this);
    columns[2].renderFunction = this.renderType_.bind(this);
    columns[3].renderFunction = this.renderDate_.bind(this);

    if (this.showCheckboxes_) {
      columns[0].headerRenderFunction =
          this.renderNameColumnHeader_.bind(this, columns[0].name);
    }

    this.regularColumnModel_ = new cr.ui.table.TableColumnModel(columns);

    if (fullPage) {
      columns.push(new cr.ui.table.TableColumn(
          'offline', str('OFFLINE_COLUMN_LABEL'), 21));
      columns[4].renderFunction = this.renderOffline_.bind(this);

      this.gdataColumnModel_ = new cr.ui.table.TableColumnModel(columns);
    } else {
      this.gdataColumnModel_ = null;
    }

    // Don't pay attention to double clicks on the table header.
    this.table_.querySelector('.list').addEventListener(
        'dblclick', this.onDetailDoubleClick_.bind(this));

    cr.ui.contextMenuHandler.setContextMenu(this.table_.querySelector('.list'),
        this.fileContextMenu_);

    this.table_.addEventListener('mousedown',
                                 this.onGridOrTableMouseDown_.bind(this));
    this.setupDragAndDrop_(this.table_.list);
  };

  FileManager.prototype.setupDragAndDrop_ = function(list) {
    list.addEventListener('dragstart', this.onDragStart_.bind(this));
    list.addEventListener('dragend', this.onDragEnd_.bind(this));
    list.addEventListener('dragover', this.onDragOver_.bind(this));
    list.addEventListener('drop', this.onDrop_.bind(this));
  };

  FileManager.prototype.onDragStart_ = function(event) {
    var dt = event.dataTransfer;
    var container = this.document_.querySelector('#drag-image-container');
    var length = this.selection.dragNodes.length;
    for (var i = 0; i < length; i++) {
      var listItem = this.selection.dragNodes[i];
      listItem.selected = true;
      listItem.style.zIndex = length - i;
      container.appendChild(listItem);
    }

    this.cutOrCopyToClipboard_(dt, false);

    dt.setDragImage(container, 0, 0);
    dt.effectAllowed = 'copyMove';
  };

  FileManager.prototype.onDragEnd_ = function(event) {
    var container = this.document_.querySelector('#drag-image-container');
    container.textContent = '';
  };

  FileManager.prototype.onDragOver_ = function(event) {
    if (this.canPasteOrDrop_(event.dataTransfer)) {
      event.preventDefault();
    }
  };

  FileManager.prototype.onDrop_ = function(event) {
    if (!this.canPasteOrDrop_(event.dataTransfer))
      return;
    event.preventDefault();
    this.pasteFromClipboard_(event.dataTransfer);
  };

  FileManager.prototype.initButter_ = function() {
    var self = this;
    var progress = this.copyManager_.getProgress();

    var options = {progress: progress.percentage, actions:{}};
    options.actions[str('CANCEL_LABEL')] = function cancelPaste() {
      self.copyManager_.requestCancel();
    };
    this.showButter(strf('PASTE_ITEMS_REMAINING', progress.pendingItems),
                    options);
  };

  FileManager.prototype.onCopyProgress_ = function(event) {
    var progress = this.copyManager_.getProgress();

    if (event.reason == 'BEGIN') {
      if (this.currentButter_)
        this.hideButter();

      clearTimeout(this.butterTimeout_);
      // If the copy process lasts more than 500 ms, we show a progress bar.
      this.butterTimeout_ = setTimeout(this.initButter_.bind(this), 500);
      return;
    }
    if (event.reason == 'PROGRESS') {
      // Perform this check inside Progress event handler, avoid to log error
      // message 'Unknown event reason: PROGRESS' in console.
      if (this.currentButter_) {
        var options = {progress: progress.percentage};
        this.updateButter(strf('PASTE_ITEMS_REMAINING', progress.pendingItems),
                          options);
      }
      return;
    }
    if (event.reason == 'SUCCESS') {
      clearTimeout(this.butterTimeout_);
      if (this.currentButter_)
        this.hideButter();

      self = this;
      var callback;
      while (callback = self.pasteSuccessCallbacks_.shift()) {
        try {
          callback();
        } catch (ex) {
          console.error('Caught exception while inovking callback: ' +
                        callback, ex);
        }
      }
    } else if (event.reason == 'ERROR') {
      clearTimeout(this.butterTimeout_);
      switch (event.error.reason) {
        case 'TARGET_EXISTS':
          var name = event.error.data.name;
          if (event.error.data.isDirectory)
            name += '/';
          this.showButterError(strf('PASTE_TARGET_EXISTS_ERROR', name));
          break;

        case 'FILESYSTEM_ERROR':
          this.showButterError(
              strf('PASTE_FILESYSTEM_ERROR',
                   util.getFileErrorMnemonic(event.error.data.code)));
          break;

        default:
          this.showButterError(strf('PASTE_UNEXPECTED_ERROR', event.error));
          break;
      }
    } else if (event.reason == 'CANCELLED') {
      this.showButter(str('PASTE_CANCELLED'));
    } else {
      console.log('Unknown event reason: ' + event.reason);
    }

    // TODO(benchan): Currently, there is no FileWatcher emulation for
    // GDataFileSystem, so we need to manually trigger the directory rescan
    // after paste operations complete. Remove this once we emulate file
    // watching functionalities in GDataFileSystem.
    if (this.isOnGData()) {
      if (event.reason == 'SUCCESS' || event.reason == 'ERROR' ||
          event.reason == 'CANCELLED') {
        this.directoryModel_.rescanLater();
      }
    }
  };

  FileManager.prototype.updateColumnModel_ = function() {
    if (this.listType_ != FileManager.ListType.DETAIL)
      return;
    this.table_.columnModel =
        (this.isOnGData() && this.gdataColumnModel_) ?
            this.gdataColumnModel_ :
            this.regularColumnModel_;
  };

  /**
   * Respond to a command being executed.
   */
  FileManager.prototype.onCommand_ = function(event) {
    switch (event.command.id) {
      case 'cut':
        document.execCommand('cut');
        return;

      case 'copy':
        document.execCommand('copy');
        return;

      case 'paste':
        document.execCommand('paste');
        return;

      case 'rename':
        this.initiateRename_();
        return;

      case 'delete':
        this.deleteEntries(this.selection.entries);
        return;

      case 'newfolder':
        this.onNewFolderCommand_(event);
        return;

      case 'unmount':
        var entry =
            this.getRootEntry_(this.rootsList_.selectionModel.selectedIndex);

        this.unmountVolume_(entry.toURL());
        return;

      case 'format':
        var entry =
            this.getRootEntry_(this.rootsList_.selectionModel.selectedIndex);

        this.confirm.show(str('FORMATTING_WARNING'), function() {
          chrome.fileBrowserPrivate.formatDevice(entry.toURL());
        });

        return;
    }
  };

  /**
   * Respond to the back and forward buttons.
   */
  FileManager.prototype.onPopState_ = function(event) {
    // TODO(serya): We should restore selected items here.
    this.closeFilePopup_();
    this.setupCurrentDirectory_();
  };

  FileManager.prototype.requestResize_ = function(timeout) {
    setTimeout(this.onResize_.bind(this), timeout || 0);
  };

  /**
   * Resize details and thumb views to fit the new window size.
   */
  FileManager.prototype.onResize_ = function() {
    this.table_.style.height = this.grid_.style.height =
      this.grid_.parentNode.clientHeight + 'px';
    this.table_.list_.style.height = (this.table_.clientHeight - 1 -
                                      this.table_.header_.clientHeight) + 'px';

    if (this.listType_ == FileManager.ListType.THUMBNAIL) {
      var g = this.grid_;
      g.startBatchUpdates();
      setTimeout(function() {
        g.columns = 0;
        g.redraw();
        g.endBatchUpdates();
      }, 0);
    } else {
      this.table_.redraw();
    }

    this.rootsList_.style.height =
        this.rootsList_.parentNode.clientHeight + 'px';
    this.rootsList_.redraw();
    this.truncateBreadcrumbs_();
  };

  FileManager.prototype.resolvePath = function(
      path, resultCallback, errorCallback) {
    return util.resolvePath(this.filesystem_.root, path, resultCallback,
                            errorCallback);
  };

  FileManager.prototype.getPathFromUrlOrParams_ = function() {
    return location.hash ?  // Location hash has the highest priority.
        decodeURI(location.hash.substr(1)) :
        this.params_.defaultPath;
  };

  /**
   * Restores current directory and may be a selected item after page load (or
   * reload) or popping a state (after click on back/forward). If location.hash
   * is present it means that the user has navigated somewhere and that place
   * will be restored. defaultPath primarily is used with save/open dialogs.
   * Default path may also contain a file name. Freshly opened file manager
   * window has neither.
   *
   * @param {boolean} blankWhileOpeningAFile
   */
  FileManager.prototype.setupCurrentDirectory_ =
      function(blankWhileOpeningAFile) {
    var path = this.getPathFromUrlOrParams_();

    if (!path) {
      this.directoryModel_.setupDefaultPath();
      return;
    }

    // In the FULL_PAGE mode if the hash path points to a file we might have
    // to invoke a task after selecting it.
    // If the file path is in params_ we only want to select the file.
    if (location.hash &&
        this.dialogType_ == FileManager.DialogType.FULL_PAGE) {
      // To prevent the file list flickering for a moment before the action
      // is executed we hide it under a white div.
      var shade;
      if (blankWhileOpeningAFile) {
        shade = this.document_.createElement('div');
        shade.className = 'overlay-pane';
        shade.style.backgroundColor = 'white';
        this.document_.body.appendChild(shade);
      }
      function removeShade() {
        if (shade)
          shade.parentNode.removeChild(shade);
      }

      // Keep track of whether the path is identified as an existing leaf
      // node.  Note that onResolve is guaranteed to be called (exactly once)
      // before onLoadedActivateLeaf.
      var foundLeaf = true;
      function onResolve(baseName, leafName, exists) {
        if (!exists || leafName == '') {
          // Non-existent file or a directory. Remove the shade immediately.
          removeShade();
          foundLeaf = false;
        }
      }

      // TODO(kaznacheev): refactor dispatchDefaultTask to accept an array
      // of urls instead of a selection. This will remove the need to wait
      // until the selection is done.
      var self = this;
      function onLoadedActivateLeaf() {
        if (foundLeaf) {
          // There are 3 ways we can get here:
          // 1. Invoked from file_manager_util::ViewFile. This can only
          //    happen for 'gallery' and 'mount-archive' actions.
          // 2. Reloading a Gallery page. Must be an image or a video file.
          // 3. A user manually entered a URL pointing to a file.
          if (FileType.isImageOrVideo(path)) {
            self.dispatchInternalTask_('gallery', self.selection.urls);
          } else if (FileType.getMediaType(path) == 'archive') {
            self.dispatchInternalTask_('mount-archive', self.selection.urls);
          } else {
            // Manually entered path, do nothing, remove the shade ASAP.
            removeShade();
            return;
          }
          setTimeout(removeShade, 1000);
        }
      }
      this.directoryModel_.setupPath(path, onLoadedActivateLeaf, onResolve);
      return;
    }

    if (this.dialogType_ == FileManager.DialogType.SELECT_SAVEAS_FILE) {
      this.directoryModel_.setupPath(path, undefined,
          function(basePath, leafName) {
            this.filenameInput_.value = leafName;
            this.selectDefaultPathInFilenameInput_();
          }.bind(this));
      return;
    }

    this.directoryModel_.setupPath(path);
  };

  /**
   * Tweak the UI to become a particular kind of dialog, as determined by the
   * dialog type parameter passed to the constructor.
   */
  FileManager.prototype.initDialogType_ = function() {
    var defaultTitle;
    var okLabel = str('OPEN_LABEL');

    switch (this.dialogType_) {
      case FileManager.DialogType.SELECT_FOLDER:
        defaultTitle = str('SELECT_FOLDER_TITLE');
        break;

      case FileManager.DialogType.SELECT_OPEN_FILE:
        defaultTitle = str('SELECT_OPEN_FILE_TITLE');
        break;

      case FileManager.DialogType.SELECT_OPEN_MULTI_FILE:
        defaultTitle = str('SELECT_OPEN_MULTI_FILE_TITLE');
        break;

      case FileManager.DialogType.SELECT_SAVEAS_FILE:
        defaultTitle = str('SELECT_SAVEAS_FILE_TITLE');
        okLabel = str('SAVE_LABEL');
        break;

      case FileManager.DialogType.FULL_PAGE:
        break;

      default:
        throw new Error('Unknown dialog type: ' + this.dialogType_);
    }

    this.okButton_.textContent = okLabel;

    dialogTitle = this.params_.title || defaultTitle;
    this.dialogDom_.querySelector('.dialog-title').textContent = dialogTitle;
  };

  /**
   * Render (and wire up) a checkbox to be used in either a detail or a
   * thumbnail list item.
   */
  FileManager.prototype.renderCheckbox_ = function(entry) {
    var input = this.document_.createElement('input');
    input.setAttribute('type', 'checkbox');
    input.setAttribute('tabindex', -1);
    input.className = 'file-checkbox common';
    input.addEventListener('mousedown',
                           this.onCheckboxMouseDownUp_.bind(this));
    input.addEventListener('mouseup',
                           this.onCheckboxMouseDownUp_.bind(this));
    input.addEventListener('click',
                           this.onCheckboxClick_.bind(this));

    if (this.selection && this.selection.entries.indexOf(entry) != -1) {
      // Our DOM nodes get discarded as soon as we're scrolled out of view,
      // so we have to make sure the check state is correct when we're brought
      // back to life.
      input.checked = true;
    }

    return input;
  };

  FileManager.prototype.renderNameColumnHeader_ = function(name, table) {
    var input = this.document_.createElement('input');
    input.setAttribute('type', 'checkbox');
    input.setAttribute('tabindex', -1);
    input.id = 'select-all-checkbox';
    input.className = 'common';
    this.updateSelectAllCheckboxState_(input);

    input.addEventListener('click', function(event) {
      if (input.checked)
        table.selectionModel.selectAll();
      else
        table.selectionModel.unselectAll();
      event.preventDefault();
      event.stopPropagation();
    });

    var fragment = this.document_.createDocumentFragment();
    fragment.appendChild(input);
    fragment.appendChild(this.document_.createTextNode(name));
    return fragment;
  };

  /**
   * Update check and disable states of the 'Select all' checkbox.
   */
  FileManager.prototype.updateSelectAllCheckboxState_ = function(checkbox) {
    var dm = this.directoryModel_.fileList;
    checkbox.checked = this.selection && dm.length > 0 &&
                       dm.length == this.selection.totalCount;
    checkbox.disabled = dm.length == 0;
  };

  /**
   * Update the thumbnail image to fit/fill the square container.
   *
   * Using webkit center packing does not align the image properly, so we need
   * to wait until the image loads and its proportions are known, then manually
   * position it at the center.
   *
   * @param {CSSStyleDeclaration} style Style object of the image.
   * @param {number} width Width of the image.
   * @param {number} height Height of the image.
   * @param {boolean} fill True: the image should fill the entire container,
   *                       false: the image should fully fit into the container.
   */
  FileManager.prototype.centerImage_ = function(style, width, height, fill) {
    function percent(fraction) {
      return Math.round(fraction * 100 * 10) / 10 + '%';  // Round to 0.1%
    }

    // First try vertical fit or horizontal fill.
    var fractionX = width / height;
    var fractionY = 1;
    if ((fractionX < 1) == !!fill) {  // Vertical fill or horizontal fit.
      fractionY = 1 / fractionX;
      fractionX = 1;
    }

    style.width = percent(fractionX);
    style.height = percent(fractionY);
    style.left = percent((1 - fractionX) / 2);
    style.top = percent((1 - fractionY) / 2);
  };

  FileManager.prototype.applyImageTransformation_ = function(box, transform) {
    if (transform) {
      box.style.webkitTransform =
          'scaleX(' + transform.scaleX + ') ' +
          'scaleY(' + transform.scaleY + ') ' +
          'rotate(' + transform.rotate90 * 90 + 'deg)';
    }
  };

  /**
   * Create a box containing a centered thumbnail image.
   *
   * @param {Entry} entry
   * @param {boolean} True if fill, false if fit.
   * @param {function(HTMLElement)} opt_imageLoadCallback Callback called when
   *                                the image has been loaded before inserting
   *                                it into the DOM.
   * @return {HTMLDivElement}
   */
  FileManager.prototype.renderThumbnailBox_ = function(entry, fill,
                                                       opt_imageLoadCallback) {
    var box = this.document_.createElement('div');
    box.className = 'img-container';
    var img = this.document_.createElement('img');
    var self = this;

    function onThumbnailURL(iconType, url, transform) {
      self.thumbnailUrlCache_[entry.fullPath] = {
        iconType: iconType,
        url: url,
        transform: transform
      };
      img.onload = function() {
        self.centerImage_(img.style, img.width, img.height, fill);
        if (opt_imageLoadCallback)
          opt_imageLoadCallback(img, transform);
        box.appendChild(img);
      };
      img.src = url;
      self.applyImageTransformation_(box, transform);
    }

   var cached = this.thumbnailUrlCache_[entry.fullPath];
    if (cached)
      onThumbnailURL(cached.iconType, cached.url, cached.transform);
    else
      this.getThumbnailURL(entry, onThumbnailURL);

    return box;
  };

  FileManager.prototype.decorateThumbnail_ = function(li, entry) {
    li.className = 'thumbnail-item';

    if (this.showCheckboxes_)
      li.appendChild(this.renderCheckbox_(entry));

    li.appendChild(this.renderThumbnailBox_(entry, false));
    var label = this.renderFileNameLabel_(entry);
    this.styleGDataItem_(entry, label);
    li.appendChild(label);
  };

  /**
   * Render the type column of the detail table.
   *
   * Invoked by cr.ui.Table when a file needs to be rendered.
   *
   * @param {Entry} entry The Entry object to render.
   * @param {string} columnId The id of the column to be rendered.
   * @param {cr.ui.Table} table The table doing the rendering.
   */
  FileManager.prototype.renderIconType_ = function(entry, columnId, table) {
    var icon = this.document_.createElement('div');
    icon.className = 'detail-icon';
    this.getIconType(entry);
    icon.setAttribute('iconType', entry.cachedIconType_);
    return icon;
  };

  /**
   * Return the localized name for the root.
   * @param {string} path The full path of the root (starting with slash).
   * @return {string} The localized name.
   */
  FileManager.prototype.getRootLabel_ = function(path) {
    if (path == '/' + DirectoryModel.DOWNLOADS_DIRECTORY)
      return str('DOWNLOADS_DIRECTORY_LABEL');

    if (path == '/' + DirectoryModel.ARCHIVE_DIRECTORY)
      return str('ARCHIVE_DIRECTORY_LABEL');
    if (isParentPath('/' + DirectoryModel.ARCHIVE_DIRECTORY, path))
      return path.substring(DirectoryModel.ARCHIVE_DIRECTORY.length + 2);

    if (path == '/' + DirectoryModel.REMOVABLE_DIRECTORY)
      return str('REMOVABLE_DIRECTORY_LABEL');
    if (isParentPath('/' + DirectoryModel.REMOVABLE_DIRECTORY, path))
      return path.substring(DirectoryModel.REMOVABLE_DIRECTORY.length + 2);

    if (path == '/' + DirectoryModel.GDATA_DIRECTORY)
      return str('GDATA_DIRECTORY_LABEL');

    return path;
  };

  FileManager.prototype.renderRoot_ = function(entry) {
    var li = this.document_.createElement('li');
    li.className = 'root-item';
    li.addEventListener('click', this.onRootClick_.bind(this, entry));

    var rootType = DirectoryModel.getRootType(entry.fullPath);

    var div = this.document_.createElement('div');
    div.className = 'root-label';

    var icon = rootType;
    var deviceNumber = this.getDeviceNumber(entry);

    if (deviceNumber != undefined) {
      var mountCondition = this.mountPoints_[deviceNumber].mountCondition;
      if (mountCondition == 'unknown_filesystem' ||
          mountCondition == 'unsupported_filesystem')
        icon = 'unreadable';
    }

    div.setAttribute('icon', icon);

    div.textContent = this.getRootLabel_(entry.fullPath);
    li.appendChild(div);

    if (rootType == DirectoryModel.RootType.ARCHIVE ||
        rootType == DirectoryModel.RootType.REMOVABLE) {
      var spacer = this.document_.createElement('div');
      spacer.className = 'spacer';
      li.appendChild(spacer);

      var eject = this.document_.createElement('div');
      eject.className = 'root-eject';
      eject.addEventListener('click', function(event) {
        event.stopPropagation();
        this.unmountVolume_(entry.toURL());
      }.bind(this));
      // Block other mouse handlers.
      eject.addEventListener('mouseup', function(e) { e.stopPropagation() });
      eject.addEventListener('mousedown', function(e) { e.stopPropagation() });
      li.appendChild(eject);

      cr.ui.contextMenuHandler.setContextMenu(li, this.rootsContextMenu_);
    }

    cr.defineProperty(li, 'lead', cr.PropertyKind.BOOL_ATTR);
    cr.defineProperty(li, 'selected', cr.PropertyKind.BOOL_ATTR);
    return li;
  };

  /**
   * Handler for root item being clicked.
   * @param {Entry} entry to navigate to.
   * @param {Event} event The event.
   */
  FileManager.prototype.onRootClick_ = function(entry, event) {
    var path = this.directoryModel_.currentEntry.fullPath;
    if (path.indexOf(entry.fullPath) == 0 && path != entry.fullPath) {
      this.directoryModel_.changeDirectory(entry.fullPath);
    }
  };

  /**
   * Unmounts device.
   * @param {string} url The url of removable storage to unmount.
   */
  FileManager.prototype.unmountVolume_ = function(url) {
    this.unmountRequests_.push(url);
    chrome.fileBrowserPrivate.removeMount(url);
  };

  FileManager.prototype.styleGDataItem_ = function(entry, element) {
    if (!this.isOnGData())
      return;

    cacheGDataProps(entry, function(entry) {
      if (!entry.gdata_)
        return;

      if (entry.gdata_.isHosted) {
        element.classList.add('gdata-hosted');
      }
      if (this.isAvaliableOffline_(entry.gdata_, this.getFileType(entry))) {
        element.classList.add('gdata-present');
      }
    }.bind(this));
  };

  /**
   * Render the Name column of the detail table.
   *
   * Invoked by cr.ui.Table when a file needs to be rendered.
   *
   * @param {Entry} entry The Entry object to render.
   * @param {string} columnId The id of the column to be rendered.
   * @param {cr.ui.Table} table The table doing the rendering.
   */
  FileManager.prototype.renderName_ = function(entry, columnId, table) {
    var label = this.document_.createElement('div');
    if (this.showCheckboxes_)
      label.appendChild(this.renderCheckbox_(entry));
    label.appendChild(this.renderIconType_(entry, columnId, table));
    label.entry = entry;
    label.className = 'detail-name';
    label.appendChild(this.renderFileNameLabel_(entry));

    this.styleGDataItem_(entry, label);
    return label;
  };

  /**
   * Render filename label for grid and list view.
   * @param {Entry} entry The Entry object to render.
   * @return {HTMLDivElement} The label.
   */
  FileManager.prototype.renderFileNameLabel_ = function(entry) {
    // Filename need to be in a '.filename-label' container for correct
    // work of inplace renaming.
    var fileName = this.document_.createElement('div');
    fileName.className = 'filename-label';

    fileName.textContent = this.directoryModel_.currentEntry.name == '' ?
        this.getRootLabel_(entry.name) : entry.name;
    return fileName;
  };

  /**
   * Render the Size column of the detail table.
   *
   * @param {Entry} entry The Entry object to render.
   * @param {string} columnId The id of the column to be rendered.
   * @param {cr.ui.Table} table The table doing the rendering.
   */
  FileManager.prototype.renderSize_ = function(entry, columnId, table) {
    var div = this.document_.createElement('div');
    div.className = 'size';
    this.updateSize_(div, entry);
    return div;
  };

  FileManager.prototype.updateSize_ = function(div, entry) {
    // Unlike other rtl languages, Herbew use MB and writes the unit to the
    // right of the number. We use css trick to workaround this.
    if (navigator.language == 'he')
      div.className = 'align-end-weakrtl';
    div.textContent = '...';
    var self = this;
    cacheEntryDateAndSize(entry, function(entry) {
      if (entry.cachedSize_ == -1) {
        div.textContent = '';
      } else if (entry.cachedSize_ == 0 &&
                 self.getFileType(entry).type == 'hosted') {
        div.textContent = '--';
      } else {
        div.textContent = util.bytesToSi(entry.cachedSize_);
      }
    }, null, true);
  };

  /**
   * Render the Type column of the detail table.
   *
   * @param {Entry} entry The Entry object to render.
   * @param {string} columnId The id of the column to be rendered.
   * @param {cr.ui.Table} table The table doing the rendering.
   */
  FileManager.prototype.renderType_ = function(entry, columnId, table) {
    var div = this.document_.createElement('div');
    div.className = 'type';
    this.updateType_(div, entry);
    this.styleGDataItem_(entry, div);
    return div;
  };

  FileManager.prototype.updateType_ = function(div, entry) {
    this.cacheEntryFileType(entry, function(entry) {
      var info = entry.cachedFileType_;
      if (info.name) {
        if (info.subtype)
          div.textContent = strf(info.name, info.subtype);
        else
          div.textContent = str(info.name);
      } else
        div.textContent = '';
    }, true);
  };

  /**
   * Render the Date column of the detail table.
   *
   * @param {Entry} entry The Entry object to render.
   * @param {string} columnId The id of the column to be rendered.
   * @param {cr.ui.Table} table The table doing the rendering.
   */
  FileManager.prototype.renderDate_ = function(entry, columnId, table) {
    var div = this.document_.createElement('div');
    div.className = 'date';
    this.updateDate_(div, entry);
    this.styleGDataItem_(entry, div);
    return div;
  };

  FileManager.prototype.updateDate_ = function(div, entry) {
    div.textContent = '...';

    var self = this;
    cacheEntryDateAndSize(entry, function(entry) {
      if (self.directoryModel_.isSystemDirectoy &&
          entry.cachedMtime_.getTime() == 0) {
        // Mount points for FAT volumes have this time associated with them.
        // We'd rather display nothing than this bogus date.
        div.textContent = '';
      } else {
        div.textContent = self.shortDateFormatter_.format(entry.cachedMtime_);
      }
    }, null, true);
  };

  FileManager.prototype.renderOffline_ = function(entry, columnId, table) {
    var doc = this.document_;
    var div = doc.createElement('div');
    div.className = 'offline';

    var checkbox = doc.createElement('input');
    checkbox.type = 'checkbox';
    checkbox.className = 'common pin';
    checkbox.tabIndex = -1;
    checkbox.addEventListener('click',
                              this.onPinClick_.bind(this, checkbox, entry));

    if (this.isOnGData()) {
      cacheGDataProps(entry, function(entry) {
        checkbox.checked = entry.gdata_.isPinned;
        div.appendChild(checkbox);
      });
    }
    return div;
  };

  /**
   * Restore the item which is being renamed while refreshing the file list. Do
   * nothing if no item is being renamed or such an item disappeared.
   *
   * While refreshing file list it gets repopulated with new file entries.
   * There is not a big difference wether DOM items stay the same or not.
   * Except for the item that the user is renaming.
   */
  FileManager.prototype.restoreItemBeingRenamed_ = function() {
    if (!this.isRenamingInProgress())
      return;

    var dm = this.directoryModel_;
    var leadIndex = dm.fileListSelection.leadIndex;
    if (leadIndex < 0)
      return;

    var leadEntry = dm.fileList.item(leadIndex);
    if (this.renameInput_.currentEntry.fullPath != leadEntry.fullPath)
      return;

    var leadListItem = this.findListItemForNode_(this.renameInput_);
    if (this.currentList_ == this.table_.list) {
      this.updateType_(leadListItem.querySelector('.type'), leadEntry);
      this.updateDate_(leadListItem.querySelector('.date'), leadEntry);
      this.updateSize_(leadListItem.querySelector('.size'), leadEntry);
    }
    this.currentList_.restoreLeadItem(leadListItem);
  };

  /**
   * Compute summary information about the current selection.
   *
   * This method dispatches the 'selection-summarized' event when it completes.
   * Depending on how many of the selected files already have known sizes, the
   * dispatch may happen immediately, or after a number of async calls complete.
   */
  FileManager.prototype.summarizeSelection_ = function() {
    var selection = this.selection = {
      entries: [],
      urls: [],
      totalCount: 0,
      fileCount: 0,
      directoryCount: 0,
      bytes: 0,
      iconType: null,
      indexes: this.currentList_.selectionModel.selectedIndexes,
      files: [],
      dragNodes: []
    };

    if (!selection.indexes.length) {
      this.updateCommonActionButtons_();
      this.updatePreviewPanelVisibility_();
      cr.dispatchSimpleEvent(this, 'selection-summarized');
      return;
    }

    this.previewSummary_.textContent = str('COMPUTING_SELECTION');
    // Removing childrens of task buttons and preview thumbnails after simple
    // event dispatched (see above). This can ensure a smooth disappearing
    // animation when nothing is selected.
    this.taskItems_.visible = false;
    this.taskItems_.clear();
    removeChildren(this.previewThumbnails_);

    var fileCount = 0;
    var byteCount = 0;
    var pendingFiles = [];
    var thumbnailCount = 0;

    for (var i = 0; i < selection.indexes.length; i++) {
      var entry = this.directoryModel_.fileList.item(selection.indexes[i]);
      if (!entry)
        continue;

      selection.entries.push(entry);
      selection.urls.push(entry.toURL());

      if (selection.iconType == null) {
        selection.iconType = this.getIconType(entry);
      } else if (selection.iconType != 'unknown') {
        var iconType = this.getIconType(entry);
        if (selection.iconType != iconType)
          selection.iconType = 'unknown';
      }

      if (thumbnailCount < MAX_PREVIEW_THUMBAIL_COUNT && entry.isFile) {
        var box = this.document_.createElement('div');
        var imageLoadCalback = thumbnailCount == 0 &&
                               this.initThumbnailZoom_.bind(this, box);
        var thumbnail = this.renderThumbnailBox_(entry, true, imageLoadCalback);
        box.appendChild(thumbnail);
        box.style.zIndex = MAX_PREVIEW_THUMBAIL_COUNT + 1 - i;
        box.addEventListener('click',
            this.dispatchDefaultTask_.bind(this, selection));

        this.previewThumbnails_.appendChild(box);
        thumbnailCount++;
      }

      // Items to drag are created in advance. Images must be loaded
      // at the time the 'dragstart' event comes. Otherwise draggable
      // image will be rendered without IMG tags.
      if (selection.dragNodes.length < MAX_PREVIEW_THUMBAIL_COUNT)
        selection.dragNodes.push(new GridItem(this, entry));

      selection.totalCount++;

      if (entry.isFile) {
        selection.fileCount += 1;
        if (!('cachedSize_' in entry)) {
          // Any file that hasn't been rendered may be missing its cachedSize_
          // property.  For example, visit a large file list, and press ctrl-a
          // to select all.  In this case, we need to asynchronously get the
          // sizes for these files before telling the world the selection has
          // been summarized.  See the 'computeNextFile' logic below.
          pendingFiles.push(entry);
          continue;
        } else {
          selection.bytes += entry.cachedSize_;
        }
        // File object must be prepeared in advance for clipboard operations
        // (copy, paste and drag). Clipboard object closes for write after
        // returning control from that handlers so they may not have
        // asynchronous operations.
        if (!this.isOnGData()) {
          entry.file(function(f) {
            selection.files.push(f);
          });
        }
      } else {
        selection.directoryCount += 1;
      }
    }

    // Now this.selection is complete. Update buttons.
    this.updateCommonActionButtons_();
    this.updatePreviewPanelVisibility_();

    var self = this;

    function cacheNextFile(fileEntry) {
      if (fileEntry) {
        // We're careful to modify the 'selection', rather than 'self.selection'
        // here, just in case the selection has changed since this summarization
        // began.
        selection.bytes += fileEntry.cachedSize_;
      }

      if (pendingFiles.length) {
        cacheEntryDateAndSize(pendingFiles.pop(), cacheNextFile);
      } else {
        self.dispatchEvent(new cr.Event('selection-summarized'));
      }
    }

    if (this.dialogType_ == FileManager.DialogType.FULL_PAGE) {
      this.taskItems_.clear();
      // Some internal tasks cannot be defined in terms of file patterns,
      // so we pass selection to check for them manually.
      if (selection.directoryCount == 0 && selection.fileCount > 0) {
        // Only files, not directories, are supported for external tasks.
        chrome.fileBrowserPrivate.getFileTasks(
            selection.urls,
            this.onTasksFound_.bind(this, selection));
      } else {
        // There may be internal tasks for directories.
        this.onTasksFound_(selection, []);
      }
    }

    cacheNextFile();
  };

  /**
   * Initialize a thumbnail in the bottom pannel to pop up on mouse over.
   * Image's assumed to be just loaded and not inserted into the DOM.
   *
   * @param {HTMLElement} box Element what's going to contain the image.
   * @param {HTMLElement} img Loaded image.
   */
  FileManager.prototype.initThumbnailZoom_ = function(box, img, transform) {
    var width = img.width;
    var height = img.height;
    const THUMBNAIL_SIZE = 45;

    if (width < THUMBNAIL_SIZE * 2 && height < THUMBNAIL_SIZE * 2)
      return;

    var scale = Math.min(1,
        IMAGE_HOVER_PREVIEW_SIZE / Math.max(width, height));

    var imageWidth = Math.round(width * scale);
    var imageHeight = Math.round(height * scale);

    var largeImage = this.document_.createElement('img');
    if (scale < 0.3) {
      // Scaling large images kills animation. Downscale it in advance.

      // Canvas scales images with liner interpolation. Make a larger
      // image (but small enough to not kill animation) and let IMG
      // scale it smoothly.
      var INTERMEDIATE_SCALE = 3;
      var canvas = this.document_.createElement('canvas');
      canvas.width = imageWidth * INTERMEDIATE_SCALE;
      canvas.height = imageHeight * INTERMEDIATE_SCALE;
      var ctx = canvas.getContext('2d');
      ctx.drawImage(img, 0, 0, canvas.width, canvas.height);
      // Using bigger than default compression reduces image size by
      // several times. Quality degradation compensated by greater resolution.
      largeImage.src = canvas.toDataURL('image/jpeg', 0.6);
    } else {
      largeImage.src = img.src;
    }
    var largeImageBox = this.document_.createElement('div');
    largeImageBox.className = 'popup';

    var boxWidth = Math.max(THUMBNAIL_SIZE, imageWidth);
    var boxHeight = Math.max(THUMBNAIL_SIZE, imageHeight);

    if (transform && transform.rotate90 % 2 == 1) {
      var t = boxWidth;
      boxWidth = boxHeight;
      boxHeight = t;
    }

    var style = largeImageBox.style;
    style.width = boxWidth + 'px';
    style.height = boxHeight + 'px';
    style.top = (-boxHeight + THUMBNAIL_SIZE) + 'px';

    var style = largeImage.style;
    style.width = imageWidth + 'px';
    style.height = imageHeight + 'px';
    style.left = (boxWidth - imageWidth) / 2 + 'px';
    style.top = (boxHeight - imageHeight) / 2 + 'px';
    style.position = 'relative';

    this.applyImageTransformation_(largeImage, transform);

    largeImageBox.appendChild(largeImage);
    box.insertBefore(largeImageBox, box.firstChild);
  };

  FileManager.prototype.updatePreviewPanelVisibility_ = function() {
    var panel = this.previewPanel_;
    var state = panel.getAttribute('visibility');
    var mustBeVisible = (this.selection.totalCount > 0);
    var self = this;

    switch (state) {
      case 'visible':
        if (!mustBeVisible)
          startHiding();
        break;

      case 'hiding':
        if (mustBeVisible)
          stopHidingAndShow();
        break;

      case 'hidden':
        if (mustBeVisible)
          show();
    }

    function stopHidingAndShow() {
      clearTimeout(self.hidingTimeout_);
      self.hidingTimeout_ = 0;
      setVisibility('visible');
    }

    function startHiding() {
      setVisibility('hiding');
      self.hidingTimeout_ = setTimeout(function() {
          self.hidingTimeout_ = 0;
          setVisibility('hidden');
          self.onResize_();
        }, 250);
    }

    function show() {
      setVisibility('visible');
      self.onResize_();
    }

    function setVisibility(visibility) {
      panel.setAttribute('visibility', visibility);
    }
  };


  FileManager.prototype.isOnGData = function() {
    return this.directoryModel_ &&
        this.directoryModel_.rootPath == '/' + DirectoryModel.GDATA_DIRECTORY;
  };

  FileManager.prototype.getMetadataProvider = function() {
    return this.metadataProvider_;
  };

  /**
   * Callback called when tasks for selected files are determined.
   * @param {Object} selection Selection is passed here, since this.selection
   *     can change before tasks were found, and we should be accurate.
   * @param {Array.<Task>} tasksList The tasks list.
   */
  FileManager.prototype.onTasksFound_ = function(selection, tasksList) {
    this.taskItems_.clear();

    var defaultTask = null;
    var tasksCount = 0;
    for (var i = 0; i < tasksList.length; i++) {
      var task = tasksList[i];

      // Tweak images, titles of internal tasks.
      var task_parts = task.taskId.split('|');
      if (task_parts[0] == this.getExtensionId_()) {
        if (task_parts[1] == 'play') {
          // TODO(serya): This hack needed until task.iconUrl is working
          //             (see GetFileTasksFileBrowserFunction::RunImpl).
          task.iconUrl =
              chrome.extension.getURL('images/icon_play_16x16.png');
          task.title = str('ACTION_LISTEN');
          this.playTask_ = task;
        } else if (task_parts[1] == 'mount-archive') {
          task.iconUrl =
              chrome.extension.getURL('images/icon_mount_archive_16x16.png');
          task.title = str('MOUNT_ARCHIVE');
        } else if (task_parts[1] == 'gallery') {
          task.iconUrl =
              chrome.extension.getURL('images/icon_preview_16x16.png');
          if (selection.urls.filter(FileType.isImage).length) {
            // Some images sected, use the generic "Open" title.
            task.title = str('ACTION_OPEN');
          } else {
            // The selection is all videos, use the specific "Watch" title.
            task.title = str('ACTION_WATCH');
          }
        } else if (task_parts[1] == 'open-hosted') {
          task.iconUrl =
          chrome.extension.getURL('images/icon_preview_16x16.png');
              task.title = str('ACTION_OPEN');
        } else if (task_parts[1] == 'view-pdf') {
          // Do not render this task if disabled.
          if (str('PDF_VIEW_ENABLED') == 'false') continue;
          task.iconUrl =
              chrome.extension.getURL('images/icon_preview_16x16.png');
          task.title = str('ACTION_VIEW');
        } else if (task_parts[1] == 'view-txt') {
          task.iconUrl =
              chrome.extension.getURL('images/icon_preview_16x16.png');
          task.title = str('ACTION_VIEW');
        } else if (task_parts[1] == 'install-crx') {
          // TODO(dgozman): change to the right icon.
          task.iconUrl =
              chrome.extension.getURL('images/icon_preview_16x16.png');
          task.title = str('INSTALL_CRX');
        }
      }
      this.renderTaskItem_(task);
      tasksCount++;
      if (defaultTask == null) defaultTask = task;
    }

    this.taskItems_.visible = tasksCount > 0;
    if (tasksCount > 1) {
      // Duplicate default task in drop-down list.
      this.renderTaskItem_(defaultTask);
    }

    selection.tasksList = tasksList;
    if (selection.dispatchDefault) {
      // We got a request to dispatch the default task for the selection.
      selection.dispatchDefault = false;
      this.dispatchDefaultTask_(selection);
    }
  };

  FileManager.prototype.renderTaskItem_ = function(task) {
    var item = this.document_.createElement('div');
    item.className = 'task-item';
    item.task = task;

    var img = this.document_.createElement('img');
    img.src = task.iconUrl;
    item.appendChild(img);

    var label = this.document_.createElement('div');
    label.appendChild(this.document_.createTextNode(task.title));
    item.appendChild(label);

    this.taskItems_.addItem(item);
  };

  FileManager.prototype.getExtensionId_ = function() {
    return chrome.extension.getURL('').split('/')[2];
  };

  FileManager.prototype.onDownloadsWarningClick_ = function(event) {
    chrome.tabs.create({url: DOWNLOADS_FAQ_URL});
    if (this.dialogType_ != FileManager.DialogType.FULL_PAGE) {
      this.onCancel_();
    }
  };

  FileManager.prototype.onTaskItemClicked_ = function(event) {
    this.dispatchFileTask_(event.item.task.taskId, this.selection.urls);
  };

  /**
   * Dispatches default task for the current selection. If tasks are not ready
   * yet, dispatches after task are available.
   * @param {Object=} opt_selection
   */
  FileManager.prototype.dispatchDefaultTask_ = function(opt_selection) {
    var selection = opt_selection || this.selection;

    if (selection.urls.length == 0)
      return;

    if (!selection.tasksList) {
      // Don't have tasks list yet - wait until get it.
      selection.dispatchDefault = true;
      return;
    }

    if (selection.tasksList.length > 0) {
      this.dispatchFileTask_(selection.tasksList[0].taskId, selection.urls);
      return;
    }

    if (selection.urls.length == 1) {
      // We don't have tasks, so try the default browser action.
      // We only do that for single selection to avoid confusion.

      var callback = function(success) {
        if (!success && selection.entries.length == 1)
          this.alert.showHtml(
              unescape(selection.entries[0].name),
              strf('NO_ACTION_FOR_FILE', NO_ACTION_FOR_FILE_URL),
              function() {});
      }.bind(this);

      this.executeIfAvailable_(selection.urls, function(urls) {
        chrome.fileBrowserPrivate.viewFiles(urls, 'default', callback);
      });
    }
  };

  FileManager.prototype.dispatchInternalTask_ = function(task, urls) {
     this.dispatchFileTask_(this.getExtensionId_() + '|' + task, urls);
  };

  FileManager.prototype.dispatchFileTask_ = function(taskId, urls) {
    this.executeIfAvailable_(urls, function(urls) {
      chrome.fileBrowserPrivate.executeTask(taskId, urls);
      var task_parts = taskId.split('|');
      if (task_parts[0] == this.getExtensionId_()) {
        // For internal tasks we do not listen to the event to avoid
        // handling the same task instance from multiple tabs.
        // So, we manually execute the task.
        this.onFileTaskExecute_(task_parts[1], urls);
      }
    }.bind(this));
  };

  FileManager.prototype.executeIfAvailable_ = function(urls, callback) {
    if (this.isOnGData() && this.isOffline()) {
      chrome.fileBrowserPrivate.getGDataFileProperties(urls, function(props) {
        for (var i = 0; i != props.length; i++) {
          if (!this.isAvaliableOffline_(props[i], FileType.getType(urls[i]))) {
            this.alert.showHtml(
                str('OFFLINE_HEADER'),
                strf(
                    urls.length == 1 ?
                        'OFFLINE_MESSAGE' :
                        'OFFLINE_MESSAGE_PLURAL',
                    str('OFFLINE_COLUMN_LABEL')));
            return;
          }
        }
        callback(urls);
      }.bind(this));
    } else {
      callback(urls);
    }
  };

  FileManager.prototype.isAvaliableOffline_ = function(gdata, type) {
    return gdata.isPresent || type.offline;
  };

  FileManager.prototype.isOffline = function() {
    return !navigator.onLine;
  };

  FileManager.prototype.onOnlineOffline_ = function() {
    if (this.isOffline()) {
      console.log('OFFLINE');
      this.dialogContainer_.setAttribute('offline', true);
    } else {
      console.log('ONLINE');
      this.dialogContainer_.removeAttribute('offline');
    }
  };

  /**
   * Event handler called when some volume was mounted or unmouted.
   */
  FileManager.prototype.onMountCompleted_ = function(event) {
    var self = this;

    var changeDirectoryTo = null;

    if (event && event.mountType == 'gdata') {
      metrics.recordInterval('Load.GData');
      console.log("GData mounted");
      if (event.status == 'success') {
        this.gdataMounted_ = true;
        this.gdataMountInfo_ = {
          "mountPath": event.mountPath,
          "sourceUrl": event.sourceUrl,
          "mountType": event.mountType,
          "mountCondition": event.status
        };
        // Not calling clearGDataLoadingTimer_ here because we want to keep
        // "Loading Google Docs" message until the directory loads. It is OK if
        // the timer fires after the mount because onDirectoryChanged_ will hide
        // the unmounted panel.
        if (this.setupCurrentDirectoryPostponed_) {
          this.setupCurrentDirectoryPostponed_(false /* execute */);
        } else if (this.isOnGData() &&
                   this.directoryModel_.currentEntry.unmounted) {
          // We are currently on an unmounted GData directory, force a rescan.
          changeDirectoryTo = this.directoryModel_.rootPath;
        }
      } else {
        this.gdataMounted_ = false;
        this.gdataMountInfo_ = null;
        this.clearGDataLoadingTimer_();
        this.onGDataUnreachable_('GData mount failed: ' +  event.status);
        if (this.setupCurrentDirectoryPostponed_) {
          this.setupCurrentDirectoryPostponed_(true /* cancel */);
          // Change to unmounted GData root.
          changeDirectoryTo = '/' + DirectoryModel.GDATA_DIRECTORY;
        }
      }
    }

    chrome.fileBrowserPrivate.getMountPoints(function(mountPoints) {
      self.setMountPoints_(mountPoints);
      if (event.eventType == 'mount') {
        // Mount request finished - remove it.
        var index = self.mountRequests_.indexOf(event.sourceUrl);
        if (index != -1) {
          self.mountRequests_.splice(index, 1);
          // Go to mounted directory, if request was initiated from this tab.
          if (event.status == 'success')
            changeDirectoryTo = event.mountPath;
        }
      }

      if (event.eventType == 'unmount') {
        // Unmount request finished - remove it.
        var index = self.unmountRequests_.indexOf(event.sourceUrl);
        if (index != -1) {
          self.unmountRequests_.splice(index, 1);
          if (event.status != 'success')
            self.alert.show(strf('UNMOUNT_FAILED', event.status));
        }
      }

      if (event.eventType == 'mount' && event.status != 'success' &&
          event.mountType == 'file') {
        // Report mount error.
        var fileName = event.sourceUrl.substr(
            event.sourceUrl.lastIndexOf('/') + 1);
        self.alert.show(strf('ARCHIVE_MOUNT_FAILED', fileName,
                             event.status));
      }

      if (event.eventType == 'unmount' && event.status == 'success' &&
          event.mountPath == self.directoryModel_.rootPath) {
        if (self.params_.mountTriggered) {
          // window.close() sometimes doesn't work.
          chrome.tabs.getCurrent(function(tab){
            chrome.tabs.remove(tab.id);
          });
          return;
        }
        // Current durectory just unmounted. Move to the 'Downloads'.
        changeDirectoryTo = '/' + DirectoryModel.DOWNLOADS_DIRECTORY;
      }

      // Even if something failed root list should be rescanned.
      // Failed mounts can "give" us new devices which might be formatted,
      // so we have to refresh root list then.
      self.directoryModel_.updateRoots(function() {
        if (changeDirectoryTo) {
          self.directoryModel_.changeDirectory(changeDirectoryTo);
        }
      }, self.gdataMounted_);
    });
  };

  /**
   * Event handler called when some internal task should be executed.
   */
  FileManager.prototype.onFileTaskExecute_ = function(id, urls) {
    if (id == 'play') {
      var position = 0;
      if (urls.length == 1) {
        // If just a single audio file is selected pass along every audio file
        // in the directory.
        var selectedUrl = urls[0];
        urls = this.getAllUrlsInCurrentDirectory_().filter(FileType.isAudio);
        position = urls.indexOf(selectedUrl);
      }
      chrome.mediaPlayerPrivate.play(urls, position);
    } else if (id == 'mount-archive') {
      for (var index = 0; index < urls.length; ++index) {
        // Url in MountCompleted event won't be escaped, so let's make sure
        // we don't use escaped one in mountRequests_.
        var unescapedUrl = unescape(urls[index]);
        this.mountRequests_.push(unescapedUrl);
        chrome.fileBrowserPrivate.addMount(unescapedUrl, 'file', {});
      }
    } else if (id == 'format-device') {
      this.confirm.show(str('FORMATTING_WARNING'), function() {
        chrome.fileBrowserPrivate.formatDevice(urls[0]);
      });
    } else if (id == 'gallery') {
      this.openGallery_(urls);
    } else if (id == 'view-pdf' || id == 'view-txt' || id == 'install-crx') {
      chrome.fileBrowserPrivate.viewFiles(urls, 'default', function() {});
    } else if (id == 'open-hosted') {
      // TODO (kaznacheev)
      if (this.isOnGData()) {
        chrome.fileBrowserPrivate.getGDataFileProperties(urls,
            function(results) {
              for (var i = 0; i != results.length; i++) {
                chrome.tabs.create({
                  url: results[i].editUrl
                });
              }
            });
      } else {
        // Local file. Get the url from the text content.
        for (var i = 0; i != urls.length; i++) {
          util.readTextFromFileURL(urls[i], 10000 /* max size*/,
              function(text) {
                try {
                  var json = JSON.parse(text);
                  if ('url' in json) {
                    chrome.tabs.create({url: json.url});
                  }
                } catch(e) {
                  console.error(e);
                }
              });
        }
      }
    }
  };

  FileManager.prototype.getDeviceNumber = function(entry) {
    if (!entry.isDirectory) return undefined;
    for (var i = 0; i < this.mountPoints_.length; i++) {
      if (normalizeAbsolutePath(entry.fullPath) ==
          normalizeAbsolutePath(this.mountPoints_[i].mountPath)) {
        return i;
      }
    }
    return undefined;
  };

  FileManager.prototype.openFilePopup_ = function(popup) {
    this.closeFilePopup_();
    this.filePopup_ = popup;
    this.dialogDom_.appendChild(this.filePopup_);
    this.filePopup_.focus();
  };

  FileManager.prototype.closeFilePopup_ = function() {
    if (this.filePopup_) {
      this.dialogDom_.removeChild(this.filePopup_);
      this.filePopup_ = null;
      this.refocus();
    }
  };

  FileManager.prototype.getAllUrlsInCurrentDirectory_ = function() {
    var urls = [];
    var dm = this.directoryModel_.fileList;
    for (var i = 0; i != dm.length; i++) {
      urls.push(dm.item(i).toURL());
    }
    return urls;
  };

  FileManager.prototype.getShareActions_ = function(urls, callback) {
    chrome.fileBrowserPrivate.getFileTasks(urls, function(tasks) {
      var shareActions = [];
      for (var index = 0; index < tasks.length; index++) {
        var task = tasks[index];
        if (task.taskId != this.getExtensionId_() + '|gallery') {
          // Add callback, so gallery can execute the task.
          task.execute = this.dispatchFileTask_.bind(this, task.taskId);
          shareActions.push(task);
        }
      }
      callback(shareActions);
    }.bind(this));
  };

  FileManager.prototype.openGallery_ = function(urls) {
    var self = this;

    var galleryFrame = this.document_.createElement('iframe');
    galleryFrame.className = 'overlay-pane';
    galleryFrame.scrolling = 'no';
    galleryFrame.setAttribute('webkitallowfullscreen', true);

    var selectedUrl;
    if (urls.length == 1 && FileType.isImage(urls[0])) {
      // Single image item selected. Pass to the Gallery as a selected.
      selectedUrl = urls[0];
      // Pass along every image and video in the directory so that it shows up
      // in the ribbon.
      // We do not do that if a single video is selected because the UI is
      // cleaner without the ribbon.
      urls = this.getAllUrlsInCurrentDirectory_().filter(
          FileType.isImageOrVideo);
    } else {
      // Pass just the selected items, select the first entry.
      selectedUrl = urls[0];
    }

    var dirPath = this.directoryModel_.currentEntry.fullPath;

    // Push a temporary state which will be replaced every time an individual
    // item is selected in the Gallery.
    this.updateLocation_(false /*push*/, dirPath);

    galleryFrame.onload = function() {
      self.document_.title = str('ACTION_VIEW');
      galleryFrame.contentWindow.ImageUtil.metrics = metrics;
      galleryFrame.contentWindow.FileType = FileType;
      galleryFrame.contentWindow.util = util;

      // Gallery shoud treat GData folder as readonly.
      var readonly = self.directoryModel_.readonly || self.isOnGData();
      var currentDir = self.directoryModel_.currentEntry;
      var downloadsDir = self.directoryModel_.rootsList.item(0);

      var context = {
        // We show the root label in readonly warning (e.g. archive name).
        readonlyDirName:
            readonly ?
                (self.isOnGData() ?
                    self.getRootLabel_(currentDir.fullPath) :
                    self.directoryModel_.rootName) :
                null,
        saveDirEntry: readonly ? downloadsDir : currentDir,
        metadataProvider: self.getMetadataProvider(),
        getShareActions: self.getShareActions_.bind(self),
        onNameChange: function(name) {
          self.updateLocation_(true /*replace*/, dirPath + '/' + name);
        },
        onClose: function() {
          history.back(1);
        },
        displayStringFunction: strf
      };
      galleryFrame.contentWindow.Gallery.open(context, urls, selectedUrl);
    };

    galleryFrame.src = 'gallery.html';
    this.openFilePopup_(galleryFrame);
  };

  /**
   * Update the breadcrumb display to reflect the current directory.
   */
  FileManager.prototype.updateBreadcrumbs_ = function() {
    var bc = this.dialogDom_.querySelector('.breadcrumbs');
    removeChildren(bc);

    var rootPath = this.directoryModel_.rootPath;
    var relativePath = this.directoryModel_.currentEntry.fullPath.
                       substring(rootPath.length).replace(/\/$/, '');

    var pathNames = relativePath.replace(/\/$/, '').split('/');
    if (pathNames[0] == '')
      pathNames.splice(0, 1);

    // We need a first breadcrumb for root, so placing last name from
    // rootPath as first name of relativePath.
    var rootPathNames = rootPath.replace(/\/$/, '').split('/');
    pathNames.splice(0, 0, rootPathNames[rootPathNames.length - 1]);
    rootPathNames.splice(rootPathNames.length - 1, 1);
    var path = rootPathNames.join('/') + '/';
    var doc = this.document_;

    for (var i = 0; i < pathNames.length; i++) {
      var pathName = pathNames[i];
      path += pathName;

      var div = doc.createElement('div');

      div.className = 'breadcrumb-path';
      div.textContent = i == 0 ? this.getRootLabel_(path) : pathName;

      path = path + '/';
      div.path = path;

      if (i == 0) {
        div.classList.add('root-label');
        div.setAttribute('icon', DirectoryModel.getRootType(rootPath));

        // Wrapper with zero margins.
        var wrapper = doc.createElement('div');
        wrapper.appendChild(div);
        bc.appendChild(wrapper);
      } else {
        bc.appendChild(div);
      }

      if (i == pathNames.length - 1) {
        div.classList.add('breadcrumb-last');
      } else {
        div.addEventListener('click', this.onBreadcrumbClick_.bind(this));

        var spacer = doc.createElement('div');
        spacer.className = 'separator';
        bc.appendChild(spacer);
      }
    }
    this.truncateBreadcrumbs_();
  };

  FileManager.prototype.isRenamingInProgress = function() {
    return !!this.renameInput_.currentEntry;
  };

  /**
   * Updates breadcrumbs widths in order to truncate it properly.
   */
  FileManager.prototype.truncateBreadcrumbs_ = function() {
    var bc = this.dialogDom_.querySelector('.breadcrumbs');
    if (!bc.firstChild)
     return;

    // Assume style.width == clientWidth (items have no margins or paddings).

    for (var item = bc.firstChild; item; item = item.nextSibling) {
      item.removeAttribute('style');
      item.removeAttribute('collapsed');
    }

    var containerWidth = bc.clientWidth;

    var pathWidth = 0;
    var currentWidth = 0;
    var lastSeparator;
    for (var item = bc.firstChild; item; item = item.nextSibling) {
      if (item.className == 'separator') {
        pathWidth += currentWidth;
        currentWidth = item.clientWidth;
        lastSeparator = item;
      } else {
        currentWidth += item.clientWidth;
      }
    }
    if (pathWidth + currentWidth <= containerWidth)
      return;
    if (!lastSeparator) {
      bc.lastChild.style.width = Math.min(currentWidth, containerWidth) + 'px';
      return;
    }
    var lastCrumbSeparatorWidth = lastSeparator.clientWidth;
    // Current directory name may occupy up to 70% of space or even more if the
    // path is short.
    var maxPathWidth = Math.max(Math.round(containerWidth * 0.3),
                                containerWidth - currentWidth);
    maxPathWidth = Math.min(pathWidth, maxPathWidth);

    var parentCrumb = lastSeparator.previousSibling;
    var collapsedWidth = 0;
    if (parentCrumb && pathWidth - maxPathWidth > parentCrumb.clientWidth) {
      // At least one crumb is hidden completely (or almost completely).
      // Show sign of hidden crumbs like this:
      // root > some di... > ... > current directory.
      parentCrumb.setAttribute('collapsed', '');
      collapsedWidth = Math.min(maxPathWidth, parentCrumb.clientWidth);
      maxPathWidth -= collapsedWidth;
      if (parentCrumb.clientWidth != collapsedWidth)
        parentCrumb.style.width = collapsedWidth + 'px';

      lastSeparator = parentCrumb.previousSibling;
      if (!lastSeparator)
        return;
      collapsedWidth += lastSeparator.clientWidth;
      maxPathWidth = Math.max(0, maxPathWidth - lastSeparator.clientWidth);
    }

    pathWidth = 0;
    for (var item = bc.firstChild; item != lastSeparator;
         item = item.nextSibling) {
      // TODO(serya): Mixing access item.clientWidth and modifying style and
      // attributes could cause multiple layout reflows.
      if (pathWidth + item.clientWidth <= maxPathWidth) {
        pathWidth += item.clientWidth;
      } else if (pathWidth == maxPathWidth) {
        item.style.width = '0';
      } else if (item.classList.contains('separator')) {
        // Do not truncate separator. Instead let the last crumb be longer.
        item.style.width = '0';
        maxPathWidth = pathWidth;
      } else {
        // Truncate the last visible crumb.
        item.style.width = (maxPathWidth - pathWidth) + 'px';
        pathWidth = maxPathWidth;
      }
    }

    currentWidth = Math.min(currentWidth,
                            containerWidth - pathWidth - collapsedWidth);
    bc.lastChild.style.width = (currentWidth - lastCrumbSeparatorWidth) + 'px';
  };

  FileManager.prototype.formatMetadataValue_ = function(obj) {
    if (typeof obj.type == 'undefined') {
      return obj.value;
    } else if (obj.type == 'duration') {


      var totalSeconds = Math.floor(obj.value / 1000);
      var hours = Math.floor(totalSeconds / 60 / 60);

      var fmtSkeleton;

      // Print hours if available
      //TODO: dzvorygin use better skeletons when documentation become available
      if (hours > 0) {
        fmtSkeleton = 'hh:mm:ss';
      } else {
        fmtSkeleton = 'mm:ss';
      }

      // Convert duration to milliseconds since time start
      var date = new Date(parseInt(obj.value));

      var fmt = this.locale_.createDateTimeFormat({skeleton:fmtSkeleton});

      return fmt.format(date);
    }
  };

  FileManager.prototype.getThumbnailURL = function(entry, callback) {
    if (!entry)
      return;

    var iconType = this.getIconType(entry);

    function returnStockIcon() {
      callback(iconType, FileType.getPreviewArt(iconType));
    }

    this.getMetadataProvider().fetch(entry.toURL(), function (metadata) {
      if (metadata.thumbnailURL) {
        callback(iconType, metadata.thumbnailURL, metadata.thumbnailTransform);
      } else if (iconType == 'image') {
        cacheEntryDateAndSize(entry, function() {
          if (FileType.canUseImageUrlForPreview(metadata, entry.cachedSize_)) {
            callback(iconType, entry.toURL(), metadata.imageTransform);
          } else {
            returnStockIcon();
          }
        }, returnStockIcon);
      } else {
        returnStockIcon();
      }
    });
  };

  FileManager.prototype.getDescription = function(entry, callback) {
    if (!entry)
      return;

    this.getMetadataProvider().fetch(entry.toURL(), function(metadata) {
      callback(metadata.description);
    });
  };

  FileManager.prototype.focusCurrentList_ = function() {
    if (this.listType_ == FileManager.ListType.DETAIL)
      this.table_.focus();
    else  // this.listType_ == FileManager.ListType.THUMBNAIL)
      this.grid_.focus();
  };

  /**
   * Return full path of the current directory or null.
   */
  FileManager.prototype.getCurrentDirectory = function() {
    return this.directoryModel_ &&
        this.directoryModel_.currentEntry.fullPath;
  };

  /**
   * Return URL of the current directory or null.
   */
  FileManager.prototype.getCurrentDirectoryURL = function() {
    return this.directoryModel_ &&
        this.directoryModel_.currentEntry.toURL();
  };

  FileManager.prototype.deleteEntries = function(entries, force, opt_callback) {
    if (!force) {
      var self = this;
      var msg;
      if (entries.length == 1) {
        msg = strf('CONFIRM_DELETE_ONE', entries[0].name);
      } else {
        msg = strf('CONFIRM_DELETE_SOME', entries.length);
      }

      this.confirm.show(msg, this.deleteEntries.bind(
          this, entries, true, opt_callback));
      return;
    }

    this.directoryModel_.deleteEntries(entries, opt_callback);
  };

  FileManager.prototype.blinkSelection = function() {
    if (!this.selection || this.selection.totalCount == 0)
      return;

    for (var i = 0; i < this.selection.entries.length; i++) {
      var selectedIndex = this.selection.indexes[i];
      var listItem = this.currentList_.getListItemByIndex(selectedIndex);
      if (listItem)
        listItem.classList.add('blink');
    }

    var self = this;

    setTimeout(function() {
        for (var i = 0; i < self.selection.entries.length; i++) {
          var selectedIndex = self.selection.indexes[i];
          var listItem = self.currentList_.getListItemByIndex(selectedIndex);
          if (listItem)
            listItem.classList.remove('blink');
        }
    }, 100);
  };

  /**
   * Write the current selection to system clipboard.
   *
   * @param {Clipboard} clipboard Clipboard from the event.
   * @param {boolean} isCut True if the current command is cut.
   */
  FileManager.prototype.cutOrCopyToClipboard_ = function(clipboard, isCut) {
    var directories  = '';
    var files = '';
    for(var i = 0, entry; i < this.selection.entries.length; i++) {
      entry = this.selection.entries[i];
      if (entry.isDirectory)
        directories += entry.fullPath + '\n';
      else
        files += entry.fullPath + '\n';
    }

    clipboard.setData('fs/isCut', isCut.toString());
    clipboard.setData('fs/isOnGData', this.isOnGData());
    clipboard.setData('fs/sourceDir',
                                  this.directoryModel_.currentEntry.fullPath);
    clipboard.setData('fs/directories', directories);
    clipboard.setData('fs/files', files);

    var files = this.selection.files;
    for(var i = 0; i < files.length; i++) {
      clipboard.items.add(files[i]);
    }
  }

  FileManager.prototype.onCopy_ = function(event) {
    if (this.document_.activeElement.tagName == 'INPUT' ||
        !this.canCopyOrDrag_()) {
      return;
    }
    event.preventDefault();
    this.cutOrCopyToClipboard_(event.clipboardData, false);
    this.blinkSelection();
  };

  FileManager.prototype.onBeforeCopy_ = function(event) {
    // queryCommandEnabled returns true if event.returnValue is false.
    event.returnValue = !this.canCopyOrDrag_();
  };

  FileManager.prototype.canCopyOrDrag_ = function() {
    return this.selection && this.selection.totalCount > 0;
  };

  FileManager.prototype.onCut_ = function(event) {
    if (!this.document_.activeElement.tagName == 'INPUT' ||
        this.canCutOrDrag_()) {
      return;
    }
    event.preventDefault();
    this.cutOrCopyToClipboard_(event.clipboardData, true);

    this.blinkSelection();
  };

  FileManager.prototype.onBeforeCut_ = function(event) {
    // queryCommandEnabled returns true if event.returnValue is false.
    event.returnValue = !this.canCutOrDrag_();
  };

  FileManager.prototype.canCutOrDrag_ = function() {
    var ro = this.directoryModel_.readonly;
    return !ro && this.selection && this.selection.totalCount > 0;
  };

  FileManager.prototype.onPaste_ = function(event) {
    if (this.document_.activeElement.tagName == 'INPUT' ||
        !this.canPasteOrDrop_(event.clipboardData)) {
      return;
    }
    event.preventDefault();
    this.pasteFromClipboard_(event.clipboardData);
  };

  FileManager.prototype.onBeforePaste_ = function(event) {
    // TODO(serya) should be:
    // event.returnValue = !this.canPasteOrDrop_(event.clipboardData);
    this.commands_.paste.disabled =
        !this.canPasteOrDrop_(event.clipboardData);
  };

  FileManager.prototype.canPasteOrDrop_ = function(clipboardData) {
    var ro = this.directoryModel_.readonly;
    return !ro && !!clipboardData.getData('fs/isCut');
  };

  /**
   * Queue up a file copy operation based on the current system clipboard.
   */
  FileManager.prototype.pasteFromClipboard_ = function(clipboard) {
    var operationInfo = {
      isCut: clipboard.getData('fs/isCut'),
      isOnGData: clipboard.getData('fs/isOnGData'),
      sourceDir: clipboard.getData('fs/sourceDir'),
      directories: clipboard.getData('fs/directories'),
      files: clipboard.getData('fs/files')
    };

    this.copyManager_.paste(operationInfo,
                            this.directoryModel_.currentEntry,
                            this.isOnGData(),
                            this.filesystem_.root);

    var clearClipboard = function (event) {
      event.preventDefault();
      event.clipboardData.setData('fs/clear', '');
    }

    // On cut, we clear the clipboard after the file is pasted/moved so we don't
    // try to move/delete the original file again.
    if (operationInfo.isCut == 'true') {
      this.document_.removeEventListener('cut', this.onCutBound_);
      this.document_.addEventListener('cut', clearClipboard);
      this.document_.execCommand('cut');
      this.document_.removeEventListener('cut', clearClipboard);
      this.document_.addEventListener('cut', this.onCutBound_);
    }
  };

  /**
   * Update the selection summary UI when the selection summarization completes.
   */
  FileManager.prototype.onSelectionSummarized_ = function() {
    var selection = this.selection;
    var bytes = util.bytesToSi(selection.bytes);
    var text = '';
    if (selection.totalCount == 0) {
      // We dont want to change the string during preview panel animating away.
      return;
    } else if (selection.fileCount == 1 && selection.directoryCount == 0) {
      text = selection.entries[0].name + ', ' + bytes;
    } else if (selection.fileCount == 0 && selection.directoryCount == 1) {
      text = selection.entries[0].name;
    } else if (selection.directoryCount == 0) {
      text = strf('MANY_FILES_SELECTED', selection.fileCount, bytes);
    } else if (selection.fileCount == 0) {
      text = strf('MANY_DIRECTORIES_SELECTED', selection.directoryCount);
    } else {
      text = strf('MANY_ENTRIES_SELECTED', selection.totalCount, bytes);
    }
    this.previewSummary_.textContent = text;
  };

  /**
   * Handle a click event on a breadcrumb element.
   *
   * @param {Event} event The click event.
   */
  FileManager.prototype.onBreadcrumbClick_ = function(event) {
    this.directoryModel_.changeDirectory(event.srcElement.path);
  };

  FileManager.prototype.onCheckboxMouseDownUp_ = function(event) {
    // If exactly one file is selected and its checkbox is *not* clicked,
    // then this should be treated as a "normal" click (ie. the previous
    // selection should be cleared).
    if (this.selection.totalCount == 1 && this.selection.entries[0].isFile) {
      var selectedIndex = this.selection.indexes[0];
      var listItem = this.currentList_.getListItemByIndex(selectedIndex);
      var checkbox = listItem.querySelector('input[type="checkbox"]');
      if (!checkbox.checked)
        return;
    }

    // Otherwise, treat clicking on a checkbox the same as a ctrl-click.
    // The default properties of event.ctrlKey make it read-only, but
    // don't prevent deletion, so we delete first, then set it true.
    delete event.ctrlKey;
    event.ctrlKey = true;
  };

  FileManager.prototype.onCheckboxClick_ = function(event) {
    if (event.shiftKey) {
      // Something about the timing of shift-clicks causes the checkbox
      // to get selected and then very quickly unselected.  It appears that
      // we forcibly select the checkbox as part of onSelectionChanged, and
      // then the default action of this click event fires and toggles the
      // checkbox back off.
      //
      // Since we're going to force checkboxes into the correct state for any
      // multi-selection, we can prevent this shift click from toggling the
      // checkbox and avoid the trouble.
      event.preventDefault();
    }
  };

  FileManager.prototype.onPinClick_ = function(checkbox, entry, event) {
    var self = this;
    function callback(props) {
      if (props.errorCode) {
        // TODO(serya): Do not show the message if unpin failed.
        cacheEntryDateAndSize(entry, function() {
          self.alert.showHtml(str('GDATA_OUT_OF_SPACE_HEADER'),
              strf('GDATA_OUT_OF_SPACE_MESSAGE',
                  util.bytesToSi(entry.cachedSize_)));
        });
      }
      checkbox.checked = entry.gdata_.isPinned = props[0].isPinned;
    }

    chrome.fileBrowserPrivate.pinGDataFile([entry.toURL()],
                                           checkbox.checked, callback);
    event.preventDefault();
  };

  FileManager.prototype.selectDefaultPathInFilenameInput_ = function() {
    var input = this.filenameInput_;
    input.focus();
    var selectionEnd = input.value.lastIndexOf('.');
    if (selectionEnd == -1) {
      input.select();
    } else {
      input.selectionStart = 0;
      input.selectionEnd = selectionEnd;
    }
    // Clear, so we never do this again.
    this.params_.defaultPath = '';
  };

  /**
   * Update the UI when the selection model changes.
   *
   * @param {cr.Event} event The change event.
   */
  FileManager.prototype.onSelectionChanged_ = function(event) {
    this.summarizeSelection_();

    if (this.dialogType_ == FileManager.DialogType.SELECT_SAVEAS_FILE) {
      // If this is a save-as dialog, copy the selected file into the filename
      // input text box.

      if (this.selection &&
          this.selection.totalCount == 1 &&
          this.selection.entries[0].isFile &&
          this.filenameInput_.value != this.selection.entries[0].name) {
        this.filenameInput_.value = this.selection.entries[0].name;
      }
    }

    this.updateOkButton_();

    var newThumbnailUrlCache = {};
    if (this.selection) {
      const entries = this.selection.entries;
      for (var i = 0; i < entries.length; i++) {
        var path = entries[i].fullPath;
        if (path in this.thumbnailUrlCache_)
          newThumbnailUrlCache[path] = this.thumbnailUrlCache_[path];
      }
    }
    this.thumbnailUrlCache_ = newThumbnailUrlCache;

    setTimeout(this.onSelectionChangeComplete_.bind(this, event), 0);
  };

  /**
   * Handle selection change related tasks that won't run properly during
   * the actual selection change event.
   */
  FileManager.prototype.onSelectionChangeComplete_ = function(event) {
    // Inform tests it's OK to click buttons now.
    chrome.test.sendMessage('selection-change-complete');

    if (!this.showCheckboxes_)
      return;

    for (var i = 0; i < event.changes.length; i++) {
      // Turn off any checkboxes for items that are no longer selected.
      var selectedIndex = event.changes[i].index;
      var listItem = this.currentList_.getListItemByIndex(selectedIndex);
      if (!listItem) {
        // When changing directories, we get notified about list items
        // that are no longer there.
        continue;
      }

      if (!event.changes[i].selected) {
        var checkbox = listItem.querySelector('input[type="checkbox"]');
        checkbox.checked = false;
      }
    }

    if (this.selection.totalCount > 0) {
      // If more than one file is selected, make sure all checkboxes are lit
      // up.
      for (var i = 0; i < this.selection.entries.length; i++) {
        var selectedIndex = this.selection.indexes[i];
        var listItem = this.currentList_.getListItemByIndex(selectedIndex);
        if (listItem)
          listItem.querySelector('input[type="checkbox"]').checked = true;
      }
    }
    var selectAllCheckbox =
        this.document_.getElementById('select-all-checkbox');
    if (selectAllCheckbox)
      this.updateSelectAllCheckboxState_(selectAllCheckbox);
  };

  FileManager.prototype.updateOkButton_ = function(event) {
    var selectable;

    if (this.dialogType_ == FileManager.DialogType.SELECT_FOLDER) {
      selectable = this.selection.directoryCount == 1 &&
          this.selection.fileCount == 0;
    } else if (this.dialogType_ == FileManager.DialogType.SELECT_OPEN_FILE) {
      selectable = (this.selection.directoryCount == 0 &&
                    this.selection.fileCount == 1);
    } else if (this.dialogType_ ==
               FileManager.DialogType.SELECT_OPEN_MULTI_FILE) {
      selectable = (this.selection.directoryCount == 0 &&
                    this.selection.fileCount >= 1);
    } else if (this.dialogType_ == FileManager.DialogType.SELECT_SAVEAS_FILE) {
      if (this.directoryModel_.readonly) {
        // Nothing can be saved in to the root or media/ directories.
        selectable = false;
      } else {
        selectable = !!this.filenameInput_.value;
      }
    } else if (this.dialogType_ == FileManager.DialogType.FULL_PAGE) {
      // No "select" buttons on the full page UI.
      selectable = true;
    } else {
      throw new Error('Unknown dialog type');
    }

    this.okButton_.disabled = !selectable;
    return selectable;
  };

  /**
   * Handle a double-click event on an entry in the detail list.
   *
   * @param {Event} event The click event.
   */
  FileManager.prototype.onDetailDoubleClick_ = function(event) {
    if (this.isRenamingInProgress()) {
      // Don't pay attention to double clicks during a rename.
      return;
    }

    if (this.selection.totalCount != 1) {
      console.log('Invalid selection');
      return;
    }
    var entry = this.selection.entries[0];

    if (entry.isDirectory) {
      return this.onDirectoryAction(entry);
    }

    this.dispatchSelectionAction_();
  };

  FileManager.prototype.dispatchSelectionAction_ = function() {
    if (this.dialogType_ == FileManager.DialogType.FULL_PAGE) {
      this.dispatchDefaultTask_();
      return true;
    }
    if (!this.okButton_.disabled) {
      this.onOk_();
      return true;
    }
    return false;
  };

  FileManager.prototype.onDirectoryAction = function(entry) {
    var deviceNumber = this.getDeviceNumber(entry);
    if (deviceNumber != undefined &&
        this.mountPoints_[deviceNumber].mountCondition ==
        'unknown_filesystem') {
      return this.showButter(str('UNKNOWN_FILESYSTEM_WARNING'));
    } else if (deviceNumber != undefined &&
               this.mountPoints_[deviceNumber].mountCondition ==
               'unsupported_filesystem') {
      return this.showButter(str('UNSUPPORTED_FILESYSTEM_WARNING'));
    } else {
      return this.directoryModel_.changeDirectory(entry.fullPath);
    }
  };

  /**
   * Show or hide the "Low disk space" warning.
   * @param {boolean} show True if the box need to be shown.
   */
  FileManager.prototype.showLowDiskSpaceWarning_ = function(show) {
    if (show) {
      if (this.downloadsWarning_.hasAttribute('hidden')) {
        this.downloadsWarning_.removeAttribute('hidden');
        this.requestResize_(0);
      }
    } else {
      if (!this.downloadsWarning_.hasAttribute('hidden')) {
        this.downloadsWarning_.setAttribute('hidden', '');
        this.requestResize_(100);
      }
    }
  };

  /**
   * Update the location in the address bar.
   *
   * @param {boolean} replace True if the history state should be replaced,
   *                          false if pushed.
   * @param {string} path Path to be put in the address bar after the hash.
   */
  FileManager.prototype.updateLocation_ = function(replace, path) {
    var location = document.location.origin + document.location.pathname + '#' +
                   encodeURI(path);
    //TODO(kaznacheev): Fix replaceState for component extensions. Currently it
    //does not replace the content of the address bar.
    if (replace)
      history.replaceState(undefined, path, location);
    else
      history.pushState(undefined, path, location);
  },

  /**
   * Update the UI when the current directory changes.
   *
   * @param {cr.Event} event The directory-changed event.
   */
  FileManager.prototype.onDirectoryChanged_ = function(event) {
    this.updateCommonActionButtons_();
    this.updateOkButton_();
    this.updateBreadcrumbs_();
    this.updateColumnModel_();

    // Updated when a user clicks on the label of a file, used to detect
    // when a click is eligible to trigger a rename.  Can be null, or
    // an object with 'path' and 'date' properties.
    this.lastLabelClick_ = null;

    var dirEntry = event.newDirEntry;
    this.updateLocation_(event.initial, dirEntry.fullPath);

    this.checkFreeSpace_(this.getCurrentDirectory());

    // TODO(dgozman): title may be better than this.
    this.document_.title = this.getCurrentDirectory().substr(1);

    var self = this;

    if (this.watchedDirectoryUrl_) {
      if (this.watchedDirectoryUrl_ != event.previousDirEntry.toURL()) {
        console.warn('event.previousDirEntry does not match File Manager state',
            event, this.watchedDirectoryUrl_);
      }
      chrome.fileBrowserPrivate.removeFileWatch(this.watchedDirectoryUrl_,
          function(result) {
            if (!result) {
              console.log('Failed to remove file watch');
            }
          });
      this.watchedDirectoryUrl_ = null;
    }

    if (event.newDirEntry.fullPath != '/' && !event.newDirEntry.unmounted) {
      this.watchedDirectoryUrl_ = event.newDirEntry.toURL();
      chrome.fileBrowserPrivate.addFileWatch(this.watchedDirectoryUrl_,
        function(result) {
          if (!result) {
            console.log('Failed to add file watch');
            this.watchedDirectoryUrl_ = null;
          }
        }.bind(this));
    }

    this.updateVolumeMetadata_();

    if (event.newDirEntry.unmounted)
      this.dialogContainer_.setAttribute('unmounted', true);
    else
      this.dialogContainer_.removeAttribute('unmounted');

    if (this.isOnGData()) {
      this.dialogContainer_.setAttribute('gdata', true);
      if (event.newDirEntry.unmounted) {
        this.initGData_(true /* directory changed */);
      }
    } else {
      this.dialogContainer_.removeAttribute('gdata');
    }
  };

  FileManager.prototype.updateVolumeMetadata_ = function() {
    var dm = this.directoryModel_;
    if (!dm) return;

    // Only request metadata for removable devices and archives.
    var rootType = dm.rootType;
    if (rootType != DirectoryModel.RootType.REMOVABLE &&
        rootType != DirectoryModel.RootType.ARCHIVE)
      return;

    dm.readonly = true;
    var rootEntry = dm.rootEntry;
    if (!rootEntry) return;
    chrome.fileBrowserPrivate.getVolumeMetadata(rootEntry.toURL(),
                                                function(metadata) {
      if (metadata && dm.rootEntry == rootEntry)
        dm.readonly = metadata.isReadOnly;
    });
  };

  FileManager.prototype.findListItemForEvent_ = function(event) {
    return this.findListItemForNode_(event.srcElement);
  };

  FileManager.prototype.findListItemForNode_ = function(node) {
    var item = this.currentList_.getListItemAncestor(node);
    // TODO(serya): list should check that.
    return item && this.currentList_.isItem(item) ? item : null;
  };

  FileManager.prototype.onGridOrTableMouseDown_ = function(event) {
    var item = this.findListItemForEvent_(event);
    if (!item)
      return;

    if (this.allowRenameClick_(event, item)) {
      event.preventDefault();
      this.directoryModel_.fileListSelection.selectedIndex = item.listIndex;
      this.initiateRename_();
    }
  };

  /**
   * Unload handler for the page.  May be called manually for the file picker
   * dialog, because it closes by calling extension API functions that do not
   * return.
   */
  FileManager.prototype.onUnload_ = function() {
    if (this.watchedDirectoryUrl_) {
      chrome.fileBrowserPrivate.removeFileWatch(
          this.watchedDirectoryUrl_,
          function(result) {
            if (!result) {
              console.log('Failed to remove file watch');
            }
          });
      this.watchedDirectoryUrl_ = null;
    }
  };

  FileManager.prototype.onFileChanged_ = function(event) {
    // We receive a lot of events even in folders we are not interested in.
    if (event.fileUrl == this.getCurrentDirectoryURL())
      this.directoryModel_.rescanLater();
  };

  /**
   * Determine whether or not a click should initiate a rename.
   *
   * Renames can happen on mouse click if the user clicks on a label twice,
   * at least a half second apart.
   * @param {MouseEvent} event Click on the item.
   * @param {cr.ui.ListItem} item Clicked item.
   */
  FileManager.prototype.allowRenameClick_ = function(event, item) {
    var dir = this.directoryModel_.currentEntry;
    if (this.dialogType_ != FileManager.DialogType.FULL_PAGE ||
        this.directoryModel_.readonly) {
      // Renaming only enabled for full-page mode, outside of the root
      // directory.
      return false;
    }

    // Didn't click on the label.
    if (!event.srcElement.classList.contains('filename-label'))
      return false;

    // Wrong button or using a keyboard modifier.
    if (event.button != 0 || event.shiftKey || event.metaKey || event.altKey) {
      this.lastLabelClick_ = null;
      return false;
    }

    var now = new Date();
    var lastLabelClick = this.lastLabelClick_;
    this.lastLabelClick_ = {index: item.listIndex, date: now};

    if (this.isRenamingInProgress())
      return false;

    if (lastLabelClick && lastLabelClick.index == item.listIndex) {
      var delay = now - lastLabelClick.date;
      if (delay > 500 && delay < 2000) {
        this.lastLabelClick_ = null;
        return true;
      }
    }

    return false;
  };

  FileManager.prototype.initiateRename_ = function() {
    var item = this.currentList_.ensureLeadItemExists();
    if (!item)
      return;
    var label = item.querySelector('.filename-label');
    var input = this.renameInput_;

    input.value = label.textContent;
    label.parentNode.setAttribute('renaming', '');
    label.parentNode.appendChild(input);
    input.focus();
    var selectionEnd = input.value.lastIndexOf('.');
    if (selectionEnd == -1) {
      input.select();
    } else {
      input.selectionStart = 0;
      input.selectionEnd = selectionEnd;
    }

    // This has to be set late in the process so we don't handle spurious
    // blur events.
    input.currentEntry = this.currentList_.dataModel.item(item.listIndex);
  };

  FileManager.prototype.onRenameInputKeyDown_ = function(event) {
    if (!this.isRenamingInProgress())
      return;

    // Do not move selection or lead item in list during rename.
    if (event.keyIdentifier == 'Up' || event.keyIdentifier == 'Down') {
      event.stopPropagation();
    }

    switch (util.getKeyModifiers(event) + event.keyCode) {
      case '27':  // Escape
        this.cancelRename_();
        event.preventDefault();
        break;

      case '13':  // Enter
        this.commitRename_();
        event.preventDefault();
        break;
    }
  };

  FileManager.prototype.onRenameInputBlur_ = function(event) {
    if (this.isRenamingInProgress() && !this.renameInput_.validation_)
      this.cancelRename_();
  };

  FileManager.prototype.commitRename_ = function() {
    var input = this.renameInput_;
    var entry = input.currentEntry;
    var newName = input.value;

    if (newName == entry.name) {
      this.cancelRename_();
      return;
    }

    input.validation_ = true;
    function validationDone() {
      input.validation_ = false;
      // Alert dialog restores focus unless the item removed from DOM.
      if (this.document_.activeElement != input)
        this.cancelRename_();
    }

    if (!this.validateFileName_(newName, validationDone.bind(this)))
      return;

    var nameNode = this.findListItemForNode_(this.renameInput_).
                   querySelector('.filename-label');
    function onError(err) {
      nameNode.textContent = entry.name;
      this.alert.show(strf('ERROR_RENAMING', entry.name,
                           util.getFileErrorMnemonic(err.code)));
    }

    this.cancelRename_();
    // Optimistically apply new name immediately to avoid flickering in
    // case of success.
    nameNode.textContent = newName;

    this.directoryModel_.doesExist(newName, function(exists, isFile) {
      if (!exists) {
        this.directoryModel_.renameEntry(entry, newName, onError.bind(this));
      } else {
        nameNode.textContent = entry.name;
        var message = isFile ? 'FILE_ALREADY_EXISTS' :
                               'DIRECTORY_ALREADY_EXISTS';
        this.alert.show(strf(message, newName));
      }
    }.bind(this));
  };

  FileManager.prototype.cancelRename_ = function() {
    this.renameInput_.currentEntry = null;
    this.lastLabelClick_ = null;

    var parent = this.renameInput_.parentNode;
    if (parent) {
      parent.removeAttribute('renaming');
      parent.removeChild(this.renameInput_);
    }
    this.refocus();
  };

  FileManager.prototype.onFilenameInputKeyUp_ = function(event) {
    var enabled = this.updateOkButton_();
    if (enabled &&
        (util.getKeyModifiers(event) + event.keyCode) == '13' /* Enter */)
      this.onOk_();
  };

  FileManager.prototype.onFilenameInputFocus_ = function(event) {
    var input = this.filenameInput_;

    // On focus we want to select everything but the extension, but
    // Chrome will select-all after the focus event completes.  We
    // schedule a timeout to alter the focus after that happens.
    setTimeout(function() {
        var selectionEnd = input.value.lastIndexOf('.');
        if (selectionEnd == -1) {
          input.select();
        } else {
          input.selectionStart = 0;
          input.selectionEnd = selectionEnd;
        }
    }, 0);
  };

  /**
   * Handles a keypress on the toggle sidebar button.  It has the same effect as
   * a mouseclick if enter or space is pressed.
   *
   * @param {KeyboardEvent} event Information on the key event.
   */
  FileManager.prototype.onToggleSidebarPress_ = function(event) {
    // Check if Enter (13) or Space (32) is pressed.
    if (event.keyCode == 13 || event.keyCode == 32)
      this.onToggleSidebar_(event);
  }

  FileManager.prototype.onToggleSidebar_ = function(event) {
    if (this.dialogContainer_.hasAttribute('sidebar')) {
      this.dialogContainer_.removeAttribute('sidebar');
    } else {
      this.dialogContainer_.setAttribute('sidebar', 'sidebar');
    }
    // TODO(dgozman): make table header css-resizable.
    setTimeout(this.onResize_.bind(this), 300);
  };

  FileManager.prototype.cancelSpinnerTimeout_ = function() {
    if (this.showSpinnerTimeout_) {
      clearTimeout(this.showSpinnerTimeout_);
      this.showSpinnerTimeout_ = null;
    }
  };

  FileManager.prototype.showSpinnerLater_ = function() {
    this.cancelSpinnerTimeout_();
    this.showSpinnerTimeout_ =
        setTimeout(this.showSpinner_.bind(this, true), 500);
  };

  FileManager.prototype.showSpinner_ = function(on) {
    this.cancelSpinnerTimeout_();
    this.spinner_.style.display = on ? '' : 'none';
  };

  FileManager.prototype.onNewFolderCommand_ = function(event) {
    var defaultName = str('DEFAULT_NEW_FOLDER_NAME');

    // Find a name that doesn't exist in the data model.
    var files = this.directoryModel_.fileList;
    var hash = {};
    for (var i = 0; i < files.length; i++) {
      var name = files.item(i).name;
      // Filtering names prevents from conflicts with prototype's names
      // and '__proto__'.
      if (name.substring(0, defaultName.length) == defaultName)
        hash[name] = 1;
    }

    var baseName = defaultName;
    var separator = '';
    var suffix = '';
    var index = '';

    function advance() {
      separator = ' (';
      suffix = ')';
      index++;
    }

    function current() {
      return baseName + separator + index + suffix;
    }

    // Accessing hasOwnProperty is safe since hash properties filtered.
    while (hash.hasOwnProperty(current())) {
      advance();
    }

    var self = this;
    var list = self.currentList_;
    function tryCreate() {
      self.directoryModel_.createDirectory(current(),
                                           onSuccess, onError);
    }

    function onSuccess(entry) {
      metrics.recordUserAction('CreateNewFolder');
      list.selectedItem = entry;
      self.initiateRename_();
    }

    function onError(error) {
      self.alert.show(strf('ERROR_CREATING_FOLDER', current(),
                           util.getFileErrorMnemonic(error.code)));
    }

    tryCreate();
  };

  FileManager.prototype.onDetailViewButtonClick_ = function(event) {
    this.setListType(FileManager.ListType.DETAIL);
  };

  FileManager.prototype.onThumbnailViewButtonClick_ = function(event) {
    this.setListType(FileManager.ListType.THUMBNAIL);
  };

  /**
   * KeyDown event handler for the document.
   */
  FileManager.prototype.onKeyDown_ = function(event) {
    if (event.srcElement === this.renameInput_) {
      // Ignore keydown handler in the rename input box.
      return;
    }

    switch (util.getKeyModifiers(event) + event.keyCode) {
      case 'Ctrl-190':  // Ctrl-. => Toggle filter files.
        var dm = this.directoryModel_;
        dm.filterHidden = !dm.filterHidden;
        event.preventDefault();
        return;

      case '27':  // Escape => Cancel dialog.
        if (this.copyManager_.getStatus().totalFiles != 0) {
          // If there is a copy in progress, ESC will cancel it.
          event.preventDefault();
          this.copyManager_.requestCancel();
          return;
        }

        if (this.butterTimer_) {
          // Allow the user to manually dismiss timed butter messages.
          event.preventDefault();
          this.hideButter();
          return;
        }

        if (this.dialogType_ != FileManager.DialogType.FULL_PAGE) {
          // If there is nothing else for ESC to do, then cancel the dialog.
          event.preventDefault();
          this.onCancel_();
        }
        break;
    }
  };

  /**
   * KeyDown event handler for the div.list-container element.
   */
  FileManager.prototype.onListKeyDown_ = function(event) {
    if (event.srcElement.tagName == 'INPUT') {
      // Ignore keydown handler in the rename input box.
      return;
    }

    var self = this;
    function handleCommand(name) {
      var command = self.commands_[name];
      command.disabled = !self.canExecute_(name);
      if (command.disabled)
        return;
      event.preventDefault();
      event.stopPropagation();
      command.execute();
    }

    switch (util.getKeyModifiers(event) + event.keyCode) {
      case 'Ctrl-32':  // Ctrl-Space => New Folder.
        handleCommand('newfolder');
        break;

      case 'Ctrl-88':  // Ctrl-X => Cut.
        handleCommand('cut');
        break;

      case 'Ctrl-67':  // Ctrl-C => Copy.
        handleCommand('copy');
        break;

      case 'Ctrl-86':  // Ctrl-V => Paste.
        handleCommand('paste');
        break;

      case 'Ctrl-69':  // Ctrl-E => Rename.
        handleCommand('rename');
        break;

      case '8':  // Backspace => Up one directory.
        event.preventDefault();
        var path = this.getCurrentDirectory();
        if (path && !DirectoryModel.isRootPath(path)) {
          var path = path.replace(/\/[^\/]+$/, '');
          this.directoryModel_.changeDirectory(path);
        }
        break;

      case '13':  // Enter => Change directory or perform default action.
        if (this.selection.totalCount == 1 &&
            this.selection.entries[0].isDirectory &&
            this.dialogType_ != FileManager.SELECT_FOLDER) {
          event.preventDefault();
          this.onDirectoryAction(this.selection.entries[0]);
        } else if (this.dispatchSelectionAction_()) {
          event.preventDefault();
        }
        break;

      case '46':  // Delete.
        handleCommand('delete');
        break;
    }
  };

  /**
   * KeyPress event handler for the div.list-container element.
   */
  FileManager.prototype.onListKeyPress_ = function(event) {
    if (event.srcElement.tagName == 'INPUT') {
      // Ignore keypress handler in the rename input box.
      return;
    }

    if (event.ctrlKey || event.metaKey || event.altKey)
      return;

    var now = new Date();
    var char = String.fromCharCode(event.charCode).toLowerCase();
    var text = now - this.textSearchState_.date > 1000 ? '' :
        this.textSearchState_.text;
    this.textSearchState_ = {text: text + char, date: now};

    this.doTextSearch_();
  };

  /**
   * Performs a 'text search' - selects a first list entry with name
   * starting with entered text (case-insensitive).
   */
  FileManager.prototype.doTextSearch_ = function() {
    var text = this.textSearchState_.text;
    if (!text)
      return;

    var dm = this.directoryModel_.fileList;
    for (var index = 0; index < dm.length; ++index) {
      var name = dm.item(index).name;
      if (name.substring(0, text.length).toLowerCase() == text) {
        this.currentList_.selectionModel.selectedIndexes = [index];
        return;
      }
    }

    this.textSearchState_.text = '';
  };

  /**
   * Handle a click of the cancel button.  Closes the window.
   * TODO(jamescook): Make unload handler work automatically, crbug.com/104811
   *
   * @param {Event} event The click event.
   */
  FileManager.prototype.onCancel_ = function(event) {
    chrome.fileBrowserPrivate.cancelDialog();
    this.onUnload_();
    window.close();
  };

  /**
   * Resolves selected file urls returned from an Open dialog.
   *
   * For gdata files this involves some special treatment.
   * Starts getting gdata files if needed.
   *
   * @param {Array.<string>} fileUrls
   * @param {function(Array.<string>)}
   */
  FileManager.prototype.resolveSelectResults_ = function(fileUrls, callback) {
    if (this.isOnGData()) {
      chrome.fileBrowserPrivate.getGDataFiles(
        fileUrls,
        function(localPaths) {
          fileUrls = [].concat(fileUrls);  // Clone the array.
          // localPath can be empty if the file is not present, which
          // can happen if the user specifies a new file name to save a
          // file on gdata.
          for (var i = 0; i != localPaths.length; i++) {
            if (localPaths[i]) {
              // Add "localPath" parameter to the gdata file URL.
              fileUrls[i] += '?localPath=' + encodeURIComponent(localPaths[i]);
            }
          }
          callback(fileUrls);
        });
    } else {
      callback(fileUrls);
    }
  },

  /**
   * Closes this modal dialog with some files selected.
   * TODO(jamescook): Make unload handler work automatically, crbug.com/104811
   * @param {Object} selection Contains urls, filterIndex and multiple fields.
   */
  FileManager.prototype.callSelectFilesApiAndClose_ = function(selection) {
    if (selection.multiple) {
      chrome.fileBrowserPrivate.selectFiles(selection.urls);
    } else {
      chrome.fileBrowserPrivate.selectFile(
          selection.urls[0], selection.filterIndex);
    }
    this.onUnload_();
    window.close();
  };

  /**
   * Tries to close this modal dialog with some files selected.
   * Performs preprocessing if needed (e.g. for GData).
   * @param {Object} selection Contains urls, filterIndex and multiple fields.
   */
  FileManager.prototype.selectFilesAndClose_ = function(selection) {
    if (!this.isOnGData()) {
      setTimeout(this.callSelectFilesApiAndClose_.bind(this, selection), 0);
      return;
    }

    var shade = this.document_.createElement('div');
    shade.className = 'shade';
    var footer = this.document_.querySelector('.dialog-footer');
    var progress = footer.querySelector('.progress-track');
    progress.style.width = '0%';
    var cancelled = false;

    var progressMap = {};
    var filesStarted = 0;
    var filesTotal = selection.urls.length;
    for (var index = 0; index < selection.urls.length; index++) {
      progressMap[selection.urls[index]] = -1;
    }
    var lastPercent = 0;
    var bytesTotal = 0;
    var bytesDone = 0;

    var onFileTransfersUpdated = function(statusList) {
      for (var index = 0; index < statusList.length; index++) {
        var status = statusList[index];
        var escaped = encodeURI(status.fileUrl);
        if (!(escaped in progressMap)) continue;
        if (status.total == -1) continue;

        var old = progressMap[escaped];
        if (old == -1) {
          // -1 means we don't know file size yet.
          bytesTotal += status.total;
          filesStarted++;
          old = 0;
        }
        bytesDone += status.processed - old;
        progressMap[escaped] = status.processed;
      }

      var percent = bytesTotal == 0 ? 0 : bytesDone / bytesTotal;
      // For files we don't have information about, assume the progress is zero.
      percent = percent * filesStarted / filesTotal * 100;
      // Do not decrease the progress. This may happen, if first downloaded
      // file is small, and the second one is large.
      lastPercent = Math.max(lastPercent, percent);
      progress.style.width = lastPercent + '%';
    }.bind(this);

    var setup = function() {
      this.document_.querySelector('.dialog-container').appendChild(shade);
      setTimeout(function() { shade.setAttribute('fadein', 'fadein') }, 100);
      footer.setAttribute('progress', 'progress');
      this.cancelButton_.removeEventListener('click', this.onCancelBound_);
      this.cancelButton_.addEventListener('click', onCancel);
      chrome.fileBrowserPrivate.onFileTransfersUpdated.addListener(
          onFileTransfersUpdated);
    }.bind(this);

    var cleanup = function() {
      shade.parentNode.removeChild(shade);
      footer.removeAttribute('progress');
      this.cancelButton_.removeEventListener('click', onCancel);
      this.cancelButton_.addEventListener('click', this.onCancelBound_);
      chrome.fileBrowserPrivate.onFileTransfersUpdated.removeListener(
          onFileTransfersUpdated);
    }.bind(this);

    var onCancel = function() {
      cancelled = true;
      // According to API cancel may fail, but there is no proper UI to reflect
      // this. So, we just silently assume that everything is cancelled.
      chrome.fileBrowserPrivate.cancelFileTransfers(
          selection.urls, function(response) {});
      cleanup();
    }.bind(this);

    var onResolved = function(resolvedUrls) {
      if (cancelled) return;
      cleanup();
      selection.urls = resolvedUrls;
      // Call next method on a timeout, as it's unsafe to
      // close a window from a callback.
      setTimeout(this.callSelectFilesApiAndClose_.bind(this, selection), 0);
    }.bind(this);

    var onGotProperties = function(properties) {
      for (var i = 0; i < properties.length; i++) {
        if (properties[i].isPresent) {
          // For files already in GCache, we don't get any transfer updates.
          filesTotal--;
        }
      }
      this.resolveSelectResults_(selection.urls, onResolved);
    }.bind(this);

    setup();
    chrome.fileBrowserPrivate.getGDataFileProperties(
        selection.urls, onGotProperties);
  };

  /**
   * Handle a click of the ok button.
   *
   * The ok button has different UI labels depending on the type of dialog, but
   * in code it's always referred to as 'ok'.
   *
   * @param {Event} event The click event.
   */
  FileManager.prototype.onOk_ = function(event) {
    var currentDirUrl = this.getCurrentDirectoryURL();

    if (currentDirUrl.charAt(currentDirUrl.length - 1) != '/')
      currentDirUrl += '/';

    var self = this;
    if (this.dialogType_ == FileManager.DialogType.SELECT_SAVEAS_FILE) {
      // Save-as doesn't require a valid selection from the list, since
      // we're going to take the filename from the text input.
      var filename = this.filenameInput_.value;
      if (!filename)
        throw new Error('Missing filename!');
      if (!this.validateFileName_(filename))
        return;

      var singleSelection = {
        urls: [currentDirUrl + encodeURIComponent(filename)],
        multiple: false,
        filterIndex: self.getSelectedFilterIndex_(filename)
      };

      function resolveCallback(victim) {
        if (victim instanceof FileError) {
          // File does not exist.
          self.selectFilesAndClose_(singleSelection);
          return;
        }

        if (victim.isDirectory) {
          // Do not allow to overwrite directory.
          self.alert.show(strf('DIRECTORY_ALREADY_EXISTS', filename));
        } else {
          self.confirm.show(strf('CONFIRM_OVERWRITE_FILE', filename),
                            function() {
                              // User selected Ok from the confirm dialog.
                              self.selectFilesAndClose_(singleSelection);
                            });
        }
      }

      this.resolvePath(this.getCurrentDirectory() + '/' + filename,
          resolveCallback, resolveCallback);
      return;
    }

    var files = [];
    var selectedIndexes = this.currentList_.selectionModel.selectedIndexes;

    // All other dialog types require at least one selected list item.
    // The logic to control whether or not the ok button is enabled should
    // prevent us from ever getting here, but we sanity check to be sure.
    if (!selectedIndexes.length)
      throw new Error('Nothing selected!');

    var dm = this.directoryModel_.fileList;
    for (var i = 0; i < selectedIndexes.length; i++) {
      var entry = dm.item(selectedIndexes[i]);
      if (!entry) {
        console.log('Error locating selected file at index: ' + i);
        continue;
      }

      files.push(currentDirUrl + encodeURIComponent(entry.name));
    }

    // Multi-file selection has no other restrictions.
    if (this.dialogType_ == FileManager.DialogType.SELECT_OPEN_MULTI_FILE) {
      var multipleSelection = {
        urls: files,
        multiple: true
      };
      this.selectFilesAndClose_(multipleSelection);
      return;
    }

    // Everything else must have exactly one.
    if (files.length > 1)
      throw new Error('Too many files selected!');

    var selectedEntry = dm.item(selectedIndexes[0]);

    if (this.dialogType_ == FileManager.DialogType.SELECT_FOLDER) {
      if (!selectedEntry.isDirectory)
        throw new Error('Selected entry is not a folder!');
    } else if (this.dialogType_ == FileManager.DialogType.SELECT_OPEN_FILE) {
      if (!selectedEntry.isFile)
        throw new Error('Selected entry is not a file!');
    }

    var singleSelection = {
      urls: [files[0]],
      multiple: false,
      filterIndex: this.getSelectedFilterIndex_(files[0])
    };
    this.selectFilesAndClose_(singleSelection);
  };

  /**
   * Verifies the user entered name for file or folder to be created or
   * renamed to. Name restrictions must correspond to File API restrictions
   * (see DOMFilePath::isValidPath). Curernt WebKit implementation is
   * out of date (spec is
   * http://dev.w3.org/2009/dap/file-system/file-dir-sys.html, 8.3) and going to
   * be fixed. Shows message box if the name is invalid.
   *
   * @param {name} name New file or folder name.
   * @param {function} opt_onDone Function to invoke when user closes the
   *    warning box or immediatelly if file name is correct.
   * @return {boolean} True if name is vaild.
   */
  FileManager.prototype.validateFileName_ = function(name, opt_onDone) {
    var onDone = opt_onDone || function() {};
    var msg;
    var testResult = /[\/\\\<\>\:\?\*\"\|]/.exec(name);
    if (testResult) {
      msg = strf('ERROR_INVALID_CHARACTER', testResult[0]);
    } else if (/^\s*$/i.test(name)) {
      msg = str('ERROR_WHITESPACE_NAME');
    } else if (/^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$/i.test(name)) {
      msg = str('ERROR_RESERVED_NAME');
    } else if (this.filterFiles_ && name[0] == '.') {
      msg = str('ERROR_HIDDEN_NAME');
    }

    if (msg) {
      this.alert.show(msg, onDone);
      return false;
    }

    onDone();
    return true;
  };

  /**
   * Start or stop monitoring free space depending on the new value of current
   * directory path. In case the space is low shows a warning box.
   * @param {string} currentPath New path to the current directory.
   */
  FileManager.prototype.checkFreeSpace_ = function(currentPath) {
    var dir = DirectoryModel.DOWNLOADS_DIRECTORY;
    if (currentPath.substr(1, dir.length) == dir) {
      // Setup short timeout if currentPath just changed.
      if (this.checkFreeSpaceTimer_)
        clearTimeout(this.checkFreeSpaceTimer_);

      // Right after changing directory the bottom pannel usually hides.
      // Simultaneous animation looks unpleasent. Also showing the warning is
      // lower priority task. So delay it.
      this.checkFreeSpaceTimer_ = setTimeout(doCheck, 500);
    } else {
      if (this.checkFreeSpaceTimer_) {
        clearTimeout(this.checkFreeSpaceTimer_);
        this.checkFreeSpaceTimer_ = 0;

        // Hide warning immediately.
        this.showLowDiskSpaceWarning_(false);
      }
    }

    var self = this;
    function doCheck() {
      var path = '/' + dir;
      self.resolvePath(path, function(downloadsDirEntry) {
        chrome.fileBrowserPrivate.getSizeStats(downloadsDirEntry.toURL(),
          function(sizeStats) {
            // sizeStats is undefined if some error occur.
            var lowDiskSpace = false;
            if (sizeStats && sizeStats.totalSizeKB > 0) {
              var ratio = sizeStats.remainingSizeKB / sizeStats.totalSizeKB;

              lowDiskSpace = ratio < 0.2;
              self.showLowDiskSpaceWarning_(lowDiskSpace);
            }

            // If disk space is low check it more often. User can delete files
            // manually and we should not bother her with warning in this case.
            setTimeout(doCheck, lowDiskSpace ? 1000 * 5 : 1000 * 30);
          });
      }, onError);

      function onError(err) {
        console.log('Error while checking free space: ' + err);
        setTimeout(doCheck, 1000 * 60);
      }
    }
  }
})();
