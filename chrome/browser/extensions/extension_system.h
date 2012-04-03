// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_SYSTEM_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_SYSTEM_H_
#pragma once

#include <string>

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/profiles/profile_keyed_service.h"
#include "chrome/common/extensions/extension_constants.h"

class Extension;
class ExtensionDevToolsManager;
class ExtensionEventRouter;
class ExtensionInfoMap;
class ExtensionMessageService;
class ExtensionNavigationObserver;
class ExtensionPrefs;
class ExtensionPrefValueMap;
class ExtensionProcessManager;
class ExtensionService;
class LazyBackgroundTaskQueue;
class Profile;
class UserScriptMaster;

// The ExtensionSystem manages the creation and destruction of services
// related to extensions. Most objects are shared between normal
// and incognito Profiles, except as called out in comments.
// This interface supports using TestExtensionSystem for TestingProfiles
// that don't want all of the extensions baggage in their tests.
class ExtensionSystem : public ProfileKeyedService {
 public:
  ExtensionSystem();
  virtual ~ExtensionSystem();

  // ProfileKeyedService implementation.
  virtual void Shutdown() OVERRIDE {}

  // Initializes extensions machinery.
  // Component extensions are always enabled, external and user extensions
  // are controlled by |extensions_enabled|.
  virtual void Init(bool extensions_enabled) = 0;

  // The ExtensionService is created at startup.
  virtual ExtensionService* extension_service() = 0;

  //  The ExtensionDevToolsManager is created at startup.
  virtual ExtensionDevToolsManager* devtools_manager() = 0;

  // The UserScriptMaster is created at startup.
  virtual UserScriptMaster* user_script_master() = 0;

  // The ExtensionProcessManager is created at startup.
  virtual ExtensionProcessManager* process_manager() = 0;

  // Returns the IO-thread-accessible extension data.
  virtual ExtensionInfoMap* info_map() = 0;

  // The LazyBackgroundTaskQueue is created at startup.
  virtual LazyBackgroundTaskQueue* lazy_background_task_queue() = 0;

  // The ExtensionMessageService is created at startup.
  virtual ExtensionMessageService* message_service() = 0;

  // The ExtensionEventRouter is created at startup.
  virtual ExtensionEventRouter* event_router() = 0;

  // Called by the ExtensionService that lives in this system. Gives the
  // info map a chance to react to the load event before the EXTENSION_LOADED
  // notification has fired. The purpose for handling this event first is to
  // avoid race conditions by making sure URLRequestContexts learn about new
  // extensions before anything else needs them to know.
  virtual void RegisterExtensionWithRequestContexts(
      const Extension* extension) {}

  // Called by the ExtensionService that lives in this system. Lets the
  // info map clean up its RequestContexts once all the listeners to the
  // EXTENSION_UNLOADED notification have finished running.
  virtual void UnregisterExtensionWithRequestContexts(
      const std::string& extension_id,
      const extension_misc::UnloadedExtensionReason reason) {}
};

// The ExtensionSystem for ProfileImpl and OffTheRecordProfileImpl.
// Implementation details: non-shared services are owned by
// ExtensionSystemImpl, a ProfileKeyedService with separate incognito
// instances. A private Shared class (also a ProfileKeyedService,
// but with a shared instance for incognito) keeps the common services.
class ExtensionSystemImpl : public ExtensionSystem {
 public:
  explicit ExtensionSystemImpl(Profile* profile);
  virtual ~ExtensionSystemImpl();

  // ProfileKeyedService implementation.
  virtual void Shutdown() OVERRIDE;

  virtual void Init(bool extensions_enabled) OVERRIDE;

  virtual ExtensionService* extension_service() OVERRIDE;  // shared
  virtual UserScriptMaster* user_script_master() OVERRIDE;  // shared
  virtual ExtensionDevToolsManager* devtools_manager() OVERRIDE;
  virtual ExtensionProcessManager* process_manager() OVERRIDE;
  virtual LazyBackgroundTaskQueue* lazy_background_task_queue()
      OVERRIDE;  // shared
  virtual ExtensionInfoMap* info_map() OVERRIDE;  // shared
  virtual ExtensionMessageService* message_service() OVERRIDE;  // shared
  virtual ExtensionEventRouter* event_router() OVERRIDE;  // shared

  virtual void RegisterExtensionWithRequestContexts(
      const Extension* extension) OVERRIDE;

  virtual void UnregisterExtensionWithRequestContexts(
      const std::string& extension_id,
      const extension_misc::UnloadedExtensionReason reason) OVERRIDE;

 private:
  friend class ExtensionSystemSharedFactory;

  // Owns the Extension-related systems that have a single instance
  // shared between normal and incognito profiles.
  class Shared : public ProfileKeyedService {
   public:
    explicit Shared(Profile* profile);
    virtual ~Shared();

    // Initialization takes place in phases.
    virtual void InitPrefs();
    void InitInfoMap();
    void Init(bool extensions_enabled);

    ExtensionService* extension_service();
    UserScriptMaster* user_script_master();
    ExtensionInfoMap* info_map();
    LazyBackgroundTaskQueue* lazy_background_task_queue();
    ExtensionMessageService* message_service();
    ExtensionEventRouter* event_router();

   private:
    Profile* profile_;

    // The services that are shared between normal and incognito profiles.

    // Keep extension_prefs_ on top of extension_service_ because the latter
    // maintains a pointer to the first and shall be destructed first.
    scoped_ptr<ExtensionPrefs> extension_prefs_;
    scoped_ptr<ExtensionService> extension_service_;
    scoped_refptr<UserScriptMaster> user_script_master_;
    // extension_info_map_ needs to outlive extension_process_manager_.
    scoped_refptr<ExtensionInfoMap> extension_info_map_;
    // This is a dependency of ExtensionMessageService and ExtensionEventRouter.
    scoped_ptr<LazyBackgroundTaskQueue> lazy_background_task_queue_;
    scoped_ptr<ExtensionMessageService> extension_message_service_;
    scoped_ptr<ExtensionEventRouter> extension_event_router_;
    scoped_ptr<ExtensionNavigationObserver> extension_navigation_observer_;
  };

  Profile* profile_;

  Shared* shared_;

  // The services that have their own instances in incognito.
  scoped_refptr<ExtensionDevToolsManager> extension_devtools_manager_;
  // |extension_process_manager_| must be destroyed before the Profile's
  // |io_data_|. While |extension_process_manager_| still lives, we handle
  // incoming resource requests from extension processes and those require
  // access to the ResourceContext owned by |io_data_|.
  scoped_ptr<ExtensionProcessManager> extension_process_manager_;
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_SYSTEM_H_
