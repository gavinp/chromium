// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/renderer_preferences_util.h"

#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "content/public/common/renderer_preferences.h"

#if defined(TOOLKIT_USES_GTK)
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "chrome/browser/ui/gtk/theme_service_gtk.h"
#endif

namespace renderer_preferences_util {

void UpdateFromSystemSettings(
    content::RendererPreferences* prefs, Profile* profile) {
#if defined(TOOLKIT_USES_GTK)
  gtk_util::UpdateGtkFontSettings(prefs);

  ThemeServiceGtk* theme_service = ThemeServiceGtk::GetFrom(profile);

  prefs->focus_ring_color = theme_service->get_focus_ring_color();
  prefs->thumb_active_color = theme_service->get_thumb_active_color();
  prefs->thumb_inactive_color = theme_service->get_thumb_inactive_color();
  prefs->track_color = theme_service->get_track_color();
  prefs->active_selection_bg_color =
      theme_service->get_active_selection_bg_color();
  prefs->active_selection_fg_color =
      theme_service->get_active_selection_fg_color();
  prefs->inactive_selection_bg_color =
      theme_service->get_inactive_selection_bg_color();
  prefs->inactive_selection_fg_color =
      theme_service->get_inactive_selection_fg_color();
#elif defined(USE_ASH)
  // This color is 0x544d90fe modulated with 0xffffff.
  prefs->active_selection_bg_color = SkColorSetRGB(0xCB, 0xE4, 0xFA);
  prefs->active_selection_fg_color = SK_ColorBLACK;
  prefs->inactive_selection_bg_color = SkColorSetRGB(0xEA, 0xEA, 0xEA);
  prefs->inactive_selection_fg_color = SK_ColorBLACK;
#endif

  prefs->enable_referrers =
      profile->GetPrefs()->GetBoolean(prefs::kEnableReferrers);
  prefs->default_zoom_level =
      profile->GetPrefs()->GetDouble(prefs::kDefaultZoomLevel);
}

}  // renderer_preferences_util
