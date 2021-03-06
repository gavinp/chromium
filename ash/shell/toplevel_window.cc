// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/shell/toplevel_window.h"

#include "base/utf_string_conversions.h"
#include "ui/aura/window.h"
#include "ui/gfx/canvas.h"
#include "ui/views/widget/widget.h"

namespace ash {
namespace shell {

ToplevelWindow::CreateParams::CreateParams()
    : can_resize(false),
      can_maximize(false) {
}

// static
void ToplevelWindow::CreateToplevelWindow(const CreateParams& params) {
  static int count = 0;
  int x = count == 0 ? 50 : 350;
  count = (count + 1) % 2;
  views::Widget* widget =
      views::Widget::CreateWindowWithBounds(new ToplevelWindow(params),
                                            gfx::Rect(x, 150, 300, 300));
  widget->GetNativeView()->SetName("Examples:ToplevelWindow");
  widget->Show();
}

ToplevelWindow::ToplevelWindow(const CreateParams& params) : params_(params) {
}

ToplevelWindow::~ToplevelWindow() {
}

void ToplevelWindow::OnPaint(gfx::Canvas* canvas) {
  canvas->FillRect(GetLocalBounds(), SK_ColorDKGRAY);
}

string16 ToplevelWindow::GetWindowTitle() const {
  return ASCIIToUTF16("Examples: Toplevel Window");
}

views::View* ToplevelWindow::GetContentsView() {
  return this;
}

bool ToplevelWindow::CanResize() const {
  return params_.can_resize;
}

bool ToplevelWindow::CanMaximize() const {
  return params_.can_maximize;
}

}  // namespace shell
}  // namespace ash
