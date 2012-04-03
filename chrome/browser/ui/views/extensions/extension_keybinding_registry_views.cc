// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/extensions/extension_keybinding_registry_views.h"

#include "chrome/browser/extensions/extension_browser_event_router.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/extension.h"
#include "ui/views/focus/focus_manager.h"

ExtensionKeybindingRegistryViews::ExtensionKeybindingRegistryViews(
    Profile* profile, views::FocusManager* focus_manager)
    : ExtensionKeybindingRegistry(profile),
      profile_(profile),
      focus_manager_(focus_manager) {
  Init();
}

ExtensionKeybindingRegistryViews::~ExtensionKeybindingRegistryViews() {
  EventTargets::const_iterator iter;
  for (iter = event_targets_.begin(); iter != event_targets_.end(); ++iter)
    focus_manager_->UnregisterAccelerator(iter->first, this);
}

void ExtensionKeybindingRegistryViews::AddExtensionKeybinding(
    const Extension* extension) {
  // Add all the keybindings (except pageAction and browserAction, which are
  // handled elsewhere).
  const Extension::CommandMap& commands = extension->named_commands();
  Extension::CommandMap::const_iterator iter = commands.begin();
  for (; iter != commands.end(); ++iter) {
    event_targets_[iter->second.accelerator()] =
        std::make_pair(extension->id(), iter->second.command_name());
    focus_manager_->RegisterAccelerator(
        iter->second.accelerator(),
        ui::AcceleratorManager::kHighPriority, this);
  }
}

void ExtensionKeybindingRegistryViews::RemoveExtensionKeybinding(
    const Extension* extension) {
  EventTargets::const_iterator iter;
  for (iter = event_targets_.begin(); iter != event_targets_.end(); ++iter) {
    if (iter->second.first != extension->id())
      continue;  // Not the extension we asked for.

    focus_manager_->UnregisterAccelerator(iter->first, this);
  }
}

bool ExtensionKeybindingRegistryViews::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  ExtensionService* service = profile_->GetExtensionService();

  EventTargets::iterator it = event_targets_.find(accelerator);
  if (it == event_targets_.end()) {
    NOTREACHED();  // Shouldn't get this event for something not registered.
    return false;
  }

  service->browser_event_router()->CommandExecuted(
      profile_, it->second.first, it->second.second);

  return true;
}

bool ExtensionKeybindingRegistryViews::CanHandleAccelerators() const {
  return true;
}
