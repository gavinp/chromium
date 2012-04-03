// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/test/test_shell_delegate.h"

#include <algorithm>

#include "ash/screenshot_delegate.h"
#include "ash/shell.h"
#include "ash/shell_window_ids.h"
#include "grit/ui_resources.h"
#include "ui/aura/window.h"

namespace ash {
namespace test {

TestShellDelegate::TestShellDelegate() : locked_(false) {
}

TestShellDelegate::~TestShellDelegate() {
}

views::Widget* TestShellDelegate::CreateStatusArea() {
  return NULL;
}

bool TestShellDelegate::IsUserLoggedIn() {
  return true;
}

void TestShellDelegate::LockScreen() {
  locked_ = true;
}

void TestShellDelegate::UnlockScreen() {
  locked_ = false;
}

bool TestShellDelegate::IsScreenLocked() const {
  return locked_;
}

void TestShellDelegate::Exit() {
}

void TestShellDelegate::NewWindow(bool incognito) {
}

AppListViewDelegate* TestShellDelegate::CreateAppListViewDelegate() {
  return NULL;
}

void TestShellDelegate::StartPartialScreenshot(
    ScreenshotDelegate* screenshot_delegate) {
  if (screenshot_delegate)
    screenshot_delegate->HandleTakePartialScreenshot(NULL, gfx::Rect());
}

LauncherDelegate* TestShellDelegate::CreateLauncherDelegate(
    ash::LauncherModel* model) {
  return NULL;
}

SystemTrayDelegate* TestShellDelegate::CreateSystemTrayDelegate(
    SystemTray* tray) {
  return NULL;
}

UserWallpaperDelegate* TestShellDelegate::CreateUserWallpaperDelegate() {
  return NULL;
}

}  // namespace test
}  // namespace ash
