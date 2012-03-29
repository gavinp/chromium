// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var scriptList;

// Mapping between font list ids and the generic family setting they
// represent.
var genericFamilies = [
  { fontList: 'standardFontList', name: 'standard' },
  { fontList: 'serifFontList', name: 'serif' },
  { fontList: 'sansSerifFontList', name: 'sansserif' },
  { fontList: 'fixedFontList', name: 'fixed' }
];

var DEFAULT_SCRIPT = 'Qaaa';

function getSelectedScript() {
  return scriptList.options[scriptList.selectedIndex].value;
}

function getSelectedFont(fontList) {
  return fontList.options[fontList.selectedIndex].value;
}

// Populates the font lists with the list of system fonts from |fonts|.
function populateLists(fonts) {
  for (var i = 0; i < genericFamilies.length; i++) {
    var list = document.getElementById(genericFamilies[i].fontList);

    // Add special "(none)" item to indicate fallback to the non-per-script
    // font setting. The Font Settings API uses the empty string to indicate
    // fallback.
    var noneItem = document.createElement('option');
    noneItem.value = '';
    noneItem.text = '(none)';
    list.add(noneItem);

    for (var j = 0; j < fonts.length; j++) {
      var item = document.createElement('option');
      item.value = fonts[j].fontName;
      item.text = fonts[j].localizedName;
      list.add(item);
    }
  }

  updateListSelections();
}

// Returns a function that updates the font setting for |genericFamily|
// to match the selected value in |fontList|. It can be used as an event
// handler for selection changes in |fontList|.
function getFontChangeHandler(fontList, genericFamily) {
  return function() {
    var script = getSelectedScript();
    var font = getSelectedFont(fontList);

    var details = {};
    details.genericFamily = genericFamily;
    details.fontName = font;
    if (script != DEFAULT_SCRIPT)
      details.script = script;

    chrome.experimental.fontSettings.setFontName(details);
  };
}

// Sets the selected value of |fontList| to |fontName|.
function setSelectedFont(fontList, fontName) {
  var script = getSelectedScript();

  for (var i = 0; i < fontList.length; i++) {
    if (fontName == fontList.options[i].value) {
      fontList.selectedIndex = i;
      break;
    }
  }
  if (i == fontList.length) {
    console.warn("font '" + fontName + "' for " + fontList.id + ' for ' +
        script + ' is not on the system');
  }
}

// Returns a callback function that sets the selected value of |list| to the
// font returned from |chrome.experimental.fontSettings.getFontName|.
function getFontNameHandler(list) {
  return function(details) {
    setSelectedFont(list, details.fontName);
  };
}

// Sets the selected value of each font list to the current font setting.
function updateListSelections() {
  var script = getSelectedScript();

  for (var i = 0; i < genericFamilies.length; i++) {
    var list = document.getElementById(genericFamilies[i].fontList);
    var family = genericFamilies[i].name;

    var details = {};
    details.genericFamily = family;
    if (script != DEFAULT_SCRIPT)
      details.script = script;

    chrome.experimental.fontSettings.getFontName(details,
                                                 getFontNameHandler(list));
  }
}

function defaultFontSizeChanged() {
  var defaultFontSizeInput = document.getElementById('defaultFontSize');
  var pixelSize = parseInt(defaultFontSizeInput.value);
  if (!isNaN(pixelSize)) {
    chrome.experimental.fontSettings.setDefaultFontSize({
      pixelSize: pixelSize
    });
  }
}

function init() {
  scriptList = document.getElementById('scriptList');
  scriptList.addEventListener('change', updateListSelections);

  // Populate the font lists.
  chrome.experimental.fontSettings.getFontList(populateLists);

  // Add change handlers to the font lists.
  for (var i = 0; i < genericFamilies.length; i++) {
    var list = document.getElementById(genericFamilies[i].fontList);
    var handler = getFontChangeHandler(list, genericFamilies[i].name);
    list.addEventListener('change', handler);
  }

  var defaultFontSizeInput = document.getElementById('defaultFontSize');
  chrome.experimental.fontSettings.getDefaultFontSize({}, function(details) {
    defaultFontSizeInput.value = details.pixelSize;
  });
  defaultFontSizeInput.addEventListener('change', defaultFontSizeChanged);
}

document.addEventListener('DOMContentLoaded', init);
