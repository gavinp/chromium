// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_IPC_COMMAND_BUFFER_PROXY_H_
#define GPU_IPC_COMMAND_BUFFER_PROXY_H_
#pragma once

#include <string>

#include "base/callback.h"
#include "gpu/command_buffer/common/command_buffer.h"
#include "gpu/command_buffer/common/command_buffer_shared.h"

struct GpuMemoryAllocationForRenderer;

// Client side proxy that forwards messages synchronously to a
// CommandBufferStub.
class GPU_EXPORT CommandBufferProxy : public gpu::CommandBuffer {
 public:
  typedef base::Callback<void(
      const std::string& msg, int id)> GpuConsoleMessageCallback;

  CommandBufferProxy() { }

  virtual ~CommandBufferProxy() { }

  virtual int GetRouteID() const = 0;

  // Invoke the task when the channel has been flushed. Takes care of deleting
  // the task whether the echo succeeds or not.
  virtual bool Echo(const base::Closure& callback) = 0;

  // Sends an IPC message with the new state of surface visibility.
  virtual bool SetSurfaceVisible(bool visible) = 0;

  virtual bool DiscardBackbuffer() = 0;
  virtual bool EnsureBackbuffer() = 0;

  // Register a callback to invoke whenever we recieve a new memory allocation.
  virtual void SetMemoryAllocationChangedCallback(
      const base::Callback<void(const GpuMemoryAllocationForRenderer&)>&
          callback) = 0;

  // Reparent a command buffer. TODO(apatrick): going forward, the notion of
  // the parent / child relationship between command buffers is going away in
  // favor of the notion of surfaces that can be drawn to in one command buffer
  // and bound as a texture in any other.
  virtual bool SetParent(CommandBufferProxy* parent_command_buffer,
                         uint32 parent_texture_id) = 0;

  virtual void SetChannelErrorCallback(const base::Closure& callback) = 0;

  // Set a task that will be invoked the next time the window becomes invalid
  // and needs to be repainted. Takes ownership of task.
  virtual void SetNotifyRepaintTask(const base::Closure& callback) = 0;

  virtual void SetOnConsoleMessageCallback(
      const GpuConsoleMessageCallback& callback) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(CommandBufferProxy);
};

#endif  // GPU_IPC_COMMAND_BUFFER_PROXY_H_
