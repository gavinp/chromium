// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/protector/protected_prefs_watcher.h"

#include "base/base64.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/stringprintf.h"
#include "base/values.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/prefs/pref_set_observer.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/protector/histograms.h"
#include "chrome/browser/protector/protector_service.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/notification_service.h"

namespace protector {

namespace {

const char kBackupPrefsPrefix[] = "backup.";

// Backup pref names.
const char kBackupHomePage[] = "backup.homepage";
const char kBackupHomePageIsNewTabPage[] = "backup.homepage_is_newtabpage";
const char kBackupShowHomeButton[] = "backup.browser.show_home_button";
const char kBackupRestoreOnStartup[] = "backup.session.restore_on_startup";
const char kBackupURLsToRestoreOnStartup[] =
    "backup.session.urls_to_restore_on_startup";
const char kBackupPinnedTabs[] = "backup.pinned_tabs";
const char kBackupExtensionsIDs[] = "backup.extensions.ids";
const char kBackupSignature[] = "backup._signature";
const char kBackupVersion[] = "backup._version";

}  // namespace

// static
const int ProtectedPrefsWatcher::kCurrentVersionNumber = 2;

ProtectedPrefsWatcher::ProtectedPrefsWatcher(Profile* profile)
    : is_backup_valid_(true),
      profile_(profile) {
  // Perform necessary pref migrations before actually starting to observe
  // pref changes, otherwise the migration would affect the backup data as well.
  EnsurePrefsMigration();
  pref_observer_.reset(PrefSetObserver::CreateProtectedPrefSetObserver(
      profile->GetPrefs(), this));
  UpdateCachedPrefs();
  ValidateBackup();
  VLOG(1) << "Initialized pref watcher";
}

ProtectedPrefsWatcher::~ProtectedPrefsWatcher() {
}

// static
void ProtectedPrefsWatcher::RegisterUserPrefs(PrefService* prefs) {
  prefs->RegisterStringPref(kBackupHomePage, "",
                            PrefService::UNSYNCABLE_PREF);
  prefs->RegisterBooleanPref(kBackupHomePageIsNewTabPage, false,
                            PrefService::UNSYNCABLE_PREF);
  prefs->RegisterBooleanPref(kBackupShowHomeButton, false,
                             PrefService::UNSYNCABLE_PREF);
  prefs->RegisterIntegerPref(kBackupRestoreOnStartup, 0,
                             PrefService::UNSYNCABLE_PREF);
  prefs->RegisterListPref(kBackupURLsToRestoreOnStartup,
                          PrefService::UNSYNCABLE_PREF);
  prefs->RegisterListPref(kBackupPinnedTabs,
                          PrefService::UNSYNCABLE_PREF);
  prefs->RegisterListPref(kBackupExtensionsIDs,
                          PrefService::UNSYNCABLE_PREF);
  prefs->RegisterStringPref(kBackupSignature, "",
                            PrefService::UNSYNCABLE_PREF);
  prefs->RegisterIntegerPref(kBackupVersion, 1,
                             PrefService::UNSYNCABLE_PREF);
}

bool ProtectedPrefsWatcher::DidPrefChange(const std::string& path) const {
  const base::Value* backup_value = GetBackupForPref(path);
  if (!backup_value) {
    LOG(WARNING) << "No backup for " << path;
    return false;
  }
  const PrefService::Preference* new_pref =
      profile_->GetPrefs()->FindPreference(path.c_str());
  DCHECK(new_pref);
  return !backup_value->Equals(new_pref->GetValue());
}

const base::Value* ProtectedPrefsWatcher::GetBackupForPref(
    const std::string& path) const {
  if (!is_backup_valid_)
    return NULL;
  std::string backup_path = std::string(kBackupPrefsPrefix) + path;
  const PrefService::Preference* backup_pref =
      profile_->GetPrefs()->FindPreference(backup_path.c_str());
  DCHECK(backup_pref &&
         // These do not directly correspond to any real preference.
         backup_path != kBackupExtensionsIDs &&
         backup_path != kBackupSignature);
  return backup_pref->GetValue();
}

void ProtectedPrefsWatcher::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK(type == chrome::NOTIFICATION_PREF_CHANGED);
  const std::string* pref_name = content::Details<std::string>(details).ptr();
  DCHECK(pref_name && pref_observer_->IsObserved(*pref_name));
  if (UpdateBackupEntry(*pref_name))
    UpdateBackupSignature();
}

void ProtectedPrefsWatcher::EnsurePrefsMigration() {
  // Force SessionStartupPref migration, if necessary.
  SessionStartupPref::GetStartupPref(profile_);
}

bool ProtectedPrefsWatcher::UpdateCachedPrefs() {
  // Direct access to the extensions prefs is required becase ExtensionService
  // may not yet have been initialized.
  ExtensionPrefs::ExtensionIdSet extension_ids =
      ExtensionPrefs::GetExtensionsFrom(
          profile_->GetPrefs()->GetDictionary(ExtensionPrefs::kExtensionsPref));
  if (extension_ids == cached_extension_ids_)
    return false;
  cached_extension_ids_.swap(extension_ids);
  return true;
}

bool ProtectedPrefsWatcher::HasBackup() const {
  // TODO(ivankr): as soon as some irreversible change to Preferences happens,
  // add a condition that this change has occured as well (otherwise it's
  // possible to simply clear the "backup" dictionary to make settings
  // unprotected).
  return profile_->GetPrefs()->HasPrefPath(kBackupSignature);
}

void ProtectedPrefsWatcher::InitBackup() {
  PrefService* prefs = profile_->GetPrefs();
  prefs->SetString(kBackupHomePage, prefs->GetString(prefs::kHomePage));
  prefs->SetBoolean(kBackupHomePageIsNewTabPage,
                    prefs->GetBoolean(prefs::kHomePageIsNewTabPage));
  prefs->SetBoolean(kBackupShowHomeButton,
                    prefs->GetBoolean(prefs::kShowHomeButton));
  prefs->SetInteger(kBackupRestoreOnStartup,
                    prefs->GetInteger(prefs::kRestoreOnStartup));
  prefs->Set(kBackupURLsToRestoreOnStartup,
             *prefs->GetList(prefs::kURLsToRestoreOnStartup));
  prefs->Set(kBackupPinnedTabs, *prefs->GetList(prefs::kPinnedTabs));
  ListPrefUpdate extension_ids_update(prefs, kBackupExtensionsIDs);
  base::ListValue* extension_ids = extension_ids_update.Get();
  extension_ids->Clear();
  for (ExtensionPrefs::ExtensionIdSet::const_iterator it =
           cached_extension_ids_.begin();
       it != cached_extension_ids_.end(); ++it) {
    extension_ids->Append(base::Value::CreateStringValue(*it));
  }
  prefs->SetInteger(kBackupVersion, kCurrentVersionNumber);
  UpdateBackupSignature();
}

void ProtectedPrefsWatcher::MigrateOldBackupIfNeeded() {
  PrefService* prefs = profile_->GetPrefs();

  int current_version = prefs->GetInteger(kBackupVersion);
  VLOG(1) << "Backup version: " << current_version;
  if (current_version == kCurrentVersionNumber)
    return;

  switch (current_version) {
    case 1:
      prefs->Set(kBackupPinnedTabs, *prefs->GetList(prefs::kPinnedTabs));
      // FALL THROUGH
  }

  prefs->SetInteger(kBackupVersion, kCurrentVersionNumber);
  UpdateBackupSignature();
}

bool ProtectedPrefsWatcher::UpdateBackupEntry(const std::string& pref_name) {
  PrefService* prefs = profile_->GetPrefs();
  if (pref_name == ExtensionPrefs::kExtensionsPref) {
    // For changes in extension dictionary, do nothing if the IDs list remained
    // the same.
    if (!UpdateCachedPrefs())
      return false;
    ListPrefUpdate extension_ids_update(prefs, kBackupExtensionsIDs);
    base::ListValue* extension_ids = extension_ids_update.Get();
    extension_ids->Clear();
    for (ExtensionPrefs::ExtensionIdSet::const_iterator it =
             cached_extension_ids_.begin();
         it != cached_extension_ids_.end(); ++it) {
      extension_ids->Append(base::Value::CreateStringValue(*it));
    }
  } else if (pref_name == prefs::kHomePage) {
    prefs->SetString(kBackupHomePage, prefs->GetString(prefs::kHomePage));
  } else if (pref_name == prefs::kHomePageIsNewTabPage) {
    prefs->SetBoolean(kBackupHomePageIsNewTabPage,
                      prefs->GetBoolean(prefs::kHomePageIsNewTabPage));
  } else if (pref_name == prefs::kShowHomeButton) {
    prefs->SetBoolean(kBackupShowHomeButton,
                      prefs->GetBoolean(prefs::kShowHomeButton));
  } else if (pref_name == prefs::kRestoreOnStartup) {
    prefs->SetInteger(kBackupRestoreOnStartup,
                      prefs->GetInteger(prefs::kRestoreOnStartup));
  } else if (pref_name == prefs::kURLsToRestoreOnStartup) {
    prefs->Set(kBackupURLsToRestoreOnStartup,
               *prefs->GetList(prefs::kURLsToRestoreOnStartup));
  } else if (pref_name == prefs::kPinnedTabs) {
    prefs->Set(kBackupPinnedTabs, *prefs->GetList(prefs::kPinnedTabs));
  } else {
    NOTREACHED();
    return false;
  }
  VLOG(1) << "Updated backup entry for: " << pref_name;
  return true;
}

void ProtectedPrefsWatcher::UpdateBackupSignature() {
  PrefService* prefs = profile_->GetPrefs();
  std::string signed_data = GetSignatureData(prefs);
  DCHECK(!signed_data.empty());
  std::string signature = SignSetting(signed_data);
  DCHECK(!signature.empty());
  std::string signature_base64;
  if (!base::Base64Encode(signature, &signature_base64))
    NOTREACHED();
  prefs->SetString(kBackupSignature, signature_base64);
  // Schedule disk write on FILE thread as soon as possible.
  prefs->CommitPendingWrite();
  VLOG(1) << "Updated backup signature";
}

bool ProtectedPrefsWatcher::IsSignatureValid() const {
  DCHECK(HasBackup());
  PrefService* prefs = profile_->GetPrefs();
  std::string signed_data = GetSignatureData(prefs);
  DCHECK(!signed_data.empty());
  std::string signature;
  if (!base::Base64Decode(prefs->GetString(kBackupSignature), &signature))
    return false;
  return IsSettingValid(signed_data, signature);
}

void ProtectedPrefsWatcher::ValidateBackup() {
  if (!HasBackup()) {
    // Create initial backup entries and sign them.
    InitBackup();
    UMA_HISTOGRAM_ENUMERATION(
        kProtectorHistogramPrefs,
        kProtectorErrorValueValidZero,
        kProtectorErrorCount);
  } else if (IsSignatureValid()) {
    MigrateOldBackupIfNeeded();
    UMA_HISTOGRAM_ENUMERATION(
        kProtectorHistogramPrefs,
        kProtectorErrorValueValid,
        kProtectorErrorCount);
  } else {
    LOG(WARNING) << "Invalid backup signature";
    is_backup_valid_ = false;
    // The whole backup has been compromised, overwrite it.
    InitBackup();
    UMA_HISTOGRAM_ENUMERATION(
        kProtectorHistogramPrefs,
        kProtectorErrorBackupInvalid,
        kProtectorErrorCount);
  }
}

std::string ProtectedPrefsWatcher::GetSignatureData(PrefService* prefs) const {
  int current_version = prefs->GetInteger(kBackupVersion);
  // TODO(ivankr): replace this with some existing reliable serializer.
  // JSONWriter isn't a good choice because JSON formatting may change suddenly.
  std::string data = base::StringPrintf(
      "%s|%d|%d|%d",
      prefs->GetString(kBackupHomePage).c_str(),
      prefs->GetBoolean(kBackupHomePageIsNewTabPage) ? 1 : 0,
      prefs->GetBoolean(kBackupShowHomeButton) ? 1 : 0,
      prefs->GetInteger(kBackupRestoreOnStartup));
  const base::ListValue* startup_urls =
      prefs->GetList(kBackupURLsToRestoreOnStartup);
  for (base::ListValue::const_iterator it = startup_urls->begin();
       it != startup_urls->end(); ++it) {
    std::string url;
    if (!(*it)->GetAsString(&url))
      NOTREACHED();
    base::StringAppendF(&data, "|%s", url.c_str());
  }
  // These are safe to use becase they are always up-to-date and returned in
  // a consistent (sorted) order.
  for (ExtensionPrefs::ExtensionIdSet::const_iterator it =
           cached_extension_ids_.begin();
       it != cached_extension_ids_.end(); ++it) {
    base::StringAppendF(&data, "|%s", it->c_str());
  }
  if (current_version >= 2) {
    // Version itself is included only since version 2 since it wasn't there
    // in version 1.
    base::StringAppendF(&data, "|v%d", current_version);
    const base::ListValue* pinned_tabs = prefs->GetList(kBackupPinnedTabs);
    for (base::ListValue::const_iterator it = pinned_tabs->begin();
         it != pinned_tabs->end(); ++it) {
      const base::DictionaryValue* tab = NULL;
      if (!(*it)->GetAsDictionary(&tab)) {
        NOTREACHED();
        continue;
      }
      for (base::DictionaryValue::Iterator it2(*tab); it2.HasNext();
           it2.Advance()) {
        std::string value;
        if (!it2.value().GetAsString(&value))
          NOTREACHED();
        base::StringAppendF(&data, "|%s|%s", it2.key().c_str(), value.c_str());
      }
    }
  }
  return data;
}

}  // namespace protector
