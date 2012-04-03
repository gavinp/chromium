// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_TEST_EXTENSION_SYSTEM_H_
#define CHROME_BROWSER_EXTENSIONS_TEST_EXTENSION_SYSTEM_H_
#pragma once

#include "chrome/browser/extensions/extension_system.h"

class CommandLine;
class FilePath;

// Test ExtensionSystem, for use with TestingProfile.
class TestExtensionSystem : public ExtensionSystem {
 public:
  explicit TestExtensionSystem(Profile* profile);
  virtual ~TestExtensionSystem();

  // ProfileKeyedService implementation.
  virtual void Shutdown() OVERRIDE;

  // Creates an ExtensionService initialized with the testing profile and
  // returns it.
  ExtensionService* CreateExtensionService(const CommandLine* command_line,
                                           const FilePath& install_directory,
                                           bool autoupdate_enabled);

  // Creates an ExtensionProcessManager. If not invoked, the
  // ExtensionProcessManager is NULL.
  void CreateExtensionProcessManager();

  virtual void Init(bool extensions_enabled) OVERRIDE {}
  virtual ExtensionService* extension_service() OVERRIDE;
  void SetExtensionService(ExtensionService* service);
  virtual UserScriptMaster* user_script_master() OVERRIDE;
  virtual ExtensionDevToolsManager* devtools_manager() OVERRIDE;
  virtual ExtensionProcessManager* process_manager() OVERRIDE;
  virtual ExtensionInfoMap* info_map() OVERRIDE;
  virtual LazyBackgroundTaskQueue* lazy_background_task_queue() OVERRIDE;
  virtual ExtensionMessageService* message_service() OVERRIDE;
  virtual ExtensionEventRouter* event_router() OVERRIDE;

  // Factory method for tests to use with SetTestingProfile.
  static ProfileKeyedService* Build(Profile* profile);

 private:
  Profile* profile_;

  scoped_ptr<ExtensionPrefValueMap> extension_pref_value_map_;
  // The Extension Preferences. Only created if CreateExtensionService is
  // invoked.
  scoped_ptr<ExtensionPrefs> extension_prefs_;
  scoped_ptr<ExtensionService> extension_service_;
  scoped_ptr<ExtensionProcessManager> extension_process_manager_;
};

#endif  // CHROME_BROWSER_EXTENSIONS_TEST_EXTENSION_SYSTEM_H_
