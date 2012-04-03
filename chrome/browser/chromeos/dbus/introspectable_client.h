// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DBUS_INTROSPECTABLE_CLIENT_H_
#define CHROME_BROWSER_CHROMEOS_DBUS_INTROSPECTABLE_CLIENT_H_
#pragma once

#include <string>
#include <vector>

#include "base/callback.h"
#include "chrome/browser/chromeos/dbus/dbus_client_implementation_type.h"
#include "dbus/object_path.h"

namespace dbus {
class Bus;
}  // namespace dbus

namespace chromeos {

// IntrospectableClient is used to retrieve the D-Bus introspection data
// from a remote object.
class IntrospectableClient {
 public:
  virtual ~IntrospectableClient();

  // The IntrospectCallback is used for the Introspect() method. It receives
  // four arguments, the first two are the |service_name| and |object_path|
  // of the remote object being introspected, the third is the |xml_data| of
  // the object as described in
  // http://dbus.freedesktop.org/doc/dbus-specification.html, the fourth
  // |success| indicates whether the request succeeded.
  typedef base::Callback<void(const std::string&, const dbus::ObjectPath&,
                              const std::string&, bool)> IntrospectCallback;

  // Retrieves introspection data from the remote object on service name
  // |service_name| with object path |object_path|, calling |callback| with
  // the XML-formatted data received.
  virtual void Introspect(const std::string& service_name,
                          const dbus::ObjectPath& object_path,
                          const IntrospectCallback& callback) = 0;

  // Creates the instance
  static IntrospectableClient* Create(DBusClientImplementationType type,
                                      dbus::Bus* bus);

 protected:
  IntrospectableClient();

 private:
  DISALLOW_COPY_AND_ASSIGN(IntrospectableClient);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_DBUS_INTROSPECTABLE_CLIENT_H_
