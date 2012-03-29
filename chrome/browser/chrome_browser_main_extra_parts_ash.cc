// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chrome_browser_main_extra_parts_ash.h"

#include "ash/accelerators/accelerator_controller.h"
#include "ash/ash_switches.h"
#include "ash/shell.h"
#include "base/command_line.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/views/ash/caps_lock_handler.h"
#include "chrome/browser/ui/views/ash/chrome_shell_delegate.h"
#include "chrome/browser/ui/views/ash/screen_orientation_listener.h"
#include "chrome/browser/ui/views/ash/screenshot_taker.h"
#include "chrome/browser/ui/views/ash/status_area_host_aura.h"
#include "ui/aura/env.h"
#include "ui/aura/aura_switches.h"
#include "ui/aura/monitor_manager.h"
#include "ui/aura/root_window.h"
#include "ui/gfx/compositor/compositor_setup.h"

#if defined(OS_CHROMEOS)
#include "base/chromeos/chromeos_version.h"
#include "chrome/browser/ui/views/ash/brightness_controller_chromeos.h"
#include "chrome/browser/ui/views/ash/ime_controller_chromeos.h"
#include "chrome/browser/ui/views/ash/volume_controller_chromeos.h"
#include "chrome/browser/chromeos/input_method/input_method_manager.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#endif

ChromeBrowserMainExtraPartsAsh::ChromeBrowserMainExtraPartsAsh()
    : ChromeBrowserMainExtraParts() {
}

void ChromeBrowserMainExtraPartsAsh::PreProfileInit() {
#if defined(OS_CHROMEOS)
  if (base::chromeos::IsRunningOnChromeOS() ||
      CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kAuraHostWindowUseFullscreen)) {
    aura::MonitorManager::set_use_fullscreen_host_window(true);
    aura::RootWindow::set_hide_host_cursor(true);
    // Hide the mouse cursor completely at boot.
    if (!chromeos::UserManager::Get()->IsUserLoggedIn())
      ash::Shell::set_initially_hide_cursor(true);
  }
#endif

  // Shell takes ownership of ChromeShellDelegate.
  ash::Shell* shell = ash::Shell::CreateInstance(new ChromeShellDelegate);
  shell->accelerator_controller()->SetScreenshotDelegate(
      scoped_ptr<ash::ScreenshotDelegate>(new ScreenshotTaker).Pass());
#if defined(OS_CHROMEOS)
  shell->accelerator_controller()->SetBrightnessControlDelegate(
      scoped_ptr<ash::BrightnessControlDelegate>(
          new BrightnessController).Pass());
  chromeos::input_method::XKeyboard* xkeyboard =
      chromeos::input_method::InputMethodManager::GetInstance()->GetXKeyboard();
  shell->accelerator_controller()->SetCapsLockDelegate(
      scoped_ptr<ash::CapsLockDelegate>(new CapsLockHandler(xkeyboard)).Pass());
  shell->accelerator_controller()->SetImeControlDelegate(
      scoped_ptr<ash::ImeControlDelegate>(new ImeController).Pass());
  shell->accelerator_controller()->SetVolumeControlDelegate(
      scoped_ptr<ash::VolumeControlDelegate>(new VolumeController).Pass());

  if (!CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kDisableZeroBrowsersOpenForTests))
    BrowserList::StartKeepAlive();
#endif

  // Make sure the singleton ScreenOrientationListener object is created.
  ScreenOrientationListener::GetInstance();
}

void ChromeBrowserMainExtraPartsAsh::PostProfileInit() {
  // Add the status area buttons after Profile has been initialized.
  if (CommandLine::ForCurrentProcess()->HasSwitch(
        ash::switches::kDisableAshUberTray)) {
    ChromeShellDelegate::instance()->status_area_host()->AddButtons();
  }
}

void ChromeBrowserMainExtraPartsAsh::PostMainMessageLoopRun() {
  ash::Shell::DeleteInstance();
}
