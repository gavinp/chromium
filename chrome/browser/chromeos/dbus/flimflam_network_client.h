// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DBUS_FLIMFLAM_NETWORK_CLIENT_H_
#define CHROME_BROWSER_CHROMEOS_DBUS_FLIMFLAM_NETWORK_CLIENT_H_

#include <string>

#include "base/basictypes.h"
#include "base/callback.h"
#include "chrome/browser/chromeos/dbus/dbus_client_implementation_type.h"
#include "chrome/browser/chromeos/dbus/flimflam_client_helper.h"

namespace base {

class Value;
class DictionaryValue;

}  // namespace base

namespace dbus {

class Bus;

}  // namespace dbus

namespace chromeos {

// FlimflamNetworkClient is used to communicate with the Flimflam Network
// service.  All methods should be called from the origin thread which
// initializes the DBusThreadManager instance.
class FlimflamNetworkClient {
 public:
  typedef FlimflamClientHelper::PropertyChangedHandler PropertyChangedHandler;
  typedef FlimflamClientHelper::DictionaryValueCallback DictionaryValueCallback;
  typedef FlimflamClientHelper::CallStatus CallStatus;
  static const CallStatus FAILURE = FlimflamClientHelper::FAILURE;
  static const CallStatus SUCCESS = FlimflamClientHelper::SUCCESS;

  virtual ~FlimflamNetworkClient();

  // Factory function, creates a new instance which is owned by the caller.
  // For normal usage, access the singleton via DBusThreadManager::Get().
  static FlimflamNetworkClient* Create(DBusClientImplementationType type,
                                       dbus::Bus* bus);

  // Sets PropertyChanged signal handler.
  virtual void SetPropertyChangedHandler(
      const PropertyChangedHandler& handler) = 0;

  // Resets PropertyChanged signal handler.
  virtual void ResetPropertyChangedHandler() = 0;

  // Calls GetProperties method.
  // |callback| is called after the method call succeeds.
  virtual void GetProperties(const DictionaryValueCallback& callback) = 0;

 protected:
  // Create() should be used instead.
  FlimflamNetworkClient();

 private:
  DISALLOW_COPY_AND_ASSIGN(FlimflamNetworkClient);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_DBUS_FLIMFLAM_NETWORK_CLIENT_H_
