// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/aura/monitor_change_observer_x11.h"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include <X11/extensions/Xrandr.h>

#include "base/message_pump_x.h"
#include "base/stl_util.h"
#include "ui/aura/env.h"
#include "ui/aura/dispatcher_linux.h"
#include "ui/aura/monitor.h"
#include "ui/aura/monitor_manager.h"

namespace aura {
namespace internal {

namespace {
XRRModeInfo* FindMode(XRRScreenResources* screen_resources, XID current_mode) {
  for (int m = 0; m < screen_resources->nmode; m++) {
    XRRModeInfo *mode = &screen_resources->modes[m];
    if (mode->id == current_mode)
      return mode;
  }
  return NULL;
}

bool CompareMonitorY(const Monitor* lhs, const Monitor* rhs) {
  return lhs->bounds().y() < rhs->bounds().y();
}

}  // namespace

MonitorChangeObserverX11::MonitorChangeObserverX11()
    : xdisplay_(base::MessagePumpX::GetDefaultXDisplay()),
      x_root_window_(DefaultRootWindow(xdisplay_)),
      xrandr_event_base_(0) {
  XRRSelectInput(xdisplay_, x_root_window_, RRScreenChangeNotifyMask);
  int error_base_ignored;
  XRRQueryExtension(xdisplay_, &xrandr_event_base_, &error_base_ignored);
  static_cast<DispatcherLinux*>(
      Env::GetInstance()->GetDispatcher())->
      WindowDispatcherCreated(x_root_window_, this);
}

MonitorChangeObserverX11::~MonitorChangeObserverX11() {
  static_cast<DispatcherLinux*>(
      Env::GetInstance()->GetDispatcher())->
      WindowDispatcherDestroying(x_root_window_);
}

base::MessagePumpDispatcher::DispatchStatus
MonitorChangeObserverX11::Dispatch(XEvent* event) {
  if (event->type - xrandr_event_base_ == RRScreenChangeNotify) {
    NotifyMonitorChange();
    return base::MessagePumpDispatcher::EVENT_PROCESSED;
  }
  return base::MessagePumpDispatcher::EVENT_IGNORED;
}

void MonitorChangeObserverX11::NotifyMonitorChange() {
  if (!MonitorManager::use_fullscreen_host_window())
    return;  // Use the default monitor that monitor manager determined.

  XRRScreenResources* screen_resources =
      XRRGetScreenResources(xdisplay_, x_root_window_);
  std::map<XID, XRRCrtcInfo*> crtc_info_map;

  for (int c = 0; c < screen_resources->ncrtc; c++) {
    XID crtc_id = screen_resources->crtcs[c];
    XRRCrtcInfo *crtc_info =
        XRRGetCrtcInfo(xdisplay_, screen_resources, crtc_id);
    crtc_info_map[crtc_id] = crtc_info;
  }

  std::vector<const Monitor*> monitors;
  std::set<int> y_coords;
  for (int o = 0; o < screen_resources->noutput; o++) {
    XRROutputInfo *output_info =
        XRRGetOutputInfo(xdisplay_,
                         screen_resources,
                         screen_resources->outputs[o]);
    if (output_info->connection != RR_Connected) {
      XRRFreeOutputInfo(output_info);
      continue;
    }
    XRRCrtcInfo* crtc_info = crtc_info_map[output_info->crtc];
    if (!crtc_info) {
      LOG(WARNING) << "Crtc not found for output";
      continue;
    }
    XRRModeInfo* mode = FindMode(screen_resources, crtc_info->mode);
    CHECK(mode);
    // Mirrored monitors have the same y coordinates.
    if (y_coords.find(crtc_info->y) != y_coords.end())
      continue;
    Monitor* monitor = new Monitor;
    monitor->set_bounds(gfx::Rect(crtc_info->x, crtc_info->y,
                                  mode->width, mode->height));
    monitors.push_back(monitor);
    y_coords.insert(crtc_info->y);
    XRRFreeOutputInfo(output_info);
  }

  // Free all allocated resources.
  for (std::map<XID, XRRCrtcInfo*>::const_iterator iter = crtc_info_map.begin();
       iter != crtc_info_map.end(); ++iter) {
    XRRFreeCrtcInfo(iter->second);
  }
  XRRFreeScreenResources(screen_resources);

  // PowerManager lays out the outputs vertically. Sort them by Y
  // coordinates.
  std::sort(monitors.begin(), monitors.end(), CompareMonitorY);
  Env::GetInstance()->monitor_manager()
      ->OnNativeMonitorsChanged(monitors);
  STLDeleteContainerPointers(monitors.begin(), monitors.end());
}

}  // namespace internal
}  // namespace aura
