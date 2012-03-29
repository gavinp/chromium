// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/protector/base_prefs_change.h"
#include "chrome/browser/protector/histograms.h"
#include "chrome/browser/protector/protector_service.h"
#include "chrome/browser/protector/protector_service_factory.h"
#include "chrome/browser/tabs/tab_strip_model.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/common/pref_names.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace protector {

// Session startup settings change tracked by Protector.
class SessionStartupChange : public BasePrefsChange {
 public:
  SessionStartupChange(const SessionStartupPref& actual_startup_pref,
                       const PinnedTabCodec::Tabs& actual_pinned_tabs,
                       const SessionStartupPref& backup_startup_pref,
                       const PinnedTabCodec::Tabs& backup_pinned_tabs);

  // BaseSettingChange overrides:
  virtual bool Init(Profile* profile) OVERRIDE;
  virtual void Apply(Browser* browser) OVERRIDE;
  virtual void Discard(Browser* browser) OVERRIDE;
  virtual void Timeout() OVERRIDE;
  virtual int GetBadgeIconID() const OVERRIDE;
  virtual int GetMenuItemIconID() const OVERRIDE;
  virtual int GetBubbleIconID() const OVERRIDE;
  virtual string16 GetBubbleTitle() const OVERRIDE;
  virtual string16 GetBubbleMessage() const OVERRIDE;
  virtual string16 GetApplyButtonText() const OVERRIDE;
  virtual string16 GetDiscardButtonText() const OVERRIDE;

 private:
  virtual ~SessionStartupChange();

  // Opens all tabs in |tabs| and makes them pinned.
  void OpenPinnedTabs(Browser* browser, const PinnedTabCodec::Tabs& tabs);

  SessionStartupPref new_startup_pref_;
  SessionStartupPref backup_startup_pref_;
  PinnedTabCodec::Tabs new_pinned_tabs_;
  PinnedTabCodec::Tabs backup_pinned_tabs_;

  DISALLOW_COPY_AND_ASSIGN(SessionStartupChange);
};

SessionStartupChange::SessionStartupChange(
    const SessionStartupPref& actual_startup_pref,
    const PinnedTabCodec::Tabs& actual_pinned_tabs,
    const SessionStartupPref& backup_startup_pref,
    const PinnedTabCodec::Tabs& backup_pinned_tabs)
    : new_startup_pref_(actual_startup_pref),
      backup_startup_pref_(backup_startup_pref),
      new_pinned_tabs_(actual_pinned_tabs),
      backup_pinned_tabs_(backup_pinned_tabs) {
  UMA_HISTOGRAM_ENUMERATION(
      kProtectorHistogramStartupSettingsChanged,
      actual_startup_pref.type,
      SessionStartupPref::TYPE_COUNT);
}

SessionStartupChange::~SessionStartupChange() {
}

bool SessionStartupChange::Init(Profile* profile) {
  if (!BasePrefsChange::Init(profile))
    return false;
  SessionStartupPref::SetStartupPref(profile, backup_startup_pref_);
  PinnedTabCodec::WritePinnedTabs(profile, backup_pinned_tabs_);
  DismissOnPrefChange(prefs::kRestoreOnStartup);
  DismissOnPrefChange(prefs::kURLsToRestoreOnStartup);
  DismissOnPrefChange(prefs::kPinnedTabs);
  return true;
}

void SessionStartupChange::Apply(Browser* browser) {
  UMA_HISTOGRAM_ENUMERATION(
      kProtectorHistogramStartupSettingsApplied,
      new_startup_pref_.type,
      SessionStartupPref::TYPE_COUNT);
  IgnorePrefChanges();
  SessionStartupPref::SetStartupPref(profile(), new_startup_pref_);
  PinnedTabCodec::WritePinnedTabs(profile(), new_pinned_tabs_);
  OpenPinnedTabs(browser, new_pinned_tabs_);
}

void SessionStartupChange::Discard(Browser* browser) {
  UMA_HISTOGRAM_ENUMERATION(
      kProtectorHistogramStartupSettingsDiscarded,
      new_startup_pref_.type,
      SessionStartupPref::TYPE_COUNT);
  IgnorePrefChanges();
  // Nothing to do here since backup has already been made active by Init().
}

void SessionStartupChange::Timeout() {
  UMA_HISTOGRAM_ENUMERATION(
      kProtectorHistogramStartupSettingsTimeout,
      new_startup_pref_.type,
      SessionStartupPref::TYPE_COUNT);
}

int SessionStartupChange::GetBadgeIconID() const {
  // Icons are the same for homepage and startup settings.
  return IDR_HOMEPAGE_CHANGE_BADGE;
}

int SessionStartupChange::GetMenuItemIconID() const {
  return IDR_HOMEPAGE_CHANGE_MENU;
}

int SessionStartupChange::GetBubbleIconID() const {
  return IDR_HOMEPAGE_CHANGE_ALERT;
}

string16 SessionStartupChange::GetBubbleTitle() const {
  return l10n_util::GetStringUTF16(IDS_STARTUP_SETTINGS_CHANGE_TITLE);
}

string16 SessionStartupChange::GetBubbleMessage() const {
  return l10n_util::GetStringUTF16(IDS_STARTUP_SETTINGS_CHANGE_BUBBLE_MESSAGE);
}

string16 SessionStartupChange::GetApplyButtonText() const {
  if (new_startup_pref_.type == SessionStartupPref::LAST)
      return l10n_util::GetStringUTF16(IDS_CHANGE_STARTUP_SETTINGS_RESTORE);

  string16 first_url;
  if (new_startup_pref_.type == SessionStartupPref::URLS &&
      !new_startup_pref_.urls.empty()) {
    // Display the domain name of the first statrup URL.
    first_url = UTF8ToUTF16(new_startup_pref_.urls[0].host());
  } else if (!new_pinned_tabs_.empty()) {
    // Start with NTP or no URLs (basically the same): display the domain name
    // of the first pinned tab, if any.
    first_url = UTF8ToUTF16(new_pinned_tabs_[0].url.host());
  }
  return first_url.empty() ?
      l10n_util::GetStringUTF16(IDS_CHANGE_STARTUP_SETTINGS_NTP) :
      l10n_util::GetStringFUTF16(IDS_CHANGE_STARTUP_SETTINGS_URLS,
                                 first_url);
}

string16 SessionStartupChange::GetDiscardButtonText() const {
  return l10n_util::GetStringUTF16(IDS_KEEP_SETTING);
}

void SessionStartupChange::OpenPinnedTabs(
    Browser* browser,
    const PinnedTabCodec::Tabs& tabs) {
  for (size_t i = 0; i < tabs.size(); ++i) {
    browser::NavigateParams params(browser, tabs[i].url,
                                   content::PAGE_TRANSITION_START_PAGE);
    params.disposition = NEW_BACKGROUND_TAB;
    params.tabstrip_index = -1;
    params.tabstrip_add_types = TabStripModel::ADD_PINNED;
    params.extension_app_id = tabs[i].app_id;
    browser::Navigate(&params);
  }
}

BaseSettingChange* CreateSessionStartupChange(
    const SessionStartupPref& actual_startup_pref,
    const PinnedTabCodec::Tabs& actual_pinned_tabs,
    const SessionStartupPref& backup_startup_pref,
    const PinnedTabCodec::Tabs& backup_pinned_tabs) {
  return new SessionStartupChange(actual_startup_pref, actual_pinned_tabs,
                                  backup_startup_pref, backup_pinned_tabs);
}

}  // namespace protector
