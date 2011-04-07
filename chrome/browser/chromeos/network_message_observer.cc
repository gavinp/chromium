// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/network_message_observer.h"

#include "base/callback.h"
#include "base/stl_util-inl.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/network_library.h"
#include "chrome/browser/chromeos/notifications/balloon_view_host.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/time_format.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace {

// Returns prefs::kShowPlanNotifications in the profile of the last active
// browser. If there is no active browser, returns true.
bool ShouldShowMobilePlanNotifications() {
  Browser* browser = BrowserList::GetLastActive();
  if (!browser || !browser->profile())
    return true;

  PrefService* prefs = browser->profile()->GetPrefs();
  return prefs->GetBoolean(prefs::kShowPlanNotifications);
}

}  // namespace

namespace chromeos {

NetworkMessageObserver::NetworkMessageObserver(Profile* profile)
    : notification_connection_error_(profile, "network_connection.chromeos",
          IDR_NOTIFICATION_NETWORK_FAILED,
          l10n_util::GetStringUTF16(IDS_NETWORK_CONNECTION_ERROR_TITLE)),
      notification_low_data_(profile, "network_low_data.chromeos",
          IDR_NOTIFICATION_BARS_CRITICAL,
          l10n_util::GetStringUTF16(IDS_NETWORK_LOW_DATA_TITLE)),
      notification_no_data_(profile, "network_no_data.chromeos",
          IDR_NOTIFICATION_BARS_EMPTY,
          l10n_util::GetStringUTF16(IDS_NETWORK_OUT_OF_DATA_TITLE)) {
  NetworkLibrary* netlib = CrosLibrary::Get()->GetNetworkLibrary();
  OnNetworkManagerChanged(netlib);
  // Note that this gets added as a NetworkManagerObserver,
  // CellularDataPlanObserver, and UserActionObserver in browser_init.cc
}

NetworkMessageObserver::~NetworkMessageObserver() {
  NetworkLibrary* netlib = CrosLibrary::Get()->GetNetworkLibrary();
  netlib->RemoveNetworkManagerObserver(this);
  netlib->RemoveCellularDataPlanObserver(this);
  netlib->RemoveUserActionObserver(this);
  notification_connection_error_.Hide();
  notification_low_data_.Hide();
  notification_no_data_.Hide();
  STLDeleteValues(&cellular_networks_);
  STLDeleteValues(&wifi_networks_);
}

// static
bool NetworkMessageObserver::IsApplicableBackupPlan(
    const CellularDataPlan* plan, const CellularDataPlan* other_plan) {
  // By applicable plan, we mean that the other plan has data AND the timeframe
  // will apply: (unlimited OR used bytes < max bytes) AND
  //   ((start time - 1 sec) <= end time of currently active plan).
  // In other words, there is data available and there is no gap of more than a
  // second in time between the old plan and the new plan.
  bool has_data = other_plan->plan_type == CELLULAR_DATA_PLAN_UNLIMITED ||
      other_plan->remaining_data() > 0;
  bool will_apply =
      (other_plan->plan_start_time - plan->plan_end_time).InSeconds() <= 1;
  return has_data && will_apply;
}

void NetworkMessageObserver::OpenMobileSetupPage(const ListValue* args) {
  Browser* browser = BrowserList::GetLastActive();
  if (browser)
    browser->OpenMobilePlanTabAndActivate();
}

void NetworkMessageObserver::OpenMoreInfoPage(const ListValue* args) {
  Browser* browser = BrowserList::GetLastActive();
  if (!browser)
    return;
  chromeos::NetworkLibrary* lib =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  const chromeos::CellularNetwork* cellular = lib->cellular_network();
  if (!cellular)
    return;
  browser->ShowSingletonTab(GURL(cellular->payment_url()));
}

void NetworkMessageObserver::InitNewPlan(const CellularDataPlan* plan) {
  notification_low_data_.Hide();
  notification_no_data_.Hide();
  if (plan->plan_type == CELLULAR_DATA_PLAN_UNLIMITED) {
    notification_no_data_.set_title(
        l10n_util::GetStringFUTF16(IDS_NETWORK_DATA_EXPIRED_TITLE,
                                   ASCIIToUTF16(plan->plan_name)));
    notification_low_data_.set_title(
        l10n_util::GetStringFUTF16(IDS_NETWORK_NEARING_EXPIRATION_TITLE,
                                   ASCIIToUTF16(plan->plan_name)));
  } else {
    notification_no_data_.set_title(
        l10n_util::GetStringFUTF16(IDS_NETWORK_OUT_OF_DATA_TITLE,
                                   ASCIIToUTF16(plan->plan_name)));
    notification_low_data_.set_title(
        l10n_util::GetStringFUTF16(IDS_NETWORK_LOW_DATA_TITLE,
                                   ASCIIToUTF16(plan->plan_name)));
  }
}

void NetworkMessageObserver::ShowNeedsPlanNotification(
    const CellularNetwork* cellular) {
  notification_no_data_.set_title(
      l10n_util::GetStringFUTF16(IDS_NETWORK_NO_DATA_PLAN_TITLE,
                                 UTF8ToUTF16(cellular->name())));
  notification_no_data_.Show(
      l10n_util::GetStringFUTF16(
          IDS_NETWORK_NO_DATA_PLAN_MESSAGE,
          UTF8ToUTF16(cellular->name())),
      l10n_util::GetStringUTF16(IDS_NETWORK_PURCHASE_MORE_MESSAGE),
      NewCallback(this, &NetworkMessageObserver::OpenMobileSetupPage),
      false, false);
}

void NetworkMessageObserver::ShowNoDataNotification(
    CellularDataPlanType plan_type) {
  notification_low_data_.Hide();  // Hide previous low data notification.
  string16 message = plan_type == CELLULAR_DATA_PLAN_UNLIMITED ?
      TimeFormat::TimeRemaining(base::TimeDelta()) :
      l10n_util::GetStringFUTF16(IDS_NETWORK_DATA_REMAINING_MESSAGE,
                                 ASCIIToUTF16("0"));
  notification_no_data_.Show(message,
      l10n_util::GetStringUTF16(IDS_NETWORK_PURCHASE_MORE_MESSAGE),
      NewCallback(this, &NetworkMessageObserver::OpenMobileSetupPage),
      false, false);
}

void NetworkMessageObserver::ShowLowDataNotification(
    const CellularDataPlan* plan) {
  string16 message;
  if (plan->plan_type == CELLULAR_DATA_PLAN_UNLIMITED) {
    message = plan->GetPlanExpiration();
  } else {
    int64 remaining_mbytes = plan->remaining_data() / (1024 * 1024);
    message = l10n_util::GetStringFUTF16(IDS_NETWORK_DATA_REMAINING_MESSAGE,
        UTF8ToUTF16(base::Int64ToString(remaining_mbytes)));
  }
  notification_low_data_.Show(message,
      l10n_util::GetStringUTF16(IDS_NETWORK_MORE_INFO_MESSAGE),
      NewCallback(this, &NetworkMessageObserver::OpenMoreInfoPage),
      false, false);
}

void NetworkMessageObserver::OnNetworkManagerChanged(NetworkLibrary* obj) {
  const WifiNetworkVector& wifi_networks = obj->wifi_networks();
  const CellularNetworkVector& cellular_networks = obj->cellular_networks();

  std::string new_failed_network;
  // Check to see if we have any newly failed wifi network.
  for (WifiNetworkVector::const_iterator it = wifi_networks.begin();
       it < wifi_networks.end(); it++) {
    const WifiNetwork* wifi = *it;
    if (wifi->failed()) {
      ServicePathWifiMap::iterator iter =
          wifi_networks_.find(wifi->service_path());
      // If the network did not previously exist, then don't do anything.
      // For example, if the user travels to a location and finds a service
      // that has previously failed, we don't want to show a notification.
      if (iter == wifi_networks_.end())
        continue;

      const WifiNetwork* wifi_old = iter->second;
      // If network connection failed, display a notification.
      // We only do this if we were trying to make a new connection.
      // So if a previously connected network got disconnected for any reason,
      // we don't display notification.
      if (wifi_old->connecting()) {
        new_failed_network = wifi->name();
        // Like above, there should only be one newly failed network.
        break;
      }
    }
  }

  // Refresh stored networks.
  STLDeleteValues(&wifi_networks_);
  wifi_networks_.clear();
  for (WifiNetworkVector::const_iterator it = wifi_networks.begin();
       it < wifi_networks.end(); it++) {
    const WifiNetwork* wifi = *it;
    wifi_networks_[wifi->service_path()] = new WifiNetwork(*wifi);
  }

  STLDeleteValues(&cellular_networks_);
  cellular_networks_.clear();
  for (CellularNetworkVector::const_iterator it = cellular_networks.begin();
       it < cellular_networks.end(); it++) {
    const CellularNetwork* cellular = *it;
    cellular_networks_[cellular->service_path()] =
        new CellularNetwork(*cellular);
  }

  // Show connection error notification if necessary.
  if (!new_failed_network.empty()) {
    // Hide if already shown to force show it in case user has closed it.
    if (notification_connection_error_.visible())
      notification_connection_error_.Hide();
    notification_connection_error_.Show(l10n_util::GetStringFUTF16(
        IDS_NETWORK_CONNECTION_ERROR_MESSAGE,
        ASCIIToUTF16(new_failed_network)), false, false);
  }
}

void NetworkMessageObserver::OnCellularDataPlanChanged(NetworkLibrary* cros) {
  if (!ShouldShowMobilePlanNotifications()) {
    return;
  }

  const CellularNetwork* cellular = cros->cellular_network();
  if (!cellular)
    return;
  const CellularDataPlanVector* plans =
      cros->GetDataPlans(cellular->service_path());

  // If no plans available, check to see if we need a new plan.
  if (!plans || plans->empty()) {
    // If previously, we had a low data notification, we know that a plan was
    // near expiring. In that case, because the plan has disappeared, we assume
    // that it expired.
    // Note: even if a user dismissed the notification, it's still visible.
    if (notification_low_data_.visible()) {
      ShowNoDataNotification(cellular_data_plan_type_);
    } else if (cellular->needs_new_plan()) {
      ShowNeedsPlanNotification(cellular);
    }
    return;
  }

  CellularDataPlanVector::const_iterator iter = plans->begin();
  const CellularDataPlan* current_plan = *iter;

  // If current plan is not the last plan (there is another backup plan),
  // then we do not show notifications for this plan.
  // For example, if there is another data plan available when this runs out.
  for (++iter; iter != plans->end(); ++iter) {
    if (IsApplicableBackupPlan(current_plan, *iter)) {
      return;
    }
  }

  // If connected cellular network changed, or data plan is different, then
  // it's a new network. Then hide all previous notifications.
  bool new_plan = cellular->service_path() != cellular_service_path_ ||
      current_plan->GetUniqueIdentifier() != cellular_data_plan_unique_id_;

  if (new_plan) {
    InitNewPlan(current_plan);
  }

  if (cellular->data_left() == CellularNetwork::DATA_NONE) {
    ShowNoDataNotification(current_plan->plan_type);
  } else if (cellular->data_left() == CellularNetwork::DATA_VERY_LOW) {
    ShowLowDataNotification(current_plan);
  }

  cellular_service_path_ = cellular->service_path();
  cellular_data_plan_unique_id_ = current_plan->GetUniqueIdentifier();
  cellular_data_plan_type_ = current_plan->plan_type;
}

void NetworkMessageObserver::OnConnectionInitiated(NetworkLibrary* obj,
                                                   const Network* network) {
  // If user initiated any network connection, we hide the error notification.
  notification_connection_error_.Hide();
}

}  // namespace chromeos
