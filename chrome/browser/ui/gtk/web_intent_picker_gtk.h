// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_GTK_WEB_INTENT_PICKER_GTK_H_
#define CHROME_BROWSER_UI_GTK_WEB_INTENT_PICKER_GTK_H_
#pragma once

#include <gtk/gtk.h>

#include <vector>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/ui/gtk/constrained_window_gtk.h"
#include "chrome/browser/ui/intents/web_intent_inline_disposition_delegate.h"
#include "chrome/browser/ui/intents/web_intent_picker.h"
#include "chrome/browser/ui/intents/web_intent_picker_model_observer.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "ui/base/gtk/gtk_signal.h"
#include "ui/base/gtk/owned_widget_gtk.h"

class Browser;
class CustomDrawButton;
class GURL;
class TabContentsContainerGtk;
class TabContentsWrapper;
class ThrobberGtk;
class WebIntentPickerDelegate;

// Gtk implementation of WebIntentPicker.
class WebIntentPickerGtk : public WebIntentPicker,
                           public WebIntentPickerModelObserver,
                           public ConstrainedWindowGtkDelegate,
                           public content::NotificationObserver {
 public:
  WebIntentPickerGtk(Browser* browser,
                     TabContentsWrapper* tab_contents,
                     WebIntentPickerDelegate* delegate,
                     WebIntentPickerModel* model);
  virtual ~WebIntentPickerGtk();

  // WebIntentPicker implementation.
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

  // ConstrainedWindowGtkDelegate implementation.
  virtual GtkWidget* GetWidgetRoot() OVERRIDE;
  virtual GtkWidget* GetFocusWidget() OVERRIDE;
  virtual void DeleteDelegate() OVERRIDE;

   // content::NotificationObserver implementation.
   virtual void Observe(int type,
                        const content::NotificationSource& source,
                        const content::NotificationDetails& details) OVERRIDE;

 private:
  // Callback when picker is destroyed.
  CHROMEGTK_CALLBACK_0(WebIntentPickerGtk, void, OnDestroy);
  // Callback when a service button is clicked.
  CHROMEGTK_CALLBACK_0(WebIntentPickerGtk, void, OnServiceButtonClick);
  // Callback when close button is clicked.
  CHROMEGTK_CALLBACK_0(WebIntentPickerGtk, void, OnCloseButtonClick);
  // Callback when suggested extension title link is clicked.
  CHROMEGTK_CALLBACK_0(WebIntentPickerGtk, void, OnExtensionLinkClick);
  // Callback when suggested extension install button is clicked.
  CHROMEGTK_CALLBACK_0(WebIntentPickerGtk, void, OnExtensionInstallButtonClick);
  // Callback when "more suggestions" link is clicked.
  CHROMEGTK_CALLBACK_0(WebIntentPickerGtk, void, OnMoreSuggestionsLinkClick);

  // Initialize the contents of the picker. After this call, contents_ will be
  // non-NULL.
  void InitContents();

  // Update the installed service buttons from |model_|.
  void UpdateInstalledServices();

  // Update the Chrome Web Store label from |model_|.
  void UpdateCWSLabel();

  // Update the suggested extension table from |model_|.
  void UpdateSuggestedExtensions();

  // Enables/disables all service buttons and extension suggestions.
  void SetWidgetsEnabled(bool enabled);

  // Adds a throbber to the extension at |index|. Returns the alignment widget
  // containing the throbber.
  GtkWidget* AddThrobberToExtensionAt(size_t index);

  // Removes the added throbber.
  void RemoveThrobber();

  // Create a new widget displaying |rating| as 5 star images. Rating should be
  // in the range [0, 5].
  GtkWidget* CreateStarsWidget(double rating);

  // A weak pointer to the tab contents on which to display the picker UI.
  TabContentsWrapper* wrapper_;

  // A weak pointer to the WebIntentPickerDelegate to notify when the user
  // chooses a service or cancels.
  WebIntentPickerDelegate* delegate_;

  // A weak pointer to the picker model.
  WebIntentPickerModel* model_;

  // A weak pointer to the widget that contains all other widgets in
  // the picker.
  GtkWidget* contents_;

  // A weak pointer to the header label.
  GtkWidget* header_label_;

  // A weak pointer to the vbox that contains the buttons used to choose the
  // service.
  GtkWidget* button_vbox_;

  // A weak pointer to the Chrome Web Store header label.
  GtkWidget* cws_label_;

  // A weak pointer to the suggested extensions vbox.
  GtkWidget* extensions_vbox_;

  // A button to close the picker.
  scoped_ptr<CustomDrawButton> close_button_;

  // The throbber to display when installing an extension.
  scoped_ptr<ThrobberGtk> throbber_;

  // A weak pointer to the constrained window.
  ConstrainedWindowGtk* window_;

  // The browser we're in.
  Browser* browser_;

  // Container for the HTML in the inline disposition case.
  scoped_ptr<TabContentsWrapper> inline_disposition_tab_contents_;

  // Widget for displaying the HTML in the inline disposition case.
  scoped_ptr<TabContentsContainerGtk> tab_contents_container_;

  // content::WebContentsDelegate for the inline disposition dialog.
  scoped_ptr<WebIntentInlineDispositionDelegate> inline_disposition_delegate_;

  content::NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(WebIntentPickerGtk);
};

#endif  // CHROME_BROWSER_UI_GTK_WEB_INTENT_PICKER_GTK_H_
