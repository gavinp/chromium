// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_NACL_NACL_LISTENER_H_
#define CHROME_NACL_NACL_LISTENER_H_
#pragma once

#include <vector>

#include "base/memory/scoped_ptr.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/thread.h"
#include "chrome/common/nacl_types.h"
#include "ipc/ipc_channel.h"

namespace IPC {
class SyncChannel;
class SyncMessageFilter;
}

// The NaClListener is an IPC channel listener that waits for a
// request to start a NaCl module.
class NaClListener : public IPC::Channel::Listener {
 public:
  NaClListener();
  virtual ~NaClListener();
  // Listen for a request to launch a NaCl module.
  void Listen();
  void set_debug_enabled(bool value) {debug_enabled_ = value;}

  bool Send(IPC::Message* msg);

 private:
  void OnStartSelLdr(std::vector<nacl::FileDescriptor> handles,
                     const std::string& validation_cache_key,
                     const std::string& version,
                     bool enable_exception_handling);
  virtual bool OnMessageReceived(const IPC::Message& msg) OVERRIDE;

  // A channel back to the browser.
  scoped_ptr<IPC::SyncChannel> channel_;

  // A filter that allows other threads to use the channel.
  scoped_ptr<IPC::SyncMessageFilter> filter_;

  base::WaitableEvent shutdown_event_;
  base::Thread io_thread_;

  // Used to identify what thread we're on.
  MessageLoop* main_loop_;

  bool debug_enabled_;

  DISALLOW_COPY_AND_ASSIGN(NaClListener);
};

#endif  // CHROME_NACL_NACL_LISTENER_H_
