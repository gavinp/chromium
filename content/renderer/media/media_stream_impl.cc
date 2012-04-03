// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/media/media_stream_impl.h"

#include <utility>

#include "base/bind.h"
#include "base/logging.h"
#include "base/synchronization/waitable_event.h"
#include "base/utf_string_conversions.h"
#include "content/renderer/media/capture_video_decoder.h"
#include "content/renderer/media/media_stream_dependency_factory.h"
#include "content/renderer/media/media_stream_dispatcher.h"
#include "content/renderer/media/peer_connection_handler.h"
#include "content/renderer/media/video_capture_impl_manager.h"
#include "content/renderer/media/video_capture_module_impl.h"
#include "content/renderer/media/webrtc_audio_device_impl.h"
#include "content/renderer/p2p/ipc_network_manager.h"
#include "content/renderer/p2p/ipc_socket_factory.h"
#include "content/renderer/p2p/socket_dispatcher.h"
#include "jingle/glue/thread_wrapper.h"
#include "media/base/message_loop_factory.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebMediaStreamRegistry.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebSecurityOrigin.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebMediaStreamDescriptor.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebMediaStreamSource.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebVector.h"

namespace {

const int kVideoCaptureWidth = 640;
const int kVideoCaptureHeight = 480;
const int kVideoCaptureFramePerSecond = 30;

}  // namespace

// The MediaStreamMananger label for a stream is globally unique. The track
// session id is globally unique for the set of audio tracks and video tracks
// respectively. An audio track and a video track can have the same session id
// (without being related). Hence we create a unique track label from the stream
// label, track type and track session id:
// <MediaStreamManager-label>#{audio,video}-<session-ID>.
static std::string CreateTrackLabel(
    const std::string& manager_label,
    int session_id,
    bool is_video) {
  std::string track_label = manager_label;
  if (is_video) {
    track_label += "#video-";
  } else {
    track_label += "#audio-";
  }
  track_label += session_id;
  return track_label;
}

// Extracting the MediaStreamManager stream label will only work for track
// labels created by CreateTrackLabel. If is wasn't, the contents of the
// returned string is undefined.
static std::string ExtractManagerStreamLabel(
    const std::string& track_label) {
  std::string manager_label = track_label;
  size_t pos = manager_label.rfind("#");
  // If # isn't found, the string is left intact.
  manager_label = manager_label.substr(0, pos);
  return manager_label;
}


int MediaStreamImpl::next_request_id_ = 0;

MediaStreamImpl::MediaStreamImpl(
    MediaStreamDispatcher* media_stream_dispatcher,
    content::P2PSocketDispatcher* p2p_socket_dispatcher,
    VideoCaptureImplManager* vc_manager,
    MediaStreamDependencyFactory* dependency_factory)
    : dependency_factory_(dependency_factory),
      media_stream_dispatcher_(media_stream_dispatcher),
      p2p_socket_dispatcher_(p2p_socket_dispatcher),
      network_manager_(NULL),
      vc_manager_(vc_manager),
      peer_connection_handler_(NULL),
      message_loop_proxy_(base::MessageLoopProxy::current()),
      signaling_thread_(NULL),
      worker_thread_(NULL),
      chrome_worker_thread_("Chrome_libJingle_WorkerThread") {
}

MediaStreamImpl::~MediaStreamImpl() {
  DCHECK(!peer_connection_handler_);
  if (dependency_factory_.get())
    dependency_factory_->ReleasePeerConnectionFactory();
  if (network_manager_) {
    // The network manager needs to free its resources on the thread they were
    // created, which is the worked thread.
    if (chrome_worker_thread_.IsRunning()) {
      chrome_worker_thread_.message_loop()->PostTask(FROM_HERE, base::Bind(
          &MediaStreamImpl::DeleteIpcNetworkManager,
          base::Unretained(this)));
      // Stopping the thread will wait until all tasks have been
      // processed before returning. We wait for the above task to finish before
      // letting the destructor continue to avoid any potential race issues.
      chrome_worker_thread_.Stop();
    } else {
      NOTREACHED() << "Worker thread not running.";
    }
  }
}

WebKit::WebPeerConnectionHandler* MediaStreamImpl::CreatePeerConnectionHandler(
    WebKit::WebPeerConnectionHandlerClient* client) {
  DCHECK(CalledOnValidThread());
  if (peer_connection_handler_) {
    DVLOG(1) << "A PeerConnection already exists";
    return NULL;
  }
  if (!EnsurePeerConnectionFactory())
    return NULL;

  peer_connection_handler_ = new PeerConnectionHandler(
      client,
      this,
      dependency_factory_.get());

  return peer_connection_handler_;
}

void MediaStreamImpl::ClosePeerConnection() {
  DCHECK(CalledOnValidThread());
  video_renderer_ = NULL;
  peer_connection_handler_ = NULL;
  // TODO(grunell): This is a temporary workaround for an error in native
  // PeerConnection where added live tracks are not seen on the remote side.
  MediaStreamTrackPtrMap::const_iterator it = local_tracks_.begin();
  for (; it != local_tracks_.end(); ++it)
    it->second->set_state(webrtc::MediaStreamTrackInterface::kEnded);
}

webrtc::MediaStreamTrackInterface* MediaStreamImpl::GetLocalMediaStreamTrack(
    const std::string& label) {
  DCHECK(CalledOnValidThread());
  MediaStreamTrackPtrMap::iterator it = local_tracks_.find(label);
  if (it == local_tracks_.end())
    return NULL;
  MediaStreamTrackPtr stream = it->second;
  return stream.get();
}

void MediaStreamImpl::requestUserMedia(
    const WebKit::WebUserMediaRequest& user_media_request,
    const WebKit::WebVector<WebKit::WebMediaStreamSource>& audio_sources,
    const WebKit::WebVector<WebKit::WebMediaStreamSource>& video_sources) {
  DCHECK(CalledOnValidThread());
  DCHECK(!user_media_request.isNull());

  int request_id = next_request_id_++;

  bool audio = user_media_request.audio();
  media_stream::StreamOptions::VideoOption video_option =
      media_stream::StreamOptions::kNoCamera;
  if (user_media_request.video())
    video_option = media_stream::StreamOptions::kFacingBoth;

  std::string security_origin = UTF16ToUTF8(
      user_media_request.securityOrigin().toString());

  DVLOG(1) << "MediaStreamImpl::generateStream(" << request_id << ", [ "
           << (audio ? "audio" : "")
           << (user_media_request.video() ? " video" : "") << "], "
           << security_origin << ")";

  user_media_requests_.insert(
      std::pair<int, WebKit::WebUserMediaRequest>(
          request_id, user_media_request));

  media_stream_dispatcher_->GenerateStream(
      request_id,
      this,
      media_stream::StreamOptions(audio, video_option),
      security_origin);
}

void MediaStreamImpl::cancelUserMediaRequest(
    const WebKit::WebUserMediaRequest& user_media_request) {
  DCHECK(CalledOnValidThread());
  // TODO(grunell): Implement.
  NOTIMPLEMENTED();
}

scoped_refptr<media::VideoDecoder> MediaStreamImpl::GetVideoDecoder(
    const GURL& url,
    media::MessageLoopFactory* message_loop_factory) {
  DCHECK(CalledOnValidThread());
  WebKit::WebMediaStreamDescriptor descriptor(
      WebKit::WebMediaStreamRegistry::lookupMediaStreamDescriptor(url));
  if (descriptor.isNull())
    return NULL;  // This is not a valid stream.

  // We must find out if this is a local or remote stream. We extract the
  // MediaStreamManager stream label and if found in the dispatcher we have a
  // local stream, otherwise we have a remote stream. There will be changes soon
  // so that we don't have to bother about the type of stream here. Hence this
  // solution is OK for now.

  WebKit::WebVector<WebKit::WebMediaStreamSource> source_vector;
  descriptor.sources(source_vector);
  std::string msm_label;
  for (size_t i = 0; i < source_vector.size(); ++i) {
    if (source_vector[i].type() == WebKit::WebMediaStreamSource::TypeVideo) {
      // We assume there is one video track only.
      msm_label = ExtractManagerStreamLabel(UTF16ToUTF8(source_vector[i].id()));
      break;
    }
  }
  if (msm_label.empty())
    return NULL;

  scoped_refptr<media::VideoDecoder> decoder;
  if (media_stream_dispatcher_->IsStream(msm_label)) {
    // It's a local stream.
    int video_session_id =
        media_stream_dispatcher_->video_session_id(msm_label, 0);
    media::VideoCapture::VideoCaptureCapability capability;
    capability.width = kVideoCaptureWidth;
    capability.height = kVideoCaptureHeight;
    capability.max_fps = kVideoCaptureFramePerSecond;
    capability.expected_capture_delay = 0;
    capability.raw_type = media::VideoFrame::I420;
    capability.interlaced = false;
    decoder = new CaptureVideoDecoder(
        message_loop_factory->GetMessageLoopProxy("CaptureVideoDecoderThread"),
        video_session_id,
        vc_manager_.get(),
        capability);
  } else {
    // It's a remote stream.
    if (!video_renderer_.get())
      video_renderer_ = new talk_base::RefCountedObject<VideoRendererWrapper>();
    if (video_renderer_->renderer()) {
      // The renderer is used by PeerConnection, release it first.
      if (peer_connection_handler_) {
        peer_connection_handler_->SetVideoRenderer(
            UTF16ToUTF8(descriptor.label()),
            NULL);
      }
      video_renderer_->SetVideoDecoder(NULL);
    }
    RTCVideoDecoder* rtc_video_decoder = new RTCVideoDecoder(
        message_loop_factory->GetMessageLoop("RtcVideoDecoderThread"),
        url.spec());
    decoder = rtc_video_decoder;
    video_renderer_->SetVideoDecoder(rtc_video_decoder);
    if (peer_connection_handler_) {
      peer_connection_handler_->SetVideoRenderer(
          UTF16ToUTF8(descriptor.label()),
          video_renderer_);
    }
  }
  return decoder;
}

void MediaStreamImpl::OnStreamGenerated(
    int request_id,
    const std::string& label,
    const media_stream::StreamDeviceInfoArray& audio_array,
    const media_stream::StreamDeviceInfoArray& video_array) {
  DCHECK(CalledOnValidThread());

  // Creating the peer connection factory can fail if for example the audio
  // (input or output) or video device cannot be opened. Handling such cases
  // better is a higher level design discussion which involves the media
  // manager, webrtc and libjingle. We should still continue and fire a
  // succeeded callback here, to maintain the same states in WebKit as in the
  // media manager in terms of streams and tracks. We cannot create any native
  // track objects however, so we'll just have to skip that. Furthermore,
  // creating a peer connection later on will fail if we don't have a factory.
  EnsurePeerConnectionFactory();

  // Add audio tracks.
  WebKit::WebVector<WebKit::WebMediaStreamSource> audio_source_vector(
      audio_array.size());
  std::string track_label;
  for (size_t i = 0; i < audio_array.size(); ++i) {
    track_label = CreateTrackLabel(label, audio_array[i].session_id, false);
    if (dependency_factory_->PeerConnectionFactoryCreated()) {
      MediaStreamTrackPtr audio_track(
          dependency_factory_->CreateLocalAudioTrack(audio_array[i].name,
                                                     NULL));
      local_tracks_.insert(
          std::pair<std::string, MediaStreamTrackPtr>(track_label,
                                                      audio_track));
    }
    audio_source_vector[i].initialize(
          UTF8ToUTF16(track_label),
          WebKit::WebMediaStreamSource::TypeAudio,
          UTF8ToUTF16(audio_array[i].name));
  }

  // Add video tracks.
  WebKit::WebVector<WebKit::WebMediaStreamSource> video_source_vector(
      video_array.size());
  for (size_t i = 0; i < video_array.size(); ++i) {
    track_label = CreateTrackLabel(label, video_array[i].session_id, true);
    if (dependency_factory_->PeerConnectionFactoryCreated()) {
      webrtc::VideoCaptureModule* vcm = new VideoCaptureModuleImpl(
          video_array[i].session_id,
          vc_manager_.get());
      MediaStreamTrackPtr video_track(
          dependency_factory_->CreateLocalVideoTrack(
              video_array[i].name,
              // The video capturer takes ownership of |vcm|.
              webrtc::CreateVideoCapturer(vcm)));
      local_tracks_.insert(
          std::pair<std::string, MediaStreamTrackPtr>(track_label,
                                                      video_track));
    }
    video_source_vector[i].initialize(
          UTF8ToUTF16(track_label),
          WebKit::WebMediaStreamSource::TypeVideo,
          UTF8ToUTF16(video_array[i].name));
  }

  // TODO(grunell): Remove tracks from the map when support to stop is
  // added in WebKit.

  MediaRequestMap::iterator it = user_media_requests_.find(request_id);
  if (it == user_media_requests_.end()) {
    DVLOG(1) << "Request ID not found";
    return;
  }
  WebKit::WebUserMediaRequest user_media_request = it->second;
  user_media_requests_.erase(it);

  user_media_request.requestSucceeded(audio_source_vector, video_source_vector);
}

void MediaStreamImpl::OnStreamGenerationFailed(int request_id) {
  DCHECK(CalledOnValidThread());
  DVLOG(1) << "MediaStreamImpl::OnStreamGenerationFailed("
           << request_id << ")";
  MediaRequestMap::iterator it = user_media_requests_.find(request_id);
  if (it == user_media_requests_.end()) {
    DVLOG(1) << "Request ID not found";
    return;
  }
  WebKit::WebUserMediaRequest user_media_request = it->second;
  user_media_requests_.erase(it);

  user_media_request.requestFailed();
}

void MediaStreamImpl::OnVideoDeviceFailed(const std::string& label,
                                          int index) {
  DCHECK(CalledOnValidThread());
  DVLOG(1) << "MediaStreamImpl::OnVideoDeviceFailed("
           << label << ", " << index << ")";
  // TODO(grunell): Implement. Currently not supported in WebKit.
  NOTIMPLEMENTED();
}

void MediaStreamImpl::OnAudioDeviceFailed(const std::string& label,
                                          int index) {
  DCHECK(CalledOnValidThread());
  DVLOG(1) << "MediaStreamImpl::OnAudioDeviceFailed("
           << label << ", " << index << ")";
  // TODO(grunell): Implement. Currently not supported in WebKit.
  NOTIMPLEMENTED();
}

void MediaStreamImpl::OnDevicesEnumerated(
    int request_id,
    const media_stream::StreamDeviceInfoArray& device_array) {
  DVLOG(1) << "MediaStreamImpl::OnDevicesEnumerated("
           << request_id << ")";
  NOTIMPLEMENTED();
}

void MediaStreamImpl::OnDevicesEnumerationFailed(int request_id) {
  DVLOG(1) << "MediaStreamImpl::OnDevicesEnumerationFailed("
           << request_id << ")";
  NOTIMPLEMENTED();
}

void MediaStreamImpl::OnDeviceOpened(
    int request_id,
    const std::string& label,
    const media_stream::StreamDeviceInfo& video_device) {
  DVLOG(1) << "MediaStreamImpl::OnDeviceOpened("
           << request_id << ", " << label << ")";
  NOTIMPLEMENTED();
}

void MediaStreamImpl::OnDeviceOpenFailed(int request_id) {
  DVLOG(1) << "MediaStreamImpl::VideoDeviceOpenFailed("
           << request_id << ")";
  NOTIMPLEMENTED();
}

void MediaStreamImpl::InitializeWorkerThread(talk_base::Thread** thread,
                                             base::WaitableEvent* event) {
  jingle_glue::JingleThreadWrapper::EnsureForCurrentThread();
  jingle_glue::JingleThreadWrapper::current()->set_send_allowed(true);
  *thread = jingle_glue::JingleThreadWrapper::current();
  event->Signal();
}

void MediaStreamImpl::CreateIpcNetworkManagerOnWorkerThread(
    base::WaitableEvent* event) {
  DCHECK_EQ(MessageLoop::current(), chrome_worker_thread_.message_loop());
  network_manager_ = new content::IpcNetworkManager(p2p_socket_dispatcher_);
  event->Signal();
}

void MediaStreamImpl::DeleteIpcNetworkManager() {
  DCHECK_EQ(MessageLoop::current(), chrome_worker_thread_.message_loop());
  delete network_manager_;
  network_manager_ = NULL;
}

bool MediaStreamImpl::EnsurePeerConnectionFactory() {
  DCHECK(CalledOnValidThread());
  if (!signaling_thread_) {
    jingle_glue::JingleThreadWrapper::EnsureForCurrentThread();
    jingle_glue::JingleThreadWrapper::current()->set_send_allowed(true);
    signaling_thread_ = jingle_glue::JingleThreadWrapper::current();
  }

  if (!worker_thread_) {
    if (!chrome_worker_thread_.IsRunning()) {
      if (!chrome_worker_thread_.Start()) {
        LOG(ERROR) << "Could not start worker thread";
        signaling_thread_ = NULL;
        return false;
      }
    }
    base::WaitableEvent event(true, false);
    chrome_worker_thread_.message_loop()->PostTask(FROM_HERE, base::Bind(
        &MediaStreamImpl::InitializeWorkerThread,
        this,
        &worker_thread_,
        &event));
    event.Wait();
    DCHECK(worker_thread_);
  }

  if (!network_manager_) {
    base::WaitableEvent event(true, false);
    chrome_worker_thread_.message_loop()->PostTask(FROM_HERE, base::Bind(
          &MediaStreamImpl::CreateIpcNetworkManagerOnWorkerThread,
          this,
          &event));
    event.Wait();
  }

  if (!socket_factory_.get()) {
    socket_factory_.reset(
        new content::IpcPacketSocketFactory(p2p_socket_dispatcher_));
  }

  if (!dependency_factory_->PeerConnectionFactoryCreated()) {
    if (!dependency_factory_->CreatePeerConnectionFactory(
            worker_thread_,
            signaling_thread_,
            p2p_socket_dispatcher_,
            network_manager_,
            socket_factory_.get())) {
      LOG(ERROR) << "Could not create PeerConnection factory";
      return false;
    }
  }

  return true;
}

MediaStreamImpl::VideoRendererWrapper::VideoRendererWrapper() {}

MediaStreamImpl::VideoRendererWrapper::~VideoRendererWrapper() {}

void MediaStreamImpl::VideoRendererWrapper::SetVideoDecoder(
    RTCVideoDecoder* decoder) {
  rtc_video_decoder_ = decoder;
}
