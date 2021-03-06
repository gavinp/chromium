// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DBUS_BLUETOOTH_AGENT_SERVICE_PROVIDER_H_
#define CHROME_BROWSER_CHROMEOS_DBUS_BLUETOOTH_AGENT_SERVICE_PROVIDER_H_
#pragma once

#include <string>

#include "base/basictypes.h"
#include "base/callback.h"
#include "dbus/bus.h"
#include "dbus/object_path.h"

namespace chromeos {

// BluetoothAgentServiceProvider is used to provide a D-Bus object that BlueZ
// can communicate with during a remote device pairing request.
//
// Instantiate with a chosen D-Bus object path and delegate object, and pass
// the D-Bus object path as the |agent_path| argument to the
// chromeos::BluetoothAdapterClient::CreatePairedDevice() method. Calls made
// to the agent by the Bluetooth daemon will be passed on to your Delegate
// object for handling, and responses returned using the callbacks supplied
// to those methods.
class BluetoothAgentServiceProvider {
 public:
  // Interface for reacting to agent requests.
  class Delegate {
   public:
    virtual ~Delegate() {}

    // Possible status values that may be returned to callbacks. Success
    // indicates that a pincode or passkey has been obtained, or permission
    // granted; rejected indicates the user rejected the request or denied
    // permission; cancelled indicates the user cancelled the request
    // without confirming either way.
    enum Status {
      SUCCESS,
      REJECTED,
      CANCELLED
    };

    // Possible values for the |mode| parameter of the ConfirmModeChange()
    // method. Off indicates that the adapter is to be turned off, connectable
    // indicates that the adapter is to be turned on and accept incoming
    // connections, and discoverable indicates the adapter is to be turned
    // on and discoverable by remote devices.
    enum Mode {
      OFF,
      CONNECTABLE,
      DISCOVERABLE
    };

    // The PinCodeCallback is used for the RequestPinCode() method, it should
    // be called with two arguments, the |status| of the request (success,
    // rejected or cancelled) and the |pincode| requested.
    typedef base::Callback<void(Status, const std::string&)> PinCodeCallback;

    // The PasskeyCallback is used for the RequestPasskey() method, it should
    // be called with two arguments, the |status| of the request (success,
    // rejected or cancelled) and the |passkey| requested, a numeric in the
    // range 0-999999,
    typedef base::Callback<void(Status, uint32)> PasskeyCallback;

    // The ConfirmationCallback is used for methods which request confirmation
    // or authorization, it should be called with one argument, the |status|
    // of the request (success, rejected or cancelled).
    typedef base::Callback<void(Status)> ConfirmationCallback;

    // This method will be called when the agent is unregistered from the
    // Bluetooth daemon, generally at the end of a pairing request. It may be
    // used to perform cleanup tasks.
    virtual void Release() = 0;

    // This method will be called when the Bluetooth daemon requires a
    // PIN Code for authentication of the device with object path |device_path|,
    // the agent should obtain the code from the user and call |callback|
    // to provide it, or indicate rejection or cancellation of the request.
    //
    // PIN Codes are generally required for Bluetooth 2.0 and earlier devices
    // for which there is no automatic pairing or special handling.
    virtual void RequestPinCode(const dbus::ObjectPath& device_path,
                                const PinCodeCallback& callback) = 0;

    // This method will be called when the Bluetooth daemon requires a
    // Passkey for authentication of the device with object path |device_path|,
    // the agent should obtain the passkey from the user (a numeric in the
    // range 0-999999) and call |callback| to provide it, or indicate
    // rejection or cancellation of the request.
    //
    // Passkeys are generally required for Bluetooth 2.1 and later devices
    // which cannot provide input or display on their own, and don't accept
    // passkey-less pairing.
    virtual void RequestPasskey(const dbus::ObjectPath& device_path,
                                const PasskeyCallback& callback) = 0;

    // This method will be called when the Bluetooth daemon requires that the
    // user enter the PIN code |pincode| into the device with object path
    // |device_path| so that it may be authenticated. The Cancel() method
    // will be called to dismiss the display once pairing is complete or
    // cancelled.
    //
    // This is used for Bluetooth 2.0 and earlier keyboard devices, the
    // |pincode| will always be a six-digit numeric in the range 000000-999999
    // for compatibilty with later specifications.
    virtual void DisplayPinCode(const dbus::ObjectPath& device_path,
                                const std::string& pincode) = 0;

    // This method will be called when the Bluetooth daemon requires that the
    // user enter the Passkey |passkey| into the device with object path
    // |device_path| so that it may be authenticated. The Cancel() method
    // will be called to dismiss the display once pairing is complete or
    // cancelled.
    //
    // This is used for Bluetooth 2.1 and later devices that support input
    // but not display, such as keyboards. The Passkey is a numeric in the
    // range 0-999999 and should be always presented zero-padded to six
    // digits.
    virtual void DisplayPasskey(const dbus::ObjectPath& device_path,
                                uint32 passkey) = 0;

    // This method will be called when the Bluetooth daemon requires that the
    // user confirm that the Passkey |passkey| is displayed on the screen
    // of the device with object path |object_path| so that it may be
    // authenticated. The agent should display to the user and ask for
    // confirmation, then call |callback| to provide their response (success,
    // rejected or cancelled).
    //
    // This is used for Bluetooth 2.1 and later devices that support display,
    // such as other computers or phones. The Passkey is a numeric in the
    // range 0-999999 and should be always present zero-padded to six
    // digits.
    virtual void RequestConfirmation(const dbus::ObjectPath& device_path,
                                     uint32 passkey,
                                     const ConfirmationCallback& callback) = 0;

    // This method will be called when the Bluetooth daemon requires that the
    // user confirm that the device with object path |object_path| is
    // authorized to connect to the service with UUID |uuid|. The agent should
    // confirm with the user and call |callback| to provide their response
    // (success, rejected or cancelled).
    virtual void Authorize(const dbus::ObjectPath& device_path,
                           const std::string& uuid,
                           const ConfirmationCallback& callback) = 0;

    // This method will be called when the Bluetooth daemon requires that the
    // user confirm that the device adapter may switch to mode |mode|. The
    // agent should confirm with the user and call |callback| to provide
    // their response (success, rejected or cancelled).
    virtual void ConfirmModeChange(Mode mode,
                                   const ConfirmationCallback& callback) = 0;

    // This method will be called by the Bluetooth daemon to indicate that
    // the request failed before a reply was returned from the device.
    virtual void Cancel() = 0;
  };

  virtual ~BluetoothAgentServiceProvider();

  // Creates the instance where |bus| is the D-Bus bus connection to export
  // the object onto, |object_path| is the object path that it should have
  // and |delegate| is the object to which all method calls will be passed
  // and responses generated from.
  static BluetoothAgentServiceProvider* Create(
      dbus::Bus* bus, const dbus::ObjectPath& object_path, Delegate* delegate);

 protected:
  BluetoothAgentServiceProvider();

 private:
  DISALLOW_COPY_AND_ASSIGN(BluetoothAgentServiceProvider);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_DBUS_BLUETOOTH_AGENT_SERVICE_PROVIDER_H_
