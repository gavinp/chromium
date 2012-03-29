// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/dbus/mock_dbus_thread_manager.h"

#include "chrome/browser/chromeos/dbus/mock_bluetooth_adapter_client.h"
#include "chrome/browser/chromeos/dbus/mock_bluetooth_device_client.h"
#include "chrome/browser/chromeos/dbus/mock_bluetooth_input_client.h"
#include "chrome/browser/chromeos/dbus/mock_bluetooth_manager_client.h"
#include "chrome/browser/chromeos/dbus/mock_bluetooth_node_client.h"
#include "chrome/browser/chromeos/dbus/mock_cashew_client.h"
#include "chrome/browser/chromeos/dbus/mock_cros_disks_client.h"
#include "chrome/browser/chromeos/dbus/mock_cryptohome_client.h"
#include "chrome/browser/chromeos/dbus/mock_flimflam_network_client.h"
#include "chrome/browser/chromeos/dbus/mock_image_burner_client.h"
#include "chrome/browser/chromeos/dbus/mock_introspectable_client.h"
#include "chrome/browser/chromeos/dbus/mock_power_manager_client.h"
#include "chrome/browser/chromeos/dbus/mock_session_manager_client.h"
#include "chrome/browser/chromeos/dbus/mock_speech_synthesizer_client.h"
#include "chrome/browser/chromeos/dbus/mock_update_engine_client.h"

using ::testing::AnyNumber;
using ::testing::Return;
using ::testing::_;

namespace chromeos {

MockDBusThreadManager::MockDBusThreadManager()
    : mock_bluetooth_adapter_client_(new MockBluetoothAdapterClient),
      mock_bluetooth_device_client_(new MockBluetoothDeviceClient),
      mock_bluetooth_input_client_(new MockBluetoothInputClient),
      mock_bluetooth_manager_client_(new MockBluetoothManagerClient),
      mock_bluetooth_node_client_(new MockBluetoothNodeClient),
      mock_cashew_client_(new MockCashewClient),
      mock_cros_disks_client_(new MockCrosDisksClient),
      mock_cryptohome_client_(new MockCryptohomeClient),
      mock_flimflam_network_client_(new MockFlimflamNetworkClient),
      mock_image_burner_client_(new MockImageBurnerClient),
      mock_introspectable_client_(new MockIntrospectableClient),
      mock_power_manager_client_(new MockPowerManagerClient),
      mock_session_manager_client_(new MockSessionManagerClient),
      mock_speech_synthesizer_client_(new MockSpeechSynthesizerClient),
      mock_update_engine_client_(new MockUpdateEngineClient) {
  EXPECT_CALL(*this, GetBluetoothAdapterClient())
      .WillRepeatedly(Return(mock_bluetooth_adapter_client_.get()));
  EXPECT_CALL(*this, GetBluetoothDeviceClient())
      .WillRepeatedly(Return(mock_bluetooth_device_client_.get()));
  EXPECT_CALL(*this, GetBluetoothInputClient())
      .WillRepeatedly(Return(mock_bluetooth_input_client_.get()));
  EXPECT_CALL(*this, GetBluetoothManagerClient())
      .WillRepeatedly(Return(mock_bluetooth_manager_client()));
  EXPECT_CALL(*this, GetBluetoothNodeClient())
      .WillRepeatedly(Return(mock_bluetooth_node_client_.get()));
  EXPECT_CALL(*this, GetCashewClient())
      .WillRepeatedly(Return(mock_cashew_client()));
  EXPECT_CALL(*this, GetCrosDisksClient())
      .WillRepeatedly(Return(mock_cros_disks_client()));
  EXPECT_CALL(*this, GetCryptohomeClient())
      .WillRepeatedly(Return(mock_cryptohome_client()));
  EXPECT_CALL(*this, GetFlimflamNetworkClient())
      .WillRepeatedly(Return(mock_flimflam_network_client()));
  EXPECT_CALL(*this, GetImageBurnerClient())
      .WillRepeatedly(Return(mock_image_burner_client()));
  EXPECT_CALL(*this, GetIntrospectableClient())
      .WillRepeatedly(Return(mock_introspectable_client()));
  EXPECT_CALL(*this, GetPowerManagerClient())
      .WillRepeatedly(Return(mock_power_manager_client_.get()));
  EXPECT_CALL(*this, GetSessionManagerClient())
      .WillRepeatedly(Return(mock_session_manager_client_.get()));
  EXPECT_CALL(*this, GetSpeechSynthesizerClient())
      .WillRepeatedly(Return(mock_speech_synthesizer_client_.get()));
  EXPECT_CALL(*this, GetUpdateEngineClient())
      .WillRepeatedly(Return(mock_update_engine_client_.get()));

  // These observers calls are used in ChromeBrowserMainPartsChromeos.
  EXPECT_CALL(*mock_power_manager_client_.get(), AddObserver(_))
      .Times(AnyNumber());
  EXPECT_CALL(*mock_power_manager_client_.get(), RemoveObserver(_))
      .Times(AnyNumber());
  EXPECT_CALL(*mock_session_manager_client_.get(), AddObserver(_))
      .Times(AnyNumber());
  EXPECT_CALL(*mock_session_manager_client_.get(), RemoveObserver(_))
      .Times(AnyNumber());
  EXPECT_CALL(*mock_update_engine_client_.get(), AddObserver(_))
      .Times(AnyNumber());
  EXPECT_CALL(*mock_update_engine_client_.get(), RemoveObserver(_))
      .Times(AnyNumber());

  // Called from PowerMenuButton ctor.
  EXPECT_CALL(*mock_power_manager_client_.get(), RequestStatusUpdate(_))
      .Times(AnyNumber());

  // Called from DiskMountManager::Initialize(), ChromeBrowserMainPartsChromeos.
  EXPECT_CALL(*mock_cros_disks_client_.get(), SetUpConnections(_, _))
      .Times(AnyNumber());
}

MockDBusThreadManager::~MockDBusThreadManager() {}

}  // namespace chromeos
