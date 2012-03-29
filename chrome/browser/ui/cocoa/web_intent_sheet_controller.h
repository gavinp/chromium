// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_WEB_INTENT_SHEET_CONTROLLER_H_
#define CHROME_BROWSER_UI_COCOA_WEB_INTENT_SHEET_CONTROLLER_H_
#pragma once

#import <Cocoa/Cocoa.h>

#include "base/memory/scoped_nsobject.h"
#import "chrome/browser/ui/cocoa/base_bubble_controller.h"
#include "chrome/browser/ui/intents/web_intent_picker.h"

class WebIntentPickerCocoa;
class WebIntentPickerModel;

// Controller for intent picker constrained dialog. This dialog pops up
// whenever a web page invokes ActivateIntent and lets the user choose which
// service should be used to handle this action.
@interface WebIntentPickerSheetController : NSWindowController {
 @private
  // C++ <-> ObjectiveC bridge. Weak reference.
  WebIntentPickerCocoa* picker_;

  // Inline disposition tab contents. Weak reference.
  TabContentsWrapper* contents_;

  // The intent picker data to be rendered. Weak reference.
  WebIntentPickerModel* model_;
}

// Initialize the constrained dialog, and connect to picker.
- (id)initWithPicker:(WebIntentPickerCocoa*)picker;

// Set the contents for inline disposition intents.
- (void)setInlineDispositionTabContents:(TabContentsWrapper*)wrapper;

- (void)performLayoutWithModel:(WebIntentPickerModel*)model;

// Close the current sheet (and by extension, the constrained dialog).
- (void)closeSheet;

// Notification handler - called when sheet has been closed.
- (void)sheetDidEnd:(NSWindow*)sheet
         returnCode:(int)returnCode
        contextInfo:(void*)contextInfo;
@end  // WebIntentPickerSheetController

#endif  // CHROME_BROWSER_UI_COCOA_WEB_INTENT_SHEET_CONTROLLER_H_
