// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DBUS_MOCK_BLUETOOTH_INPUT_CLIENT_H_
#define CHROME_BROWSER_CHROMEOS_DBUS_MOCK_BLUETOOTH_INPUT_CLIENT_H_

#include <string>

#include "chrome/browser/chromeos/dbus/bluetooth_input_client.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace chromeos {

class MockBluetoothInputClient : public BluetoothInputClient {
 public:
  MockBluetoothInputClient();
  virtual ~MockBluetoothInputClient();

  MOCK_METHOD1(AddObserver, void(Observer*));
  MOCK_METHOD1(RemoveObserver, void(Observer*));
  MOCK_METHOD1(GetProperties, Properties*(const dbus::ObjectPath&));
  MOCK_METHOD2(Connect, void(const dbus::ObjectPath&,
                             const InputCallback&));
  MOCK_METHOD2(Disconnect, void(const dbus::ObjectPath&,
                                const InputCallback&));
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_DBUS_MOCK_BLUETOOTH_INPUT_CLIENT_H_
