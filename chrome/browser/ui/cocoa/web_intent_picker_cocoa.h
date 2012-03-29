// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_WEB_INTENT_PICKER_COCOA_H_
#define CHROME_BROWSER_UI_COCOA_WEB_INTENT_PICKER_COCOA_H_
#pragma once

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/intents/web_intent_picker.h"
#include "chrome/browser/ui/intents/web_intent_picker_model.h"
#include "chrome/browser/ui/intents/web_intent_picker_model_observer.h"

class ConstrainedWindow;
class TabContentsWrapper;
@class WebIntentPickerSheetController;
class WebIntentInlineDispositionDelegate;

// A bridge class that enables communication between ObjectiveC and C++.
class WebIntentPickerCocoa : public WebIntentPicker,
                             public WebIntentPickerModelObserver {
 public:
  // |wrapper|, and |delegate| must not be NULL.
  // |browser| should only be NULL for testing purposes.
  WebIntentPickerCocoa(Browser* browser,
                       TabContentsWrapper* wrapper,
                       WebIntentPickerDelegate* delegate,
                       WebIntentPickerModel* model);
  virtual ~WebIntentPickerCocoa();

  void OnSheetDidEnd(NSWindow* sheet);

  // WebIntentPickerDelegate forwarding API.
  void OnCancelled();
  void OnServiceChosen(size_t index);
  void OnExtensionInstallRequested(const std::string& extension_id);

  // WebIntentPicker:
  virtual void Close() OVERRIDE;
  virtual void OnExtensionInstallSuccess(const std::string& id) OVERRIDE;
  virtual void OnExtensionInstallFailure(const std::string& id) OVERRIDE;

  // WebIntentPickerModelObserver implementation.
  virtual void OnModelChanged(WebIntentPickerModel* model) OVERRIDE;
  virtual void OnFaviconChanged(WebIntentPickerModel* model,
                                size_t index) OVERRIDE;
  virtual void OnExtensionIconChanged(WebIntentPickerModel* model,
                                      const string16& extension_id) OVERRIDE;
  virtual void OnInlineDisposition(WebIntentPickerModel* model,
                                   const GURL& url) OVERRIDE;

 private:
  ConstrainedWindow* window_;  // Window for constrained sheet. Weak reference.

  // Weak pointer to the |delegate_| to notify about user choice/cancellation.
  WebIntentPickerDelegate* delegate_;

  // The picker model. Weak reference.
  WebIntentPickerModel* model_;

  Browser* browser_;  // The browser we're in. Weak Reference.

  WebIntentPickerSheetController* sheet_controller_;  // Weak reference.

  // Tab contents wrapper to hold intent page if inline disposition is used.
  scoped_ptr<TabContentsWrapper> inline_disposition_tab_contents_;

  // Delegate for inline disposition tab contents.
  scoped_ptr<WebIntentInlineDispositionDelegate> inline_disposition_delegate_;

  // Indicate that we invoked a service, instead of just closing/cancelling.
  bool service_invoked;

  // Re-layout the intent picker.
  void PerformLayout();

  // Default constructor, for testing only.
  WebIntentPickerCocoa();

  // For testing access.
  friend class WebIntentPickerSheetControllerTest;

  DISALLOW_COPY_AND_ASSIGN(WebIntentPickerCocoa);
};

#endif  // CHROME_BROWSER_UI_COCOA_WEB_INTENT_PICKER_COCOA_H_
