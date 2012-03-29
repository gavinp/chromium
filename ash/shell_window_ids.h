// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SHELL_WINDOW_IDS_H_
#define ASH_SHELL_WINDOW_IDS_H_
#pragma once

// Declarations of ids of special shell windows.

namespace ash {

namespace internal {

// A higher-level container that holds all of the containers stacked below
// kShellWindowId_LockScreenContainer.  Only used by PowerButtonController for
// animating lower-level containers.
const int kShellWindowId_NonLockScreenContainersContainer = 0;

// A higher-level container that holds containers that hold lock-screen
// windows.  Only used by PowerButtonController for animating lower-level
// containers.
const int kShellWindowId_LockScreenContainersContainer = 1;

// A higher-level container that holds containers that hold lock-screen-related
// windows (which we want to display while the screen is locked; effectively
// containers stacked above kShellWindowId_LockSystemModalContainer).  Only used
// by PowerButtonController for animating lower-level containers.
const int kShellWindowId_LockScreenRelatedContainersContainer = 2;

// A container used for windows of WINDOW_TYPE_CONTROL that have no parent.
// This container is not visible.
const int kShellWindowId_UnparentedControlContainer = 3;

// The desktop background window.
const int kShellWindowId_DesktopBackgroundContainer = 4;

// The container for standard top-level windows.
const int kShellWindowId_DefaultContainer = 5;

// The container for top-level windows with the 'always-on-top' flag set.
const int kShellWindowId_AlwaysOnTopContainer = 6;

// The container for panel windows.
const int kShellWindowId_PanelContainer = 7;

// The container for the launcher.
const int kShellWindowId_LauncherContainer = 8;

// The container for the app list.
const int kShellWindowId_AppListContainer = 9;

// The container for user-specific modal windows.
const int kShellWindowId_SystemModalContainer = 10;

// The container for the lock screen.
const int kShellWindowId_LockScreenContainer = 11;

// The container for the lock screen modal windows.
const int kShellWindowId_LockSystemModalContainer = 12;

// The container for the status area.
const int kShellWindowId_StatusContainer = 13;

// The container for menus.
const int kShellWindowId_MenuContainer = 14;

// The container for drag/drop images and tooltips.
const int kShellWindowId_DragImageAndTooltipContainer = 15;

// The container for bubbles briefly overlaid onscreen to show settings changes
// (volume, brightness, etc.).
const int kShellWindowId_SettingBubbleContainer = 16;

// The container for special components overlaid onscreen, such as the
// region selector for partial screenshots.
const int kShellWindowId_OverlayContainer = 17;

}  // namespace internal

}  // namespace ash


#endif  // ASH_SHELL_WINDOW_IDS_H_
