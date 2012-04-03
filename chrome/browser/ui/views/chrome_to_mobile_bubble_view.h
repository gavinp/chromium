// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_CHROME_TO_MOBILE_BUBBLE_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_CHROME_TO_MOBILE_BUBBLE_VIEW_H_
#pragma once

#include <map>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/chrome_to_mobile_service.h"
#include "ui/views/bubble/bubble_delegate.h"
#include "ui/views/controls/button/button.h"

class Profile;

namespace base {
class DictionaryValue;
}

namespace ui {
class ThrobAnimation;
}

namespace views {
class Checkbox;
class Label;
class RadioButton;
class TextButton;
}

// ChromeToMobileBubbleView is a bubble view for the Chrome To Mobile service.
class ChromeToMobileBubbleView : public views::BubbleDelegateView,
                                 public views::ButtonListener,
                                 public ChromeToMobileService::Observer {
 public:
  virtual ~ChromeToMobileBubbleView();

  static void ShowBubble(views::View* anchor_view, Profile* profile);
  static bool IsShowing();
  static void Hide();

  // views::BubbleDelegateView methods.
  virtual views::View* GetInitiallyFocusedView() OVERRIDE;
  virtual gfx::Rect GetAnchorRect() OVERRIDE;
  virtual void WindowClosing() OVERRIDE;
  virtual bool AcceleratorPressed(const ui::Accelerator& accelerator) OVERRIDE;
  virtual void AnimationProgressed(const ui::Animation* animation) OVERRIDE;

  // views::ButtonListener method.
  virtual void ButtonPressed(views::Button* sender,
                             const views::Event& event) OVERRIDE;

  // ChromeToMobileService::Observer methods.
  virtual void SnapshotGenerated(const FilePath& path, int64 bytes) OVERRIDE;
  virtual void OnSendComplete(bool success) OVERRIDE;

 protected:
  // views::BubbleDelegateView method.
  virtual void Init() OVERRIDE;

 private:
  ChromeToMobileBubbleView(views::View* anchor_view, Profile* profile);

  // Handle the message when the user presses a button.
  void HandleButtonPressed(views::Button* sender);

  // Send the page to the mobile device.
  void Send();

  // The Chrome To Mobile bubble, if we're showing one.
  static ChromeToMobileBubbleView* bubble_;

  base::WeakPtrFactory<ChromeToMobileBubbleView> weak_ptr_factory_;

  // The Chrome To Mobile service associated with this bubble.
  scoped_refptr<ChromeToMobileService> service_;

  // A map of radio buttons for each mobile device to the device's information.
  typedef std::map<views::RadioButton*, base::DictionaryValue*> DeviceMap;
  DeviceMap mobile_map_;

  // The currently selected (or solitary) mobile device's info.
  base::DictionaryValue* selected_mobile_;

  // The file path for the MHTML page snapshot.
  FilePath snapshot_path_;

  views::Checkbox* send_copy_;
  views::TextButton* send_;
  views::TextButton* cancel_;

  // An animation used to cycle through the "Sending..." status messages.
  scoped_ptr<ui::ThrobAnimation> progress_animation_;

  DISALLOW_COPY_AND_ASSIGN(ChromeToMobileBubbleView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_CHROME_TO_MOBILE_BUBBLE_VIEW_H_
