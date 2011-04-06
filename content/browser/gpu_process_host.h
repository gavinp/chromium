// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_GPU_PROCESS_HOST_H_
#define CONTENT_BROWSER_GPU_PROCESS_HOST_H_
#pragma once

#include "base/threading/non_thread_safe.h"
#include "content/browser/browser_child_process_host.h"
#include "content/common/gpu_feature_flags.h"
#include "content/common/gpu_process_launch_causes.h"

namespace IPC {
class Message;
}

class GpuProcessHost : public BrowserChildProcessHost,
                       public base::NonThreadSafe {
 public:

  // Create a GpuProcessHost with the given ID. The object can be found using
  // FromID with the same id.
  static GpuProcessHost* Create(
      int host_id,
      const GpuFeatureFlags& gpu_feature_flags,
      content::CauseForGpuLaunch);

  // Get the GPU process host for the GPU process with the given ID. Returns
  // null if the process no longer exists.
  static GpuProcessHost* FromID(int host_id);

  virtual bool Send(IPC::Message* msg);

  // IPC::Channel::Listener implementation.
  virtual bool OnMessageReceived(const IPC::Message& message);

 private:
  GpuProcessHost(int host_id,
                 const GpuFeatureFlags& gpu_feature_flags,
                 content::CauseForGpuLaunch);
  virtual ~GpuProcessHost();

  bool Init();

  // Post an IPC message to the UI shim's message handler on the UI thread.
  void RouteOnUIThread(const IPC::Message& message);

  virtual bool CanShutdown();
  virtual void OnProcessLaunched();
  virtual void OnChildDied();
  virtual void OnProcessCrashed(int exit_code);

  bool LaunchGpuProcess();

  // The serial number of the GpuProcessHost / GpuProcessHostUIShim pair.
  int host_id_;

  GpuFeatureFlags gpu_feature_flags_;

  DISALLOW_COPY_AND_ASSIGN(GpuProcessHost);
};

#endif  // CONTENT_BROWSER_GPU_PROCESS_HOST_H_
