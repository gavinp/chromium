// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_PANELS_PANEL_CONSTANTS_H_
#define CHROME_BROWSER_UI_PANELS_PANEL_CONSTANTS_H_
#pragma once

namespace panel {

  // Different platforms use different modifier keys to change the behavior
  // of a mouse click. This enum captures the meaning of the modifier rather
  // than the actual modifier key to generalize across platforms.
  enum ClickModifier {
    NO_MODIFIER,
    APPLY_TO_ALL,  // Apply the click behavior to all panels in the strip.
  };

}  // namespace panel

#endif  // CHROME_BROWSER_UI_PANELS_PANEL_CONSTANTS_H_
