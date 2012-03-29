// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/media/audio_device.h"

#include "base/debug/trace_event.h"
#include "base/message_loop.h"
#include "base/threading/thread_restrictions.h"
#include "base/time.h"
#include "content/common/child_process.h"
#include "content/common/media/audio_messages.h"
#include "content/common/view_messages.h"
#include "content/renderer/render_thread_impl.h"
#include "media/audio/audio_output_controller.h"
#include "media/audio/audio_util.h"

using media::AudioRendererSink;

// Takes care of invoking the render callback on the audio thread.
// An instance of this class is created for each capture stream in
// OnStreamCreated().
class AudioDevice::AudioThreadCallback
    : public AudioDeviceThread::Callback {
 public:
  AudioThreadCallback(const AudioParameters& audio_parameters,
                      base::SharedMemoryHandle memory,
                      int memory_length,
                      AudioRendererSink::RenderCallback* render_callback);
  virtual ~AudioThreadCallback();

  virtual void MapSharedMemory() OVERRIDE;

  // Called whenever we receive notifications about pending data.
  virtual void Process(int pending_data) OVERRIDE;

 private:
  AudioRendererSink::RenderCallback* render_callback_;
  DISALLOW_COPY_AND_ASSIGN(AudioThreadCallback);
};

AudioDevice::AudioDevice()
    : ScopedLoopObserver(ChildProcess::current()->io_message_loop()),
      callback_(NULL),
      volume_(1.0),
      stream_id_(0),
      play_on_start_(true),
      is_started_(false) {
  filter_ = RenderThreadImpl::current()->audio_message_filter();
}

AudioDevice::AudioDevice(const AudioParameters& params,
                         RenderCallback* callback)
    : ScopedLoopObserver(ChildProcess::current()->io_message_loop()),
      audio_parameters_(params),
      callback_(callback),
      volume_(1.0),
      stream_id_(0),
      play_on_start_(true),
      is_started_(false) {
  filter_ = RenderThreadImpl::current()->audio_message_filter();
}

void AudioDevice::Initialize(const AudioParameters& params,
                             RenderCallback* callback) {
  CHECK_EQ(0, stream_id_) <<
      "AudioDevice::Initialize() must be called before Start()";

  CHECK(!callback_);  // Calling Initialize() twice?

  audio_parameters_ = params;
  // TODO(xians): We have to hard code the sample format to 16 since the
  // current audio path does not support sample formats rather than 16bits per
  // channel. Remove it if the problem is fixed.
  audio_parameters_.Reset(
      params.format(),
      params.channel_layout(), params.sample_rate(), 16,
      params.frames_per_buffer());
  callback_ = callback;
}

AudioDevice::~AudioDevice() {
  // The current design requires that the user calls Stop() before deleting
  // this class.
  CHECK_EQ(0, stream_id_);
}

void AudioDevice::Start() {
  DCHECK(callback_) << "Initialize hasn't been called";
  message_loop()->PostTask(FROM_HERE,
      base::Bind(&AudioDevice::InitializeOnIOThread, this, audio_parameters_));
}

void AudioDevice::Stop() {
  {
    base::AutoLock auto_lock(audio_thread_lock_);
    audio_thread_.Stop(MessageLoop::current());
  }

  message_loop()->PostTask(FROM_HERE,
      base::Bind(&AudioDevice::ShutDownOnIOThread, this));
}

void AudioDevice::Play() {
  message_loop()->PostTask(FROM_HERE,
      base::Bind(&AudioDevice::PlayOnIOThread, this));
}

void AudioDevice::Pause(bool flush) {
  message_loop()->PostTask(FROM_HERE,
      base::Bind(&AudioDevice::PauseOnIOThread, this, flush));
}

bool AudioDevice::SetVolume(double volume) {
  if (volume < 0 || volume > 1.0)
    return false;

  if (!message_loop()->PostTask(FROM_HERE,
          base::Bind(&AudioDevice::SetVolumeOnIOThread, this, volume))) {
    return false;
  }

  volume_ = volume;

  return true;
}

void AudioDevice::GetVolume(double* volume) {
  // Return a locally cached version of the current scaling factor.
  *volume = volume_;
}

void AudioDevice::InitializeOnIOThread(const AudioParameters& params) {
  DCHECK(message_loop()->BelongsToCurrentThread());
  // Make sure we don't create the stream more than once.
  DCHECK_EQ(0, stream_id_);
  if (stream_id_)
    return;

  stream_id_ = filter_->AddDelegate(this);
  Send(new AudioHostMsg_CreateStream(stream_id_, params));
}

void AudioDevice::PlayOnIOThread() {
  DCHECK(message_loop()->BelongsToCurrentThread());
  if (stream_id_ && is_started_)
    Send(new AudioHostMsg_PlayStream(stream_id_));
  else
    play_on_start_ = true;
}

void AudioDevice::PauseOnIOThread(bool flush) {
  DCHECK(message_loop()->BelongsToCurrentThread());
  if (stream_id_ && is_started_) {
    Send(new AudioHostMsg_PauseStream(stream_id_));
    if (flush)
      Send(new AudioHostMsg_FlushStream(stream_id_));
  } else {
    // Note that |flush| isn't relevant here since this is the case where
    // the stream is first starting.
    play_on_start_ = false;
  }
}

void AudioDevice::ShutDownOnIOThread() {
  DCHECK(message_loop()->BelongsToCurrentThread());

  // Make sure we don't call shutdown more than once.
  if (stream_id_) {
    is_started_ = false;

    filter_->RemoveDelegate(stream_id_);
    Send(new AudioHostMsg_CloseStream(stream_id_));
    stream_id_ = 0;
  }

  // We can run into an issue where ShutDownOnIOThread is called right after
  // OnStreamCreated is called in cases where Start/Stop are called before we
  // get the OnStreamCreated callback.  To handle that corner case, we call
  // Stop(). In most cases, the thread will already be stopped.
  // Another situation is when the IO thread goes away before Stop() is called
  // in which case, we cannot use the message loop to close the thread handle
  // and can't not rely on the main thread existing either.
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  audio_thread_.Stop(NULL);
  audio_callback_.reset();
}

void AudioDevice::SetVolumeOnIOThread(double volume) {
  DCHECK(message_loop()->BelongsToCurrentThread());
  if (stream_id_)
    Send(new AudioHostMsg_SetVolume(stream_id_, volume));
}

void AudioDevice::OnStateChanged(AudioStreamState state) {
  DCHECK(message_loop()->BelongsToCurrentThread());

  // Do nothing if the stream has been closed.
  if (!stream_id_)
    return;

  if (state == kAudioStreamError) {
    DLOG(WARNING) << "AudioDevice::OnStateChanged(kError)";
    // Don't dereference the callback object if the audio thread
    // is stopped or stopping.  That could mean that the callback
    // object has been deleted.
    // TODO(tommi): Add an explicit contract for clearing the callback
    // object.  Possibly require calling Initialize again or provide
    // a callback object via Start() and clear it in Stop().
    if (!audio_thread_.IsStopped())
      callback_->OnRenderError();
  }
}

void AudioDevice::OnStreamCreated(
    base::SharedMemoryHandle handle,
    base::SyncSocket::Handle socket_handle,
    uint32 length) {
  DCHECK(message_loop()->BelongsToCurrentThread());
  DCHECK_GE(length,
      audio_parameters_.frames_per_buffer() * sizeof(int16) *
      audio_parameters_.channels());
#if defined(OS_WIN)
  DCHECK(handle);
  DCHECK(socket_handle);
#else
  DCHECK_GE(handle.fd, 0);
  DCHECK_GE(socket_handle, 0);
#endif

  base::AutoLock auto_lock(audio_thread_lock_);

  // Takes care of the case when Stop() is called before OnStreamCreated().
  if (!stream_id_) {
    base::SharedMemory::CloseHandle(handle);
    // Close the socket handler.
    base::SyncSocket socket(socket_handle);
    return;
  }

  DCHECK(audio_thread_.IsStopped());
  audio_callback_.reset(new AudioDevice::AudioThreadCallback(audio_parameters_,
      handle, length, callback_));
  audio_thread_.Start(audio_callback_.get(), socket_handle, "AudioDevice");

  // We handle the case where Play() and/or Pause() may have been called
  // multiple times before OnStreamCreated() gets called.
  is_started_ = true;
  if (play_on_start_)
    PlayOnIOThread();
}

void AudioDevice::Send(IPC::Message* message) {
  filter_->Send(message);
}

void AudioDevice::WillDestroyCurrentMessageLoop() {
  LOG(ERROR) << "IO loop going away before the audio device has been stopped";
  ShutDownOnIOThread();
}

// AudioDevice::AudioThreadCallback

AudioDevice::AudioThreadCallback::AudioThreadCallback(
    const AudioParameters& audio_parameters,
    base::SharedMemoryHandle memory,
    int memory_length,
    media::AudioRendererSink::RenderCallback* render_callback)
    : AudioDeviceThread::Callback(audio_parameters, memory, memory_length),
      render_callback_(render_callback) {
}

AudioDevice::AudioThreadCallback::~AudioThreadCallback() {
}

void AudioDevice::AudioThreadCallback::MapSharedMemory() {
  shared_memory_.Map(media::TotalSharedMemorySizeInBytes(memory_length_));
}

// Called whenever we receive notifications about pending data.
void AudioDevice::AudioThreadCallback::Process(int pending_data) {
  if (pending_data == media::AudioOutputController::kPauseMark) {
    memset(shared_memory_.memory(), 0, memory_length_);
    media::SetActualDataSizeInBytes(&shared_memory_, memory_length_, 0);
    return;
  }

  // Convert the number of pending bytes in the render buffer
  // into milliseconds.
  int audio_delay_milliseconds = pending_data / bytes_per_ms_;

  TRACE_EVENT0("audio", "AudioDevice::FireRenderCallback");

  // Update the audio-delay measurement then ask client to render audio.
  size_t num_frames = render_callback_->Render(audio_data_,
      audio_parameters_.frames_per_buffer(), audio_delay_milliseconds);

  // Interleave, scale, and clip to int16.
  // TODO(crogers): avoid converting to integer here, and pass the data
  // to the browser process as float, so we don't lose precision for
  // audio hardware which has better than 16bit precision.
  int16* data = reinterpret_cast<int16*>(shared_memory_.memory());
  media::InterleaveFloatToInt16(audio_data_, data,
      audio_parameters_.frames_per_buffer());

  // Let the host know we are done.
  media::SetActualDataSizeInBytes(&shared_memory_, memory_length_,
      num_frames * audio_parameters_.channels() * sizeof(data[0]));
}
