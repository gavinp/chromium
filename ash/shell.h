// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SHELL_H_
#define ASH_SHELL_H_
#pragma once

#include <utility>
#include <vector>

#include "ash/ash_export.h"
#include "ash/wm/shelf_auto_hide_behavior.h"
#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/observer_list.h"
#include "ui/gfx/size.h"
#include "ui/gfx/insets.h"

class CommandLine;
class SkBitmap;

namespace aura {
class EventFilter;
class Monitor;
class RootWindow;
class Window;
}
namespace gfx {
class Point;
class Rect;
}
namespace ui {
class Layer;
}
namespace views {
class NonClientFrameView;
class Widget;
}

namespace ash {

class AcceleratorController;
class DesktopBackgroundController;
class Launcher;
class NestedDispatcherController;
class PowerButtonController;
class ScreenAsh;
class ShellDelegate;
class ShellObserver;
class SystemTrayDelegate;
class SystemTray;
class UserWallpaperDelegate;
class VideoDetector;
class WindowCycleController;

namespace internal {
class ActivationController;
class AcceleratorFilter;
class AppList;
class DragDropController;
class EventClientImpl;
class FocusCycler;
class InputMethodEventFilter;
class KeyRewriterEventFilter;
class MonitorController;
class PartialScreenshotEventFilter;
class ResizeShadowController;
class RootWindowEventFilter;
class RootWindowLayoutManager;
class ShadowController;
class ShelfLayoutManager;
class StackingController;
class TooltipController;
class VisibilityController;
class WindowModalityController;
class WorkspaceController;
}

// Shell is a singleton object that presents the Shell API and implements the
// RootWindow's delegate interface.
//
// Upon creation, the Shell sets itself as the RootWindow's delegate, which
// takes ownership of the Shell.
class ASH_EXPORT Shell {
 public:
  enum Direction {
    FORWARD,
    BACKWARD
  };

  // Accesses private data from a Shell for testing.
  class ASH_EXPORT TestApi {
   public:
    explicit TestApi(Shell* shell);

    internal::RootWindowLayoutManager* root_window_layout();
    internal::InputMethodEventFilter* input_method_event_filter();
    internal::WorkspaceController* workspace_controller();

   private:
    Shell* shell_;  // not owned

    DISALLOW_COPY_AND_ASSIGN(TestApi);
  };

  // A shell must be explicitly created so that it can call |Init()| with the
  // delegate set. |delegate| can be NULL (if not required for initialization).
  static Shell* CreateInstance(ShellDelegate* delegate);

  // Should never be called before |CreateInstance()|.
  static Shell* GetInstance();

  // Returns true if the ash shell has been instantiated.
  static bool HasInstance();

  static void DeleteInstance();

  // Gets the singleton RootWindow used by the Shell.
  static aura::RootWindow* GetRootWindow();

  internal::RootWindowLayoutManager* root_window_layout() const {
    return root_window_layout_;
  }

  aura::Window* GetContainer(int container_id);
  const aura::Window* GetContainer(int container_id) const;

  // Adds or removes |filter| from the RootWindowEventFilter.
  void AddRootWindowEventFilter(aura::EventFilter* filter);
  void RemoveRootWindowEventFilter(aura::EventFilter* filter);
  size_t GetRootWindowEventFilterCount() const;

  // Shows the background menu over |widget|.
  void ShowBackgroundMenu(views::Widget* widget, const gfx::Point& location);

  // Toggles app list.
  void ToggleAppList();

  // Returns true if the screen is locked.
  bool IsScreenLocked() const;

  // Returns true if a modal dialog window is currently open.
  bool IsModalWindowOpen() const;

  // Creates a default views::NonClientFrameView for use by windows in the
  // Ash environment.
  views::NonClientFrameView* CreateDefaultNonClientFrameView(
      views::Widget* widget);

  // Rotates focus through containers that can receive focus.
  void RotateFocus(Direction direction);

  // Sets the work area insets of the monitor that contains |window|,
  // this notifies observers too.
  // TODO(sky): this no longer really replicates what happens and is unreliable.
  // Remove this.
  void SetMonitorWorkAreaInsets(aura::Window* window,
                                const gfx::Insets& insets);

  // Initializes |launcher_|.  Does nothing if it's already initialized.
  void CreateLauncher();

  // Adds/removes observer.
  void AddShellObserver(ShellObserver* observer);
  void RemoveShellObserver(ShellObserver* observer);

#if !defined(OS_MACOSX)
  AcceleratorController* accelerator_controller() {
    return accelerator_controller_.get();
  }
#endif  // !defined(OS_MACOSX)

  internal::RootWindowEventFilter* root_filter() {
    return root_filter_;
  }
  internal::TooltipController* tooltip_controller() {
    return tooltip_controller_.get();
  }
  internal::KeyRewriterEventFilter* key_rewriter_filter() {
    return key_rewriter_filter_.get();
  }
  internal::PartialScreenshotEventFilter* partial_screenshot_filter() {
    return partial_screenshot_filter_.get();
  }
  DesktopBackgroundController* desktop_background_controller() {
    return desktop_background_controller_.get();
  }
  PowerButtonController* power_button_controller() {
    return power_button_controller_.get();
  }
  VideoDetector* video_detector() {
    return video_detector_.get();
  }
  WindowCycleController* window_cycle_controller() {
    return window_cycle_controller_.get();
  }
  internal::FocusCycler* focus_cycler() {
    return focus_cycler_.get();
  }

  ShellDelegate* delegate() { return delegate_.get(); }
  SystemTrayDelegate* tray_delegate() { return tray_delegate_.get(); }
  UserWallpaperDelegate* user_wallpaper_delegate() {
    return user_wallpaper_delegate_.get();
  }

  Launcher* launcher() { return launcher_.get(); }

  const ScreenAsh* screen() { return screen_; }

  // Force the shelf to query for it's current visibility state.
  void UpdateShelfVisibility();

  // Sets/gets the shelf auto-hide behavior.
  void SetShelfAutoHideBehavior(ShelfAutoHideBehavior behavior);
  ShelfAutoHideBehavior GetShelfAutoHideBehavior() const;

  // TODO(sky): don't expose this!
  internal::ShelfLayoutManager* shelf() const { return shelf_; }

  SystemTray* tray() const { return tray_.get(); }

  // Returns the size of the grid.
  int GetGridSize() const;

  static void set_initially_hide_cursor(bool hide) {
    initially_hide_cursor_ = hide;
  }

  internal::ResizeShadowController* resize_shadow_controller() {
    return resize_shadow_controller_.get();
  }

  // Made available for tests.
  internal::ShadowController* shadow_controller() {
    return shadow_controller_.get();
  }

 private:
  FRIEND_TEST_ALL_PREFIXES(RootWindowEventFilterTest, MouseEventCursors);
  FRIEND_TEST_ALL_PREFIXES(RootWindowEventFilterTest, TransformActivate);

  typedef std::pair<aura::Window*, gfx::Rect> WindowAndBoundsPair;

  explicit Shell(ShellDelegate* delegate);
  virtual ~Shell();

  void Init();

  // Initializes the layout managers and event filters.
  void InitLayoutManagers();

  // Disables the workspace grid layout.
  void DisableWorkspaceGridLayout();

  static Shell* instance_;

  // If set before the Shell is initialized, the mouse cursor will be hidden
  // when the screen is initially created.
  static bool initially_hide_cursor_;

  scoped_ptr<aura::RootWindow> root_window_;
  ScreenAsh* screen_;

  internal::RootWindowEventFilter* root_filter_;  // not owned

  std::vector<WindowAndBoundsPair> to_restore_;

#if !defined(OS_MACOSX)
  scoped_ptr<NestedDispatcherController> nested_dispatcher_controller_;

  scoped_ptr<AcceleratorController> accelerator_controller_;
#endif  // !defined(OS_MACOSX)

  scoped_ptr<ShellDelegate> delegate_;
  scoped_ptr<SystemTrayDelegate> tray_delegate_;
  scoped_ptr<UserWallpaperDelegate> user_wallpaper_delegate_;

  scoped_ptr<Launcher> launcher_;

  scoped_ptr<internal::AppList> app_list_;

  scoped_ptr<internal::StackingController> stacking_controller_;
  scoped_ptr<internal::ActivationController> activation_controller_;
  scoped_ptr<internal::WindowModalityController> window_modality_controller_;
  scoped_ptr<internal::DragDropController> drag_drop_controller_;
  scoped_ptr<internal::WorkspaceController> workspace_controller_;
  scoped_ptr<internal::ResizeShadowController> resize_shadow_controller_;
  scoped_ptr<internal::ShadowController> shadow_controller_;
  scoped_ptr<internal::TooltipController> tooltip_controller_;
  scoped_ptr<internal::VisibilityController> visibility_controller_;
  scoped_ptr<DesktopBackgroundController> desktop_background_controller_;
  scoped_ptr<PowerButtonController> power_button_controller_;
  scoped_ptr<VideoDetector> video_detector_;
  scoped_ptr<WindowCycleController> window_cycle_controller_;
  scoped_ptr<internal::FocusCycler> focus_cycler_;
  scoped_ptr<internal::EventClientImpl> event_client_;
  scoped_ptr<internal::MonitorController> monitor_controller_;

  // An event filter that rewrites or drops a key event.
  scoped_ptr<internal::KeyRewriterEventFilter> key_rewriter_filter_;

  // An event filter that pre-handles key events while the partial
  // screenshot UI is active.
  scoped_ptr<internal::PartialScreenshotEventFilter> partial_screenshot_filter_;

#if !defined(OS_MACOSX)
  // An event filter that pre-handles global accelerators.
  scoped_ptr<internal::AcceleratorFilter> accelerator_filter_;
#endif

  // An event filter that pre-handles all key events to send them to an IME.
  scoped_ptr<internal::InputMethodEventFilter> input_method_filter_;

  // The shelf for managing the launcher and the status widget in non-compact
  // mode. Shell does not own the shelf. Instead, it is owned by container of
  // the status area.
  internal::ShelfLayoutManager* shelf_;

  ObserverList<ShellObserver> observers_;

  // Owned by aura::RootWindow, cached here for type safety.
  internal::RootWindowLayoutManager* root_window_layout_;

  // Status area with clock, Wi-Fi signal, etc.
  views::Widget* status_widget_;

  // System tray with clock, Wi-Fi signal, etc. (a replacement in progress for
  // |status_widget_|).
  scoped_ptr<SystemTray> tray_;

  DISALLOW_COPY_AND_ASSIGN(Shell);
};

}  // namespace ash

#endif  // ASH_SHELL_H_
