// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_PANELS_PANEL_RESIZE_CONTROLLER_H_
#define CHROME_BROWSER_UI_PANELS_PANEL_RESIZE_CONTROLLER_H_
#pragma once

#include <set>
#include "base/basictypes.h"
#include "ui/gfx/point.h"
#include "ui/gfx/rect.h"

class Panel;
class PanelManager;
namespace gfx {
  class Rect;
}

// Responsible for handling resize operations initiated for all panels.
class PanelResizeController {
 public:
  explicit PanelResizeController(PanelManager* panel_manager);

  enum ResizingSides {
    NO_SIDES = 0,
    RESIZE_TOP,
    RESIZE_TOP_RIGHT,
    RESIZE_RIGHT,
    RESIZE_BOTTOM_RIGHT,
    RESIZE_BOTTOM,
    RESIZE_BOTTOM_LEFT,
    RESIZE_LEFT,
    RESIZE_TOP_LEFT
  };

  // Resize the given panel.
  // |mouse_location| is in screen coordinate system.
  void StartResizing(Panel* panel,
                     const gfx::Point& mouse_location,
                     ResizingSides sides);
  void Resize(const gfx::Point& mouse_location);
  void EndResizing(bool cancelled);

  // Asynchronous confirmation of panel having been closed.
  void OnPanelClosed(Panel* panel);

  bool IsResizing() const { return resizing_panel_ != NULL; }

  // Helper to compute the ResizingSide from the location of the mouse.
  // Splits the edges into 8 areas (edges and corners) using uniform
  // thickness.
  // TODO (ABurago): move this into the native layer.
  static ResizingSides IsMouseNearFrameSide(gfx::Point mouse_location,
                                            int resize_edge_thickness,
                                            Panel* panel);


 private:
  PanelManager* panel_manager_;  // Weak, owns us.

  // Panel currently being resized.
  Panel* resizing_panel_;

  // Resizing at which side?
  ResizingSides sides_resized_;

  // The mouse location, in screen coordinates, when StartResizing
  // previously called.
  gfx::Point mouse_location_at_start_;

  // Bounds to restore the panel to if resize is cancelled.
  gfx::Rect bounds_at_start_;

  DISALLOW_COPY_AND_ASSIGN(PanelResizeController);
};

#endif  // CHROME_BROWSER_UI_PANELS_PANEL_RESIZE_CONTROLLER_H_
