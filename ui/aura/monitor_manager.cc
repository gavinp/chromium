// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/aura/monitor_manager.h"

#include <stdio.h>

#include "ui/aura/env.h"
#include "ui/aura/monitor.h"
#include "ui/aura/root_window.h"
#include "ui/aura/root_window_host.h"
#include "ui/gfx/rect.h"

namespace aura {
namespace {
// Default bounds for a monitor.
static const int kDefaultHostWindowX = 200;
static const int kDefaultHostWindowY = 200;
static const int kDefaultHostWindowWidth = 1280;
static const int kDefaultHostWindowHeight = 1024;
}  // namespace

// static
bool MonitorManager::use_fullscreen_host_window_ = false;

// static
Monitor* MonitorManager::CreateMonitorFromSpec(const std::string& spec) {
  gfx::Rect bounds(kDefaultHostWindowX, kDefaultHostWindowY,
                   kDefaultHostWindowWidth, kDefaultHostWindowHeight);
  int x = 0, y = 0, width, height;
  if (sscanf(spec.c_str(), "%dx%d", &width, &height) == 2) {
    bounds.set_size(gfx::Size(width, height));
  } else if (sscanf(spec.c_str(), "%d+%d-%dx%d", &x, &y, &width, &height)
             == 4) {
    bounds = gfx::Rect(x, y, width, height);
  } else if (use_fullscreen_host_window_) {
    bounds = gfx::Rect(aura::RootWindowHost::GetNativeScreenSize());
  }
  Monitor* monitor = new Monitor();
  monitor->set_bounds(bounds);
  return monitor;
}

// static
RootWindow* MonitorManager::CreateRootWindowForPrimaryMonitor() {
  MonitorManager* manager = aura::Env::GetInstance()->monitor_manager();
  RootWindow* root =
      manager->CreateRootWindowForMonitor(manager->GetMonitorAt(0));
  if (use_fullscreen_host_window_)
    root->ConfineCursorToWindow();
  return root;
}

MonitorManager::MonitorManager() {
}

MonitorManager::~MonitorManager() {
}

void MonitorManager::AddObserver(MonitorObserver* observer) {
  observers_.AddObserver(observer);
}

void MonitorManager::RemoveObserver(MonitorObserver* observer) {
  observers_.RemoveObserver(observer);
}

void MonitorManager::NotifyBoundsChanged(const Monitor* monitor) {
  FOR_EACH_OBSERVER(MonitorObserver, observers_,
                    OnMonitorBoundsChanged(monitor));
}

void MonitorManager::NotifyMonitorAdded(Monitor* monitor) {
  FOR_EACH_OBSERVER(MonitorObserver, observers_,
                    OnMonitorAdded(monitor));
}

void MonitorManager::NotifyMonitorRemoved(const Monitor* monitor) {
  FOR_EACH_OBSERVER(MonitorObserver, observers_,
                    OnMonitorRemoved(monitor));
}

}  // namespace aura
