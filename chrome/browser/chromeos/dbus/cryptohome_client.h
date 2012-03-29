// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DBUS_CRYPTOHOME_CLIENT_H_
#define CHROME_BROWSER_CHROMEOS_DBUS_CRYPTOHOME_CLIENT_H_
#pragma once

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/callback.h"

namespace dbus {
class Bus;
}

namespace chromeos {

// CryptohomeClient is used to communicate with the Cryptohome service.
// All method should be called from the origin thread (UI thread) which
// initializes the DBusThreadManager instance.
class CryptohomeClient {
 public:
  // An enum to describe whether or not a DBus method call succeeded.
  enum CallStatus{
    FAILURE,
    SUCCESS,
  };
  // A callback to handle AsyncCallStatus signals.
  typedef base::Callback<void(int async_id, bool return_status, int return_code)
                         > AsyncCallStatusHandler;
  // A callback to handle responses of AsyncXXX methods.
  typedef base::Callback<void(int async_id)> AsyncMethodCallback;
  // A callback to handle responses of methods returning a bool value.
  typedef base::Callback<void(CallStatus call_status,
                              bool result)> BoolMethodCallback;
  // A callback to handle responses of Pkcs11GetTpmTokenInfo method.
  typedef base::Callback<void(
      CallStatus call_status,
      const std::string& label,
      const std::string& user_pin)> Pkcs11GetTpmTokenInfoCallback;

  virtual ~CryptohomeClient();

  // Factory function, creates a new instance and returns ownership.
  // For normal usage, access the singleton via DBusThreadManager::Get().
  static CryptohomeClient* Create(dbus::Bus* bus);

  // Sets AsyncCallStatus signal handler.
  // |handler| is called when results for AsyncXXX methods are returned.
  // Cryptohome service will process the calls in a first-in-first-out manner
  // when they are made in parallel.
  virtual void SetAsyncCallStatusHandler(AsyncCallStatusHandler handler) = 0;

  // Resets AsyncCallStatus signal handler.
  virtual void ResetAsyncCallStatusHandler() = 0;

  // Calls IsMounted method and returns true when the call succeeds.
  // This method blocks until the call returns.
  virtual bool IsMounted(bool* is_mounted) = 0;

  // Calls Unmount method and returns true when the call succeeds.
  // This method blocks until the call returns.
  virtual bool Unmount(bool* success) = 0;

  // Calls AsyncCheckKey method.  |callback| is called after the method call
  // succeeds.
  virtual void AsyncCheckKey(const std::string& username,
                             const std::string& key,
                             AsyncMethodCallback callback) = 0;

  // Calls AsyncMigrateKey method.  |callback| is called after the method call
  // succeeds.
  virtual void AsyncMigrateKey(const std::string& username,
                               const std::string& from_key,
                               const std::string& to_key,
                               AsyncMethodCallback callback) = 0;

  // Calls AsyncRemove method.  |callback| is called after the method call
  // succeeds.
  virtual void AsyncRemove(const std::string& username,
                           AsyncMethodCallback callback) = 0;

  // Calls GetSystemSalt method.  This method blocks until the call returns.
  // The original content of |salt| is lost.
  virtual bool GetSystemSalt(std::vector<uint8>* salt) = 0;

  // Calls AsyncMount method.  |callback| is called after the method call
  // succeeds.
  virtual void AsyncMount(const std::string& username,
                          const std::string& key,
                          const bool create_if_missing,
                          AsyncMethodCallback callback) = 0;

  // Calls AsyncMountGuest method.  |callback| is called after the method call
  // succeeds.
  virtual void AsyncMountGuest(AsyncMethodCallback callback) = 0;

  // Calls TpmIsReady method and returns true when the call succeeds.
  // This method blocks until the call returns.
  virtual bool TpmIsReady(bool* ready) = 0;

  // Calls TpmIsEnabled method.
  virtual void TpmIsEnabled(BoolMethodCallback callback) = 0;

  // Calls TpmIsEnabled method and returns true when the call succeeds.
  // This method blocks until the call returns.
  // TODO(hashimoto): Remove this method. crosbug.com/28500
  virtual bool CallTpmIsEnabledAndBlock(bool* enabled) = 0;

  // Calls TpmGetPassword method and returns true when the call succeeds.
  // This method blocks until the call returns.
  // The original content of |password| is lost.
  virtual bool TpmGetPassword(std::string* password) = 0;

  // Calls TpmIsOwned method and returns true when the call succeeds.
  // This method blocks until the call returns.
  virtual bool TpmIsOwned(bool* owned) = 0;

  // Calls TpmIsBeingOwned method and returns true when the call succeeds.
  // This method blocks until the call returns.
  virtual bool TpmIsBeingOwned(bool* owning) = 0;

  // Calls TpmCanAttemptOwnership method and returns true when the call
  // succeeds.  This method blocks until the call returns.
  virtual bool TpmCanAttemptOwnership() = 0;

  // Calls TpmClearStoredPassword method and returns true when the call
  // succeeds.  This method blocks until the call returns.
  virtual bool TpmClearStoredPassword() = 0;

  // Calls Pkcs11IsTpmTokenReady method.
  virtual void Pkcs11IsTpmTokenReady(BoolMethodCallback callback) = 0;

  // Calls Pkcs11GetTpmTokenInfo method.
  virtual void Pkcs11GetTpmTokenInfo(
      Pkcs11GetTpmTokenInfoCallback callback) = 0;

  // Calls InstallAttributesGet method and returns true when the call succeeds.
  // This method blocks until the call returns.
  // The original content of |value| is lost.
  virtual bool InstallAttributesGet(const std::string& name,
                                    std::vector<uint8>* value,
                                    bool* successful) = 0;

  // Calls InstallAttributesSet method and returns true when the call succeeds.
  // This method blocks until the call returns.
  virtual bool InstallAttributesSet(const std::string& name,
                                    const std::vector<uint8>& value,
                                    bool* successful) = 0;

  // Calls InstallAttributesFinalize method and returns true when the call
  // succeeds.  This method blocks until the call returns.
  virtual bool InstallAttributesFinalize(bool* successful) = 0;

  // Calls InstallAttributesIsReady method and returns true when the call
  // succeeds.  This method blocks until the call returns.
  virtual bool InstallAttributesIsReady(bool* is_ready) = 0;

  // Calls InstallAttributesIsInvalid method and returns true when the call
  // succeeds.  This method blocks until the call returns.
  virtual bool InstallAttributesIsInvalid(bool* is_invalid) = 0;

  // Calls InstallAttributesIsFirstInstall method and returns true when the call
  // succeeds. This method blocks until the call returns.
  virtual bool InstallAttributesIsFirstInstall(bool* is_first_install) = 0;

 protected:
  // Create() should be used instead.
  CryptohomeClient();

 private:
  DISALLOW_COPY_AND_ASSIGN(CryptohomeClient);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_DBUS_CRYPTOHOME_CLIENT_H_
