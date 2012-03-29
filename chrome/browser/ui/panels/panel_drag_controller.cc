// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/panels/panel_drag_controller.h"

#include "base/logging.h"
#include "chrome/browser/ui/panels/detached_panel_strip.h"
#include "chrome/browser/ui/panels/docked_panel_strip.h"
#include "chrome/browser/ui/panels/panel.h"
#include "chrome/browser/ui/panels/panel_manager.h"
#include "chrome/browser/ui/panels/panel_strip.h"
#include "ui/gfx/rect.h"

// static
const int PanelDragController::kDetachDockedPanelThreshold = 100;
const int PanelDragController::kDockDetachedPanelThreshold = 30;

PanelDragController::PanelDragController(PanelManager* panel_manager)
    : panel_manager_(panel_manager),
      dragging_panel_(NULL),
      dragging_panel_original_strip_(NULL) {
}

PanelDragController::~PanelDragController() {
}

void PanelDragController::StartDragging(Panel* panel,
                                        const gfx::Point& mouse_location) {
  DCHECK(!dragging_panel_);
  DCHECK(panel->draggable());

  last_mouse_location_ = mouse_location;
  offset_from_mouse_location_on_drag_start_ =
      mouse_location.Subtract(panel->GetBounds().origin());

  dragging_panel_ = panel;
  dragging_panel_->SetPreviewMode(true);

  // Keep track of original strip and placement for the case that the drag is
  // cancelled.
  dragging_panel_original_strip_ = dragging_panel_->panel_strip();
  dragging_panel_original_strip_->SavePanelPlacement(dragging_panel_);

  dragging_panel_original_strip_->StartDraggingPanelWithinStrip(
      dragging_panel_);
}

void PanelDragController::Drag(const gfx::Point& mouse_location) {
  DCHECK(dragging_panel_);

  PanelStrip* current_strip = dragging_panel_->panel_strip();

  gfx::Rect target_panel_bounds;
  PanelStrip* target_strip = ComputeDragTargetStrip(
      mouse_location, &target_panel_bounds);
  if (target_strip != current_strip) {
    // End the dragging in old strip.
    current_strip->EndDraggingPanelWithinStrip(dragging_panel_, true);

    // Apply new panel position.
    dragging_panel_->SetPanelBounds(target_panel_bounds);

    // Move the panel to new strip.
    panel_manager_->MovePanelToStrip(dragging_panel_,
                                     target_strip->type(),
                                     PanelStrip::KNOWN_POSITION);

    // Start the dragging in new strip.
    target_strip->StartDraggingPanelWithinStrip(dragging_panel_);
  } else {
    current_strip->DragPanelWithinStrip(
        dragging_panel_,
        mouse_location.x() - last_mouse_location_.x(),
        mouse_location.y() - last_mouse_location_.y());
  }

  last_mouse_location_ = mouse_location;
}

void PanelDragController::EndDragging(bool cancelled) {
  DCHECK(dragging_panel_);

  PanelStrip* current_strip = dragging_panel_->panel_strip();
  if (cancelled) {
    // Abort the drag in current strip.
    current_strip->EndDraggingPanelWithinStrip(dragging_panel_, true);

    // Restore the dragging panel to its original strip if needed.
    // Note that the bounds of dragging panel is updated later by calling
    // RestorePanelToSavedPlacement.
    if (current_strip != dragging_panel_original_strip_) {
      PanelStrip::PositioningMask positioning_mask =
          static_cast<PanelStrip::PositioningMask>(
              PanelStrip::DEFAULT_POSITION | PanelStrip::DO_NOT_UPDATE_BOUNDS);
      panel_manager_->MovePanelToStrip(
          dragging_panel_,
          dragging_panel_original_strip_->type(),
          positioning_mask);
    }

    // End the preview mode.
    dragging_panel_->SetPreviewMode(false);

    // Restore the dragging panel to its original placement.
    dragging_panel_original_strip_->RestorePanelToSavedPlacement();
  } else {
    // The saved placement is no longer needed.
    dragging_panel_original_strip_->DiscardSavedPanelPlacement();

    // End the preview mode.
    dragging_panel_->SetPreviewMode(false);

    // End the drag. This will cause the panel to be moved to its finalized
    // position.
    current_strip->EndDraggingPanelWithinStrip(dragging_panel_, false);
  }

  dragging_panel_ = NULL;
}

PanelStrip* PanelDragController::ComputeDragTargetStrip(
    const gfx::Point& mouse_location, gfx::Rect* new_panel_bounds) const {
  if (CanDragToDockedStrip(mouse_location, new_panel_bounds))
    return panel_manager_->docked_strip();
  else if (CanDragToDetachedStrip(mouse_location, new_panel_bounds))
    return panel_manager_->detached_strip();
  else
    return dragging_panel_->panel_strip();
}

bool PanelDragController::CanDragToDockedStrip(
    const gfx::Point& mouse_location,
    gfx::Rect* new_panel_bounds) const {
  // It has to come from the detached strip.
  if (dragging_panel_->panel_strip()->type() != PanelStrip::DETACHED)
    return false;

  // Compute target panel bounds. The origin is computed based on the fact that
  // the panel should follow the mouse movement. The size remains unchanged.
  gfx::Rect target_panel_bounds = dragging_panel_->GetBounds();
  target_panel_bounds.set_origin(
      mouse_location.Subtract(offset_from_mouse_location_on_drag_start_));

  // The bottom of the panel should come very close to or fall below the bottom
  // of the docked area.
  if (panel_manager_->docked_strip()->display_area().bottom() -
          target_panel_bounds.bottom() >
      kDockDetachedPanelThreshold)
    return false;

  *new_panel_bounds = target_panel_bounds;
  return true;
}

bool PanelDragController::CanDragToDetachedStrip(
    const gfx::Point& mouse_location,
    gfx::Rect* new_panel_bounds) const {
  // It has to come from the docked strip.
  if (dragging_panel_->panel_strip()->type() != PanelStrip::DOCKED)
    return false;

  // The minimized docked panel is not allowed to detach.
  if (dragging_panel_->IsMinimized())
    return false;

  // Compute target panel bounds. The origin is computed based on the fact that
  // the panel should follow the mouse movement. The size remains unchanged.
  gfx::Rect target_panel_bounds = dragging_panel_->GetBounds();
  target_panel_bounds.set_origin(
      mouse_location.Subtract(offset_from_mouse_location_on_drag_start_));

  // The panel should be dragged up high enough to pass certain threshold.
  if (panel_manager_->docked_strip()->display_area().bottom() -
          target_panel_bounds.bottom() <
      kDetachDockedPanelThreshold)
    return false;

  *new_panel_bounds = target_panel_bounds;
  return true;
}

void PanelDragController::OnPanelClosed(Panel* panel) {
  if (!dragging_panel_)
    return;

  // If the dragging panel is closed, abort the drag.
  if (dragging_panel_ == panel)
    EndDragging(false);
}
