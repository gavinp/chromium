// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_PEPPER_PEPPER_PLATFORM_AUDIO_INPUT_IMPL_H_
#define CONTENT_RENDERER_PEPPER_PEPPER_PLATFORM_AUDIO_INPUT_IMPL_H_

#include <string>

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "content/renderer/media/audio_input_message_filter.h"
#include "media/audio/audio_parameters.h"
#include "webkit/plugins/ppapi/plugin_delegate.h"

class AudioParameters;
class PepperPluginDelegateImpl;

// PepperPlatformAudioInputImpl is operated on two threads: the main thread (the
// thread on which objects are created) and the I/O thread. All public methods,
// except the destructor, must be called on the main thread. The notifications
// to the users of this class (via the PlatformAudioInputClient interface) are
// also sent on the main thread. Internally, this class sends audio input IPC
// messages and receives AudioInputMessageFilter::Delegate notifications on the
// I/O thread.
class PepperPlatformAudioInputImpl
    : public webkit::ppapi::PluginDelegate::PlatformAudioInput,
      public AudioInputMessageFilter::Delegate,
      public base::RefCountedThreadSafe<PepperPlatformAudioInputImpl> {
 public:
  virtual ~PepperPlatformAudioInputImpl();

  // Factory function, returns NULL on failure. StreamCreated() will be called
  // when the stream is created.
  static PepperPlatformAudioInputImpl* Create(
      const base::WeakPtr<PepperPluginDelegateImpl>& plugin_delegate,
      const std::string& device_id,
      int sample_rate,
      int frames_per_buffer,
      webkit::ppapi::PluginDelegate::PlatformAudioInputClient* client);

  // PlatformAudioInput implementation (called on main thread).
  virtual void StartCapture() OVERRIDE;
  virtual void StopCapture() OVERRIDE;
  virtual void ShutDown() OVERRIDE;

 private:
  PepperPlatformAudioInputImpl();

  bool Initialize(
      const base::WeakPtr<PepperPluginDelegateImpl>& plugin_delegate,
      const std::string& device_id,
      int sample_rate,
      int frames_per_buffer,
      webkit::ppapi::PluginDelegate::PlatformAudioInputClient* client);

  // I/O thread backends to above functions.
  void InitializeOnIOThread(int session_id);
  void StartCaptureOnIOThread();
  void StopCaptureOnIOThread();
  void ShutDownOnIOThread();

  // AudioInputMessageFilter::Delegate.
  virtual void OnStreamCreated(base::SharedMemoryHandle handle,
                               base::SyncSocket::Handle socket_handle,
                               uint32 length) OVERRIDE;
  virtual void OnVolume(double volume) OVERRIDE;
  virtual void OnStateChanged(AudioStreamState state) OVERRIDE;
  virtual void OnDeviceReady(const std::string&) OVERRIDE;

  void OnDeviceOpened(int request_id,
                      bool succeeded,
                      const std::string& label);
  void CloseDevice();
  void NotifyStreamCreationFailed();

  // The client to notify when the stream is created. THIS MUST ONLY BE
  // ACCESSED ON THE MAIN THREAD.
  webkit::ppapi::PluginDelegate::PlatformAudioInputClient* client_;

  // MessageFilter used to send/receive IPC. THIS MUST ONLY BE ACCESSED ON THE
  // I/O thread except to send messages and get the message loop.
  scoped_refptr<AudioInputMessageFilter> filter_;

  // Our ID on the MessageFilter. THIS MUST ONLY BE ACCESSED ON THE I/O THREAD
  // or else you could race with the initialize function which sets it.
  int32 stream_id_;

  base::MessageLoopProxy* main_message_loop_proxy_;

  // THIS MUST ONLY BE ACCESSED ON THE MAIN THREAD.
  base::WeakPtr<PepperPluginDelegateImpl> plugin_delegate_;

  // The unique ID to identify the opened device. THIS MUST ONLY BE ACCESSED ON
  // THE MAIN THREAD.
  std::string label_;

  // Whether ShutDownOnIOThread() has been called. THIS MUST ONLY BE ACCESSED ON
  // THE I/O THREAD.
  bool shutdown_called_;

  // Initialized on the main thread and accessed on the I/O thread afterwards.
  AudioParameters params_;

  DISALLOW_COPY_AND_ASSIGN(PepperPlatformAudioInputImpl);
};

#endif  // CONTENT_RENDERER_PEPPER_PEPPER_PLATFORM_AUDIO_INPUT_IMPL_H_
