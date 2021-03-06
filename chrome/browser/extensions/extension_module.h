// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_MODULE_H__
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_MODULE_H__
#pragma once

#include "chrome/browser/extensions/extension_function.h"

class ExtensionPrefs;

class ExtensionModuleEventRouter {
 public:
  // Dispatches the onInstalled event to the given extension.
  static void DispatchOnInstalledEvent(Profile* profile,
                                       const Extension* extension);
};

class SetUpdateUrlDataFunction : public SyncExtensionFunction {
 protected:
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION_NAME("extension.setUpdateUrlData");

 private:
  ExtensionPrefs* extension_prefs();
};

class IsAllowedIncognitoAccessFunction : public SyncExtensionFunction {
 protected:
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION_NAME("extension.isAllowedIncognitoAccess");
};

class IsAllowedFileSchemeAccessFunction : public SyncExtensionFunction {
 protected:
  virtual bool RunImpl() OVERRIDE;
  DECLARE_EXTENSION_FUNCTION_NAME("extension.isAllowedFileSchemeAccess");
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_MODULE_H__
