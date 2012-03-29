// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_AURA_MONITOR_MANAGER_H_
#define UI_AURA_MONITOR_MANAGER_H_
#pragma once

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/observer_list.h"
#include "ui/aura/aura_export.h"

namespace gfx {
class Point;
class Size;
}

namespace aura {
class Monitor;
class RootWindow;
class Window;

// Observers for monitor configuration changes.
// TODO(oshima): multiple monitor support.
class MonitorObserver {
 public:
  virtual void OnMonitorBoundsChanged(const Monitor* monitor) = 0;
  virtual void OnMonitorAdded(Monitor* new_monitor) = 0;
  virtual void OnMonitorRemoved(const Monitor* old_monitor) = 0;
};

// MonitorManager creates, deletes and updates Monitor objects when
// monitor configuration changes, and notifies MonitorObservers about
// the change. This is owned by Env and its lifetime is longer than
// any windows.
class AURA_EXPORT MonitorManager {
 public:
  static void set_use_fullscreen_host_window(bool use_fullscreen) {
    use_fullscreen_host_window_ = use_fullscreen;
  }
  static bool use_fullscreen_host_window() {
    return use_fullscreen_host_window_;
  }

  // Creates a monitor from string spec. 100+200-1440x800 creates monitor
  // whose size is 1440x800 at the location (100, 200) in screen's coordinates.
  // The location can be omitted and be just "1440x800", which creates
  // monitor at the origin of the screen. An empty string creates
  // the monitor with default size.
  static Monitor* CreateMonitorFromSpec(const std::string& spec);

  // A utility function to create a root window for primary monitor.
  static RootWindow* CreateRootWindowForPrimaryMonitor();

  MonitorManager();
  virtual ~MonitorManager();

  // Adds/removes MonitorObservers.
  void AddObserver(MonitorObserver* observer);
  void RemoveObserver(MonitorObserver* observer);

  // Called when monitor configuration has changed. The new monitor
  // configurations is passed as a vector of Monitor object, which
  // contains each monitor's new infomration.
  virtual void OnNativeMonitorsChanged(
      const std::vector<const Monitor*>& monitors) = 0;

  // Create a root window for given |monitor|.
  virtual RootWindow* CreateRootWindowForMonitor(Monitor* monitor) = 0;

  // Returns the monitor object nearest given |window|.
  virtual const Monitor* GetMonitorNearestWindow(
      const Window* window) const = 0;
  virtual Monitor* GetMonitorNearestWindow(const Window* window) = 0;

  // Returns the monitor object nearest given |pint|.
  virtual  const Monitor* GetMonitorNearestPoint(
      const gfx::Point& point) const = 0;

  // Returns the monitor at |index|. The monitor at 0 is considered
  // "primary".
  virtual Monitor* GetMonitorAt(size_t index) = 0;

  virtual size_t GetNumMonitors() const = 0;

 protected:
  // Calls observers' OnMonitorBoundsChanged methods.
  void NotifyBoundsChanged(const Monitor* monitor);
  void NotifyMonitorAdded(Monitor* monitor);
  void NotifyMonitorRemoved(const Monitor* monitor);

 private:
  // If set before the RootWindow is created, the host window will cover the
  // entire monitor.  Note that this can still be overridden via the
  // switches::kAuraHostWindowSize flag.
  static bool use_fullscreen_host_window_;

  ObserverList<MonitorObserver> observers_;
  DISALLOW_COPY_AND_ASSIGN(MonitorManager);
};

}  // namespace aura

#endif  // UI_AURA_MONITOR_MANAGER_H_
