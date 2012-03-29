// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FOCUS_CYCLER_H_
#define FOCUS_CYCLER_H_
#pragma once

#include <vector>

#include "ash/ash_export.h"
#include "base/compiler_specific.h"
#include "ui/base/accelerators/accelerator.h"

namespace views {
class Widget;
}  // namespace views

namespace ash {

namespace internal {

// This class handles moving focus between a set of widgets and the main browser
// window.
class ASH_EXPORT FocusCycler : public ui::AcceleratorTarget {
 public:
  enum Direction {
    FORWARD,
    BACKWARD
  };

  FocusCycler();
  virtual ~FocusCycler();

  // Returns the widget the FocusCycler is attempting to activate or NULL if
  // FocusCycler is not activating any widgets.
  const views::Widget* widget_activating() const { return widget_activating_; }

  // Add a widget to the focus cycle and set up accelerators.  The widget needs
  // to have an AccessiblePaneView as the content view.
  void AddWidget(views::Widget* widget);

  // Move focus to the next widget.
  void RotateFocus(Direction direction);

  // Moves focus the specified widget. Returns true if the widget was activated.
  bool FocusWidget(views::Widget* widget);

  // ui::AcceleratorTarget overrides
  virtual bool AcceleratorPressed(const ui::Accelerator& accelerator) OVERRIDE;
  virtual bool CanHandleAccelerators() const OVERRIDE;

 private:
  std::vector<views::Widget*> widgets_;

  // See description above getter.
  views::Widget* widget_activating_;

  DISALLOW_COPY_AND_ASSIGN(FocusCycler);
};

}  // namespace internal

}  // namespace ash

#endif  // FOCUS_CYCLER_H_
