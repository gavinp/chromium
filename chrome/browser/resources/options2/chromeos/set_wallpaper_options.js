// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options', function() {

  var OptionsPage = options.OptionsPage;
  var UserImagesGrid = options.UserImagesGrid;

  /////////////////////////////////////////////////////////////////////////////
  // SetWallpaperOptions class:

  /**
   * Encapsulated handling of ChromeOS set wallpaper options page.
   * @constructor
   */
  function SetWallpaperOptions() {
    OptionsPage.call(
        this,
        'setWallpaper',
        localStrings.getString('setWallpaper'),
        'set-wallpaper-page');
  }

  cr.addSingletonGetter(SetWallpaperOptions);

  SetWallpaperOptions.prototype = {
    // Inherit SetWallpaperOptions from OptionsPage.
    __proto__: options.OptionsPage.prototype,

    /**
     * Initializes SetWallpaperOptions page.
     */
    initializePage: function() {
      // Call base class implementation to start preferences initialization.
      OptionsPage.prototype.initializePage.call(this);

      var wallpaperGrid = $('wallpaper-grid');
      UserImagesGrid.decorate(wallpaperGrid);

      wallpaperGrid.addEventListener('change',
                                     this.handleImageSelected_.bind(this));
      wallpaperGrid.addEventListener('dblclick',
                                     this.handleImageDblClick_.bind(this));
      wallpaperGrid.addEventListener('activate',
                                     function() { OptionsPage.closeOverlay() });

      $('set-wallpaper-overlay-confirm').onclick = function() {
        OptionsPage.closeOverlay();
      };

      // @type {Array.<author: string, url: string, website: string>}
      this.wallpapers_ = [];

      chrome.send('onSetWallpaperPageInitialized');
    },

    /**
     * Called right after the page has been shown to user.
     */
    didShowPage: function() {
      $('wallpaper-grid').updateAndFocus();
      // A quick hack to fix issue 118472. This is a general problem of list
      // control and options overlay.
      // TODO(bshe): Remove this hack when we fixed the general problem which
      // tracked in issue 118829.
      $('wallpaper-grid').redraw();
      chrome.send('onSetWallpaperPageShown');
    },

    /**
     * Called right before the page is hidden.
     */
    willHidePage: function() {
      var wallpaperGrid = $('wallpaper-grid');
      wallpaperGrid.blur();
    },

    /**
     * Handles image selection change.
     * @private
     */
    handleImageSelected_: function() {
      var wallpaperGrid = $('wallpaper-grid');
      var index = wallpaperGrid.selectionModel.selectedIndex;
      $('author-name').innerText = this.wallpapers_[index].author;
      $('author-website').innerText = this.wallpapers_[index].website;

      // Ignore deselection, selection change caused by program itself and
      // selection of one of the action buttons.
      if (index != -1 &&
          !wallpaperGrid.inProgramSelection) {
        chrome.send('selectWallpaper', [index.toString()]);
      }
    },

    /**
     * Handles double click on the image grid.
     * @param {Event} e Double click Event.
     */
    handleImageDblClick_: function(e) {
      // Close page unless the click target is the grid itself.
      if (e.target instanceof HTMLImageElement)
        OptionsPage.closeOverlay();
    },

    /**
     * Selects user image with the given index.
     * @param {int} index index of the image to select.
     * @private
     */
    setSelectedImage_: function(index) {
      var wallpaperGrid = $('wallpaper-grid');
      wallpaperGrid.selectedItemIndex = index;
      $('author-name').innerText = this.wallpapers_[index].author;
      $('author-website').innerText = this.wallpapers_[index].website;
    },

    /**
     * Appends default images to the image grid. Should only be called once.
     * @param {Array.<{author: string, url: string, website: string}>}
     * wallpapers An array of wallpaper objects.
     * @private
     */
    setDefaultImages_: function(wallpapers) {
      var wallpaperGrid = $('wallpaper-grid');
      // TODO(bshe): Ideally we should save author and website with the actual
      // image (URL) and not use index related storage. This way this data is
      // stored in one place rather than depending on the index to be
      // consistent.
      for (var i = 0, wallpaper; wallpaper = wallpapers[i]; i++) {
        this.wallpapers_.push(wallpaper);
        wallpaperGrid.addItem(wallpaper.url);
      }
    },

  };

  // Forward public APIs to private implementations.
  [
    'setDefaultImages',
    'setSelectedImage'
  ].forEach(function(name) {
    SetWallpaperOptions[name] = function() {
      var instance = SetWallpaperOptions.getInstance();
      return instance[name + '_'].apply(instance, arguments);
    };
  });

  // Export
  return {
    SetWallpaperOptions: SetWallpaperOptions
  };

});

