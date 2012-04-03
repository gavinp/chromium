// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/dbus/blocking_method_caller.h"

#include "base/bind.h"
#include "dbus/bus.h"
#include "dbus/object_proxy.h"

namespace chromeos {

namespace {

// A utility class to ensure the WaitableEvent is signaled.
class WaitableEventSignaler {
 public:
  explicit WaitableEventSignaler(base::WaitableEvent* event) : event_(event) {}

  ~WaitableEventSignaler() {
    event_->Signal();
  }

 private:
  base::WaitableEvent* event_;
};

// This function is a part of CallMethodAndBlock implementation.
void CallMethodAndBlockInternal(
    dbus::Response** response,
    WaitableEventSignaler* signaler,
    dbus::ObjectProxy* proxy,
    dbus::MethodCall* method_call) {
  *response = proxy->CallMethodAndBlock(
      method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
}

}  // namespace

BlockingMethodCaller::BlockingMethodCaller(dbus::Bus* bus,
                                           dbus::ObjectProxy* proxy)
    : bus_(bus),
      proxy_(proxy),
      on_blocking_method_call_(false /* manual_reset */,
                               false /* initially_signaled */) {
}

BlockingMethodCaller::~BlockingMethodCaller() {
}

dbus::Response* BlockingMethodCaller::CallMethodAndBlock(
    dbus::MethodCall* method_call) {
  WaitableEventSignaler* signaler =
      new WaitableEventSignaler(&on_blocking_method_call_);
  dbus::Response* response = NULL;
  bus_->PostTaskToDBusThread(
      FROM_HERE,
      base::Bind(&CallMethodAndBlockInternal,
                 &response,
                 base::Owned(signaler),
                 base::Unretained(proxy_),
                 method_call));
  on_blocking_method_call_.Wait();
  return response;
}

}  // namespace chromeos
