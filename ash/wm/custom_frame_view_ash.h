// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_CUSTOM_FRAME_VIEW_ASH_H_
#define ASH_WM_CUSTOM_FRAME_VIEW_ASH_H_
#pragma once

#include "ash/ash_export.h"
#include "base/memory/scoped_ptr.h"
#include "ui/views/controls/button/button.h"  // ButtonListener
#include "ui/views/window/non_client_view.h"

namespace ash {
class FramePainter;
}
namespace gfx {
class Font;
}
namespace views {
class ImageButton;
class Widget;
}

namespace ash {

// A NonClientFrameView used for dialogs and other non-browser windows.
// See also views::CustomFrameView and BrowserNonClientFrameViewAura.
class ASH_EXPORT CustomFrameViewAsh : public views::NonClientFrameView,
                                      public views::ButtonListener {
 public:
  // Internal class name.
  static const char kViewClassName[];

  CustomFrameViewAsh();
  virtual ~CustomFrameViewAsh();

  void Init(views::Widget* frame);

  views::ImageButton* close_button() { return close_button_; }

  // views::NonClientFrameView overrides:
  virtual gfx::Rect GetBoundsForClientView() const OVERRIDE;
  virtual gfx::Rect GetWindowBoundsForClientBounds(
      const gfx::Rect& client_bounds) const OVERRIDE;
  virtual int NonClientHitTest(const gfx::Point& point) OVERRIDE;
  virtual void GetWindowMask(const gfx::Size& size,
                             gfx::Path* window_mask) OVERRIDE;
  virtual void ResetWindowControls() OVERRIDE;
  virtual void UpdateWindowIcon() OVERRIDE;

  // views::View overrides:
  virtual gfx::Size GetPreferredSize() OVERRIDE;
  virtual void Layout() OVERRIDE;
  virtual std::string GetClassName() const OVERRIDE;
  virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE;
  virtual gfx::Size GetMinimumSize() OVERRIDE;

  // views::ButtonListener overrides:
  virtual void ButtonPressed(views::Button* sender,
                             const views::Event& event) OVERRIDE;

 private:
  // Height from top of window to top of client area.
  int NonClientTopBorderHeight() const;

  // Not owned.
  views::Widget* frame_;

  views::ImageButton* maximize_button_;
  views::ImageButton* close_button_;
  views::ImageButton* window_icon_;

  scoped_ptr<FramePainter> frame_painter_;

  DISALLOW_COPY_AND_ASSIGN(CustomFrameViewAsh);
};

}  // namespace ash

#endif  // ASH_WM_CUSTOM_FRAME_VIEW_ASH_H_
