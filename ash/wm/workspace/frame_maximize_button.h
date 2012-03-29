// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_WORKSPACE_FRAME_MAXIMIZE_BUTTON_H_
#define ASH_WM_WORKSPACE_FRAME_MAXIMIZE_BUTTON_H_

#include "ash/ash_export.h"
#include "base/memory/scoped_ptr.h"
#include "ui/views/controls/button/image_button.h"

namespace views {
class NonClientFrameView;
}

namespace ash {

namespace internal {
class PhantomWindowController;
class SnapSizer;
}

// Button used for the maximize control on the frame. Handles snapping logic.
class ASH_EXPORT FrameMaximizeButton : public views::ImageButton {
 public:
  FrameMaximizeButton(views::ButtonListener* listener,
                      views::NonClientFrameView* frame);
  virtual ~FrameMaximizeButton();

  // ImageButton overrides:
  virtual bool OnMousePressed(const views::MouseEvent& event) OVERRIDE;
  virtual void OnMouseEntered(const views::MouseEvent& event) OVERRIDE;
  virtual void OnMouseExited(const views::MouseEvent& event) OVERRIDE;
  virtual bool OnMouseDragged(const views::MouseEvent& event) OVERRIDE;
  virtual void OnMouseReleased(const views::MouseEvent& event) OVERRIDE;
  virtual void OnMouseCaptureLost() OVERRIDE;

  // Sets is_left_right_enabled_ and updates tooltip.
  void SetIsLeftRightEnabled(bool e);

  void set_is_maximize_enabled(bool e) { is_maximize_enabled_ = e; }

 protected:
  // ImageButton overrides:
  virtual SkBitmap GetImageToPaint() OVERRIDE;

 private:
  class EscapeEventFilter;

  // Where to snap to.
  enum SnapType {
    SNAP_LEFT,
    SNAP_RIGHT,
    SNAP_MAXIMIZE,
    SNAP_MINIMIZE,
    SNAP_NONE
  };

  // Cancels snap behavior.
  void Cancel();

  // Installs/uninstalls an EventFilter to track when escape is pressed.
  void InstallEventFilter();
  void UninstallEventFilter();

  // Updates |snap_type_| based on a mouse drag.
  void UpdateSnap(const gfx::Point& location);

  // Returns true if maximizing is allowed.
  bool AllowMaximize() const;

  // Returns the type of snap based on the specified location.
  SnapType SnapTypeForLocation(const gfx::Point& location) const;

  // Returns the bounds of the resulting window for the specified type.
  gfx::Rect BoundsForType(SnapType type) const;

  // Converts location to screen coordinates and returns it. These are the
  // coordinates used by the SnapSizer.
  gfx::Point LocationForSnapSizer(const gfx::Point& location) const;

  // Snaps the window to the current snap position.
  void Snap();

  // Frame that the maximize button acts on.
  views::NonClientFrameView* frame_;

  // Renders the snap position.
  scoped_ptr<internal::PhantomWindowController> phantom_window_;

  // Is snapping enabled? Set on press so that in drag we know whether we
  // should show the snap locations.
  bool is_snap_enabled_;

  // Selectively enable/disable button functionality.
  bool is_left_right_enabled_;
  bool is_maximize_enabled_;

  // Did the user drag far enough to trigger snapping?
  bool exceeded_drag_threshold_;

  // Location of the press.
  gfx::Point press_location_;

  // Current snap type.
  SnapType snap_type_;

  scoped_ptr<internal::SnapSizer> snap_sizer_;

  scoped_ptr<EscapeEventFilter> escape_event_filter_;

  DISALLOW_COPY_AND_ASSIGN(FrameMaximizeButton);
};

}  // namespace ash

#endif  // ASH_WM_WORKSPACE_FRAME_MAXIMIZE_BUTTON_H_
