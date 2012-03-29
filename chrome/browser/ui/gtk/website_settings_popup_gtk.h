// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_GTK_WEBSITE_SETTINGS_POPUP_GTK_H_
#define CHROME_BROWSER_UI_GTK_WEBSITE_SETTINGS_POPUP_GTK_H_
#pragma once

#include "chrome/browser/ui/website_settings_ui.h"

#include <gtk/gtk.h>

#include "chrome/browser/ui/gtk/bubble/bubble_gtk.h"

class Browser;
class BubbleGtk;
class Profile;
class TabContentsWrapper;
class ThemeServiceGtk;

// GTK implementation of the website settings UI. The website settings UI is
// displayed in a popup that is positioned relative the an anchor element.
class WebsiteSettingsPopupGtk : public WebsiteSettingsUI,
                                public BubbleDelegateGtk {
 public:
  WebsiteSettingsPopupGtk(gfx::NativeWindow parent,
                          Profile* profile,
                          TabContentsWrapper* wrapper);
  virtual ~WebsiteSettingsPopupGtk();

  // WebsiteSettingsUI implementations.
  virtual void SetDelegate(WebsiteSettingsUIDelegate* delegate) OVERRIDE;
  virtual void SetSiteInfo(const std::string site_info) OVERRIDE;
  virtual void SetCookieInfo(const CookieInfoList& cookie_info_list) OVERRIDE;
  virtual void SetPermissionInfo(
      const PermissionInfoList& permission_info_list) OVERRIDE;

  // BubbleDelegateGtk implementation.
  virtual void BubbleClosing(BubbleGtk* bubble, bool closed_by_escape) OVERRIDE;

 private:
  // Layouts the different sections retrieved from the model.
  void InitContents();

  // Removes all children of |container|.
  void ClearContainer(GtkWidget* container);

  // Creates a popup section and returns a virtual box that contains the section
  // content.
  GtkWidget* CreateSection(std::string section_title,
                           GtkWidget* section_content);

  // Callbacks for the link buttons.
  CHROMEGTK_CALLBACK_0(WebsiteSettingsPopupGtk, void, OnCookiesLinkClicked);
  CHROMEGTK_CALLBACK_0(WebsiteSettingsPopupGtk, void, OnPermissionChanged);
  CHROMEGTK_CALLBACK_0(WebsiteSettingsPopupGtk, void,
                       OnPermissionsSettingsLinkClicked);
  CHROMEGTK_CALLBACK_1(WebsiteSettingsPopupGtk, void, OnComboBoxShown,
                       GParamSpec*);

  // Parent window.
  GtkWindow* parent_;

  // The container that contains the content of the popup.
  GtkWidget* contents_;

  // The widget relative to which the popup is positioned.
  GtkWidget* anchor_;

  // Provides colors and stuff.
  ThemeServiceGtk* theme_service_;

  // The popup bubble container.
  BubbleGtk* bubble_;

  Profile* profile_;

  TabContentsWrapper* tab_contents_wrapper_;

  // The browser object of the current window. This is needed to open the
  // settings page in a new tab.
  Browser* browser_;

  // Container for the site info section content.
  GtkWidget* site_info_contents_;
  // Container for the cookies and site data section content.
  GtkWidget* cookies_section_contents_;
  // Container for the permissions section content.
  GtkWidget* permissions_section_contents_;

  WebsiteSettingsUIDelegate* delegate_;

  DISALLOW_COPY_AND_ASSIGN(WebsiteSettingsPopupGtk);
};

#endif  // CHROME_BROWSER_UI_GTK_WEBSITE_SETTINGS_POPUP_GTK_H_
