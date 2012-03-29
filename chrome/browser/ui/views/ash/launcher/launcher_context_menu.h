// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_ASH_LAUNCHER_LAUNCHER_CONTEXT_MENU_H_
#define CHROME_BROWSER_UI_VIEWS_ASH_LAUNCHER_LAUNCHER_CONTEXT_MENU_H_
#pragma once

#include "ash/launcher/launcher_types.h"
#include "base/basictypes.h"
#include "chrome/browser/ui/views/ash/launcher/shelf_auto_hide_menu.h"
#include "ui/base/models/simple_menu_model.h"

namespace gfx {
class Point;
}

namespace views {
class MenuRunner;
class View;
}

class ChromeLauncherDelegate;

// Context menu shown for a launcher item.
class LauncherContextMenu : public ui::SimpleMenuModel,
                            public ui::SimpleMenuModel::Delegate {
 public:
  // |item| is NULL if the context menu is for the launcher (the user right
  // |clicked on an area with no icons).
  LauncherContextMenu(ChromeLauncherDelegate* delegate,
                      const ash::LauncherItem* item);
  virtual ~LauncherContextMenu();

  // ID of the item we're showing the context menu for.
  ash::LauncherID id() const { return item_.id; }

  // ui::SimpleMenuModel::Delegate overrides:
  virtual bool IsCommandIdChecked(int command_id) const OVERRIDE;
  virtual bool IsCommandIdEnabled(int command_id) const OVERRIDE;
  virtual bool GetAcceleratorForCommandId(
      int command_id,
      ui::Accelerator* accelerator) OVERRIDE;
  virtual void ExecuteCommand(int command_id) OVERRIDE;

 private:
  enum MenuItem {
    MENU_OPEN,
    MENU_CLOSE,
    MENU_PIN,
    LAUNCH_TYPE_REGULAR_TAB,
    LAUNCH_TYPE_WINDOW,
    MENU_AUTO_HIDE,
  };

  // Does |item_| represent a valid item? See description of constructor for
  // details on why it may not be valid.
  bool is_valid_item() const { return item_.id != 0; }

  ChromeLauncherDelegate* delegate_;
  ash::LauncherItem item_;
  ShelfAutoHideMenu shelf_menu_;

  DISALLOW_COPY_AND_ASSIGN(LauncherContextMenu);
};

#endif  // CHROME_BROWSER_UI_VIEWS_ASH_LAUNCHER_LAUNCHER_CONTEXT_MENU_H_
