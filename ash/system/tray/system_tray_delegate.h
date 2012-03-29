// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_TRAY_SYSTEM_TRAY_DELEGATE_H_
#define ASH_SYSTEM_TRAY_SYSTEM_TRAY_DELEGATE_H_
#pragma once

#include <string>
#include <vector>

#include "ash/ash_export.h"
#include "ash/system/user/login_status.h"
#include "base/i18n/time_formatting.h"
#include "base/string16.h"
#include "third_party/skia/include/core/SkBitmap.h"

class SkBitmap;

namespace ash {

struct ASH_EXPORT NetworkIconInfo {
  NetworkIconInfo();
  ~NetworkIconInfo();

  bool highlight;
  bool tray_icon_visible;
  SkBitmap image;
  string16 name;
  string16 description;
  std::string service_path;
};

struct ASH_EXPORT BluetoothDeviceInfo {
  BluetoothDeviceInfo();
  ~BluetoothDeviceInfo();

  std::string address;
  string16 display_name;
  bool connected;
};

typedef std::vector<BluetoothDeviceInfo> BluetoothDeviceList;

struct ASH_EXPORT IMEPropertyInfo {
  IMEPropertyInfo();
  ~IMEPropertyInfo();

  bool selected;
  std::string key;
  string16 name;
};

typedef std::vector<IMEPropertyInfo> IMEPropertyInfoList;

struct ASH_EXPORT IMEInfo {
  IMEInfo();
  ~IMEInfo();

  bool selected;
  std::string id;
  string16 name;
  string16 short_name;
};

typedef std::vector<IMEInfo> IMEInfoList;

struct PowerSupplyStatus;

class SystemTrayDelegate {
 public:
  virtual ~SystemTrayDelegate() {}

  // Returns true if system tray should be visible on startup.
  virtual bool GetTrayVisibilityOnStartup() = 0;

  // Gets information about the logged in user.
  virtual const std::string GetUserDisplayName() const = 0;
  virtual const std::string GetUserEmail() const = 0;
  virtual const SkBitmap& GetUserImage() const = 0;
  virtual user::LoginStatus GetUserLoginStatus() const = 0;

  // Returns whether a system upgrade is available.
  virtual bool SystemShouldUpgrade() const = 0;

  // Returns the resource id for the icon to show for the update notification.
  virtual int GetSystemUpdateIconResource() const = 0;

  // Returns the desired hour clock type.
  virtual base::HourClockType GetHourClockType() const = 0;

  // Gets the current power supply status.
  virtual PowerSupplyStatus GetPowerSupplyStatus() const = 0;

  // Shows settings.
  virtual void ShowSettings() = 0;

  // Shows the settings related to date, timezone etc.
  virtual void ShowDateSettings() = 0;

  // Shows the settings related to network.
  virtual void ShowNetworkSettings() = 0;

  // Shows the settings related to bluetooth.
  virtual void ShowBluetoothSettings() = 0;

  // Shows settings related to input methods.
  virtual void ShowIMESettings() = 0;

  // Shows help.
  virtual void ShowHelp() = 0;

  // Is the system audio muted?
  virtual bool IsAudioMuted() const = 0;

  // Mutes/Unmutes the audio system.
  virtual void SetAudioMuted(bool muted) = 0;

  // Gets the volume level.
  virtual float GetVolumeLevel() const = 0;

  // Sets the volume level.
  virtual void SetVolumeLevel(float level) = 0;

  // Gets whether the caps lock is on.
  virtual bool IsCapsLockOn() const = 0;

  // Gets whether accessibility mode is turned on.
  virtual bool IsInAccessibilityMode() const = 0;

  // Attempts to shut down the system.
  virtual void ShutDown() = 0;

  // Attempts to sign out the user.
  virtual void SignOut() = 0;

  // Attempts to lock the screen.
  virtual void RequestLockScreen() = 0;

  // Attempts to restart the system.
  virtual void RequestRestart() = 0;

  // Returns a list of available bluetooth devices.
  virtual void GetAvailableBluetoothDevices(BluetoothDeviceList* devices) = 0;

  // Toggles connection to a specific bluetooth device.
  virtual void ToggleBluetoothConnection(const std::string& address) = 0;

  // Returns the currently selected IME.
  virtual void GetCurrentIME(IMEInfo* info) = 0;

  // Returns a list of availble IMEs.
  virtual void GetAvailableIMEList(IMEInfoList* list) = 0;

  // Returns a list of properties for the currently selected IME.
  virtual void GetCurrentIMEProperties(IMEPropertyInfoList* list) = 0;

  // Switches to the selected input method.
  virtual void SwitchIME(const std::string& ime_id) = 0;

  // Activates an IME property.
  virtual void ActivateIMEProperty(const std::string& key) = 0;

  // Returns information about the most relevant network. Relevance is
  // determined by the implementor (e.g. a connecting network may be more
  // relevant over a connected network etc.)
  virtual void GetMostRelevantNetworkIcon(NetworkIconInfo* info,
                                          bool large) = 0;

  // Returns information about the available networks.
  virtual void GetAvailableNetworks(std::vector<NetworkIconInfo>* list) = 0;

  // Connects to the network specified by the unique id.
  virtual void ConnectToNetwork(const std::string& network_id) = 0;

  // Gets the network IP address, and the mac addresses for the ethernet and
  // wifi devices. If any of this is unavailable, empty strings are returned.
  virtual void GetNetworkAddresses(std::string* ip_address,
                                   std::string* ethernet_mac_address,
                                   std::string* wifi_mac_address) = 0;

  // Shous UI to add a new bluetooth device.
  virtual void AddBluetoothDevice() = 0;

  // Toggles airplane mode.
  virtual void ToggleAirplaneMode() = 0;

  // Toggles wifi network.
  virtual void ToggleWifi() = 0;

  // Toggles cellular network.
  virtual void ToggleCellular() = 0;

  // Toggles bluetooth.
  virtual void ToggleBluetooth() = 0;

  // Shows UI to connect to an unlisted wifi network.
  virtual void ShowOtherWifi() = 0;

  // Shows UI to search for cellular networks.
  virtual void ShowOtherCellular() = 0;

  // Returns whether wifi is available.
  virtual bool GetWifiAvailable() = 0;

  // Returns whether cellular networking is available.
  virtual bool GetCellularAvailable() = 0;

  // Returns whether bluetooth capability is available.
  virtual bool GetBluetoothAvailable() = 0;

  // Returns whether wifi is enabled.
  virtual bool GetWifiEnabled() = 0;

  // Returns whether cellular networking is enabled.
  virtual bool GetCellularEnabled() = 0;

  // Returns whether bluetooth is enabled.
  virtual bool GetBluetoothEnabled() = 0;

  // Returns whether cellular scanning is supported.
  virtual bool GetCellularScanSupported() = 0;

  // Retrieves information about the carrier. If the information cannot be
  // retrieved, returns false.
  virtual bool GetCellularCarrierInfo(std::string* carrier_id,
                                      std::string* toup_url) = 0;

  // Opens the top up url.
  virtual void ShowCellularTopupURL(const std::string& topup_url) = 0;

  // Shows UI for changing proxy settings.
  virtual void ChangeProxySettings() = 0;
};

}  // namespace ash

#endif  // ASH_SYSTEM_TRAY_SYSTEM_TRAY_DELEGATE_H_
