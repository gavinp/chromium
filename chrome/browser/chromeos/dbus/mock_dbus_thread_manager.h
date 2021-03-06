// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DBUS_MOCK_DBUS_THREAD_MANAGER_H_
#define CHROME_BROWSER_CHROMEOS_DBUS_MOCK_DBUS_THREAD_MANAGER_H_

#include <string>

#include "chrome/browser/chromeos/dbus/dbus_thread_manager.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace dbus {

class Bus;

}  // namespace dbus

namespace chromeos {

class  MockBluetoothAdapterClient;
class  MockBluetoothDeviceClient;
class  MockBluetoothInputClient;
class  MockBluetoothManagerClient;
class  MockBluetoothNodeClient;
class  MockCashewClient;
class  MockCrosDisksClient;
class  MockCryptohomeClient;
class  MockFlimflamNetworkClient;
class  MockImageBurnerClient;
class  MockIntrospectableClient;
class  MockPowerManagerClient;
class  MockSessionManagerClient;
class  MockSpeechSynthesizerClient;
class  MockUpdateEngineClient;

// This class provides a mock DBusThreadManager with mock clients
// installed. You can customize the behaviors of mock clients with
// mock_foo_client() functions.
class MockDBusThreadManager : public DBusThreadManager {
 public:
  MockDBusThreadManager();
  virtual ~MockDBusThreadManager();

  MOCK_METHOD0(GetSystemBus, dbus::Bus*(void));
  MOCK_METHOD0(GetBluetoothAdapterClient, BluetoothAdapterClient*(void));
  MOCK_METHOD0(GetBluetoothDeviceClient, BluetoothDeviceClient*(void));
  MOCK_METHOD0(GetBluetoothInputClient, BluetoothInputClient*(void));
  MOCK_METHOD0(GetBluetoothManagerClient, BluetoothManagerClient*(void));
  MOCK_METHOD0(GetBluetoothNodeClient, BluetoothNodeClient*(void));
  MOCK_METHOD0(GetCashewClient, CashewClient*(void));
  MOCK_METHOD0(GetCrosDisksClient, CrosDisksClient*(void));
  MOCK_METHOD0(GetCryptohomeClient, CryptohomeClient*(void));
  MOCK_METHOD0(GetFlimflamNetworkClient, FlimflamNetworkClient*(void));
  MOCK_METHOD0(GetImageBurnerClient, ImageBurnerClient*(void));
  MOCK_METHOD0(GetIntrospectableClient, IntrospectableClient*(void));
  MOCK_METHOD0(GetPowerManagerClient, PowerManagerClient*(void));
  MOCK_METHOD0(GetSessionManagerClient, SessionManagerClient*(void));
  MOCK_METHOD0(GetSpeechSynthesizerClient, SpeechSynthesizerClient*(void));
  MOCK_METHOD0(GetUpdateEngineClient, UpdateEngineClient*(void));

  MockBluetoothAdapterClient* mock_bluetooth_adapter_client() {
    return mock_bluetooth_adapter_client_.get();
  }
  MockBluetoothDeviceClient* mock_bluetooth_device_client() {
    return mock_bluetooth_device_client_.get();
  }
  MockBluetoothInputClient* mock_bluetooth_input_client() {
    return mock_bluetooth_input_client_.get();
  }
  MockBluetoothManagerClient* mock_bluetooth_manager_client() {
    return mock_bluetooth_manager_client_.get();
  }
  MockBluetoothNodeClient* mock_bluetooth_node_client() {
    return mock_bluetooth_node_client_.get();
  }
  MockCashewClient* mock_cashew_client() {
    return mock_cashew_client_.get();
  }
  MockCrosDisksClient* mock_cros_disks_client() {
    return mock_cros_disks_client_.get();
  }
  MockCryptohomeClient* mock_cryptohome_client() {
    return mock_cryptohome_client_.get();
  }
  MockFlimflamNetworkClient* mock_flimflam_network_client() {
    return mock_flimflam_network_client_.get();
  }
  MockImageBurnerClient* mock_image_burner_client() {
    return mock_image_burner_client_.get();
  }
  MockIntrospectableClient* mock_introspectable_client() {
    return mock_introspectable_client_.get();
  }
  MockPowerManagerClient* mock_power_manager_client() {
    return mock_power_manager_client_.get();
  }
  MockSessionManagerClient* mock_session_manager_client() {
    return mock_session_manager_client_.get();
  }
  MockSpeechSynthesizerClient* mock_speech_synthesizer_client() {
    return mock_speech_synthesizer_client_.get();
  }
  MockUpdateEngineClient* mock_update_engine_client() {
    return mock_update_engine_client_.get();
  }

 private:
  scoped_ptr<MockBluetoothAdapterClient> mock_bluetooth_adapter_client_;
  scoped_ptr<MockBluetoothDeviceClient> mock_bluetooth_device_client_;
  scoped_ptr<MockBluetoothInputClient> mock_bluetooth_input_client_;
  scoped_ptr<MockBluetoothManagerClient> mock_bluetooth_manager_client_;
  scoped_ptr<MockBluetoothNodeClient> mock_bluetooth_node_client_;
  scoped_ptr<MockCashewClient> mock_cashew_client_;
  scoped_ptr<MockCrosDisksClient> mock_cros_disks_client_;
  scoped_ptr<MockCryptohomeClient> mock_cryptohome_client_;
  scoped_ptr<MockFlimflamNetworkClient> mock_flimflam_network_client_;
  scoped_ptr<MockImageBurnerClient> mock_image_burner_client_;
  scoped_ptr<MockIntrospectableClient> mock_introspectable_client_;
  scoped_ptr<MockPowerManagerClient> mock_power_manager_client_;
  scoped_ptr<MockSessionManagerClient> mock_session_manager_client_;
  scoped_ptr<MockSpeechSynthesizerClient> mock_speech_synthesizer_client_;
  scoped_ptr<MockUpdateEngineClient> mock_update_engine_client_;

  DISALLOW_COPY_AND_ASSIGN(MockDBusThreadManager);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_DBUS_MOCK_DBUS_THREAD_MANAGER_H_
