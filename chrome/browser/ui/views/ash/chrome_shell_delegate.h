// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_ASH_CHROME_SHELL_DELEGATE_H_
#define CHROME_BROWSER_UI_VIEWS_ASH_CHROME_SHELL_DELEGATE_H_
#pragma once

#include "ash/launcher/launcher_types.h"
#include "ash/shell_delegate.h"
#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

class StatusAreaHostAura;
class StatusAreaView;
class WindowPositioner;

namespace views {
class View;
}

class ChromeShellDelegate : public ash::ShellDelegate,
                            public content::NotificationObserver {
 public:
  ChromeShellDelegate();
  virtual ~ChromeShellDelegate();

  static ChromeShellDelegate* instance() { return instance_; }

  StatusAreaHostAura* status_area_host() {
    return status_area_host_.get();
  }

  StatusAreaView* GetStatusArea();

  WindowPositioner* window_positioner() { return window_positioner_.get(); }

  // ash::ShellDelegate overrides;
  virtual views::Widget* CreateStatusArea() OVERRIDE;
  virtual bool IsUserLoggedIn() OVERRIDE;
  virtual void LockScreen() OVERRIDE;
  virtual void UnlockScreen() OVERRIDE;
  virtual bool IsScreenLocked() const OVERRIDE;
  virtual void Exit() OVERRIDE;
  virtual void NewWindow(bool is_incognito) OVERRIDE;
  virtual ash::AppListViewDelegate* CreateAppListViewDelegate() OVERRIDE;
  virtual void StartPartialScreenshot(
      ash::ScreenshotDelegate* screenshot_delegate) OVERRIDE;
  virtual ash::LauncherDelegate* CreateLauncherDelegate(
      ash::LauncherModel* model) OVERRIDE;
  virtual ash::SystemTrayDelegate* CreateSystemTrayDelegate(
      ash::SystemTray* tray) OVERRIDE;
  virtual ash::UserWallpaperDelegate* CreateUserWallpaperDelegate() OVERRIDE;

  // content::NotificationObserver override:
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

 private:
  static ChromeShellDelegate* instance_;

  content::NotificationRegistrar registrar_;

  scoped_ptr<StatusAreaHostAura> status_area_host_;
  scoped_ptr<WindowPositioner> window_positioner_;

  DISALLOW_COPY_AND_ASSIGN(ChromeShellDelegate);
};

#endif  // CHROME_BROWSER_UI_VIEWS_ASH_CHROME_SHELL_DELEGATE_H_
