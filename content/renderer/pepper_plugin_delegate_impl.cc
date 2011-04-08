// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/pepper_plugin_delegate_impl.h"

#include <cmath>
#include <queue>

#include "base/callback.h"
#include "base/file_path.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/string_split.h"
#include "base/task.h"
#include "base/time.h"
#include "chrome/common/pepper_plugin_registry.h"
#include "chrome/common/render_messages.h"
#include "chrome/renderer/render_thread.h"
#include "content/common/audio_messages.h"
#include "content/common/child_process_messages.h"
#include "content/common/child_thread.h"
#include "content/common/file_system/file_system_dispatcher.h"
#include "content/common/pepper_file_messages.h"
#include "content/common/pepper_messages.h"
#include "content/common/view_messages.h"
#include "content/renderer/audio_message_filter.h"
#include "content/renderer/command_buffer_proxy.h"
#include "content/renderer/content_renderer_client.h"
#include "content/renderer/renderer_gl_context.h"
#include "content/renderer/gpu_channel_host.h"
#include "content/renderer/pepper_platform_context_3d_impl.h"
#include "content/renderer/render_view.h"
#include "content/renderer/webgraphicscontext3d_command_buffer_impl.h"
#include "content/renderer/webplugin_delegate_proxy.h"
#include "ipc/ipc_channel_handle.h"
#include "ppapi/c/dev/pp_video_dev.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/c/private/ppb_flash.h"
#include "ppapi/c/private/ppb_flash_net_connector.h"
#include "ppapi/proxy/host_dispatcher.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFileChooserCompletion.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFileChooserParams.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebPluginContainer.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebScreenInfo.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebView.h"
#include "ui/gfx/size.h"
#include "ui/gfx/surface/transport_dib.h"
#include "webkit/fileapi/file_system_callback_dispatcher.h"
#include "webkit/glue/context_menu.h"
#include "webkit/plugins/npapi/webplugin.h"
#include "webkit/plugins/ppapi/file_path.h"
#include "webkit/plugins/ppapi/ppb_file_io_impl.h"
#include "webkit/plugins/ppapi/plugin_module.h"
#include "webkit/plugins/ppapi/ppapi_plugin_instance.h"
#include "webkit/plugins/ppapi/ppb_broker_impl.h"
#include "webkit/plugins/ppapi/ppb_flash_impl.h"
#include "webkit/plugins/ppapi/ppb_flash_net_connector_impl.h"

using WebKit::WebView;

namespace {

const int32 kDefaultCommandBufferSize = 1024 * 1024;

// Implements the Image2D using a TransportDIB.
class PlatformImage2DImpl
    : public webkit::ppapi::PluginDelegate::PlatformImage2D {
 public:
  // This constructor will take ownership of the dib pointer.
  // On Mac, we assume that the dib is cached by the browser, so on destruction
  // we'll tell the browser to free it.
  PlatformImage2DImpl(int width, int height, TransportDIB* dib)
      : width_(width),
        height_(height),
        dib_(dib) {
  }

#if defined(OS_MACOSX)
  // On Mac, we have to tell the browser to free the transport DIB.
  virtual ~PlatformImage2DImpl() {
    if (dib_.get()) {
      RenderThread::current()->Send(
          new ViewHostMsg_FreeTransportDIB(dib_->id()));
    }
  }
#endif

  virtual skia::PlatformCanvas* Map() {
    return dib_->GetPlatformCanvas(width_, height_);
  }

  virtual intptr_t GetSharedMemoryHandle(uint32* byte_count) const {
    *byte_count = dib_->size();
#if defined(OS_WIN)
    return reinterpret_cast<intptr_t>(dib_->handle());
#elif defined(OS_MACOSX)
    return static_cast<intptr_t>(dib_->handle().fd);
#elif defined(OS_LINUX)
    return static_cast<intptr_t>(dib_->handle());
#endif
  }

  virtual TransportDIB* GetTransportDIB() const {
    return dib_.get();
  }

 private:
  int width_;
  int height_;
  scoped_ptr<TransportDIB> dib_;

  DISALLOW_COPY_AND_ASSIGN(PlatformImage2DImpl);
};


class PlatformAudioImpl
    : public webkit::ppapi::PluginDelegate::PlatformAudio,
      public AudioMessageFilter::Delegate,
      public base::RefCountedThreadSafe<PlatformAudioImpl> {
 public:
  explicit PlatformAudioImpl(scoped_refptr<AudioMessageFilter> filter)
      : client_(NULL), filter_(filter), stream_id_(0),
        main_message_loop_(MessageLoop::current()) {
    DCHECK(filter_);
  }

  virtual ~PlatformAudioImpl() {
    // Make sure we have been shut down. Warning: this will usually happen on
    // the I/O thread!
    DCHECK_EQ(0, stream_id_);
    DCHECK(!client_);
  }

  // Initialize this audio context. StreamCreated() will be called when the
  // stream is created.
  bool Initialize(uint32_t sample_rate, uint32_t sample_count,
       webkit::ppapi::PluginDelegate::PlatformAudio::Client* client);

  // PlatformAudio implementation (called on main thread).
  virtual bool StartPlayback();
  virtual bool StopPlayback();
  virtual void ShutDown();

 private:
  // I/O thread backends to above functions.
  void InitializeOnIOThread(const AudioParameters& params);
  void StartPlaybackOnIOThread();
  void StopPlaybackOnIOThread();
  void ShutDownOnIOThread();

  virtual void OnRequestPacket(AudioBuffersState buffers_state) {
    LOG(FATAL) << "Should never get OnRequestPacket in PlatformAudioImpl";
  }

  virtual void OnStateChanged(AudioStreamState state) {}

  virtual void OnCreated(base::SharedMemoryHandle handle, uint32 length) {
    LOG(FATAL) << "Should never get OnCreated in PlatformAudioImpl";
  }

  virtual void OnLowLatencyCreated(base::SharedMemoryHandle handle,
                                   base::SyncSocket::Handle socket_handle,
                                   uint32 length);

  virtual void OnVolume(double volume) {}

  // The client to notify when the stream is created. THIS MUST ONLY BE
  // ACCESSED ON THE MAIN THREAD.
  webkit::ppapi::PluginDelegate::PlatformAudio::Client* client_;

  // MessageFilter used to send/receive IPC. THIS MUST ONLY BE ACCESSED ON THE
  // I/O thread except to send messages and get the message loop.
  scoped_refptr<AudioMessageFilter> filter_;

  // Our ID on the MessageFilter. THIS MUST ONLY BE ACCESSED ON THE I/O THREAD
  // or else you could race with the initialize function which sets it.
  int32 stream_id_;

  MessageLoop* main_message_loop_;

  DISALLOW_COPY_AND_ASSIGN(PlatformAudioImpl);
};

bool PlatformAudioImpl::Initialize(
    uint32_t sample_rate, uint32_t sample_count,
    webkit::ppapi::PluginDelegate::PlatformAudio::Client* client) {

  DCHECK(client);
  // Make sure we don't call init more than once.
  DCHECK_EQ(0, stream_id_);

  client_ = client;

  AudioParameters params;
  params.format = AudioParameters::AUDIO_PCM_LINEAR;
  params.channels = 2;
  params.sample_rate = sample_rate;
  params.bits_per_sample = 16;
  params.samples_per_packet = sample_count;

  filter_->message_loop()->PostTask(FROM_HERE,
      NewRunnableMethod(this, &PlatformAudioImpl::InitializeOnIOThread,
                        params));
  return true;
}

bool PlatformAudioImpl::StartPlayback() {
  if (filter_) {
    filter_->message_loop()->PostTask(FROM_HERE,
        NewRunnableMethod(this, &PlatformAudioImpl::StartPlaybackOnIOThread));
    return true;
  }
  return false;
}

bool PlatformAudioImpl::StopPlayback() {
  if (filter_) {
    filter_->message_loop()->PostTask(FROM_HERE,
        NewRunnableMethod(this, &PlatformAudioImpl::StopPlaybackOnIOThread));
    return true;
  }
  return false;
}

void PlatformAudioImpl::ShutDown() {
  // Called on the main thread to stop all audio callbacks. We must only change
  // the client on the main thread, and the delegates from the I/O thread.
  client_ = NULL;
  filter_->message_loop()->PostTask(FROM_HERE,
      NewRunnableMethod(this, &PlatformAudioImpl::ShutDownOnIOThread));
}

void PlatformAudioImpl::InitializeOnIOThread(const AudioParameters& params) {
  stream_id_ = filter_->AddDelegate(this);
  filter_->Send(new AudioHostMsg_CreateStream(0, stream_id_, params, true));
}

void PlatformAudioImpl::StartPlaybackOnIOThread() {
  if (stream_id_)
    filter_->Send(new AudioHostMsg_PlayStream(0, stream_id_));
}

void PlatformAudioImpl::StopPlaybackOnIOThread() {
  if (stream_id_)
    filter_->Send(new AudioHostMsg_PauseStream(0, stream_id_));
}

void PlatformAudioImpl::ShutDownOnIOThread() {
  // Make sure we don't call shutdown more than once.
  if (!stream_id_)
    return;

  filter_->Send(new AudioHostMsg_CloseStream(0, stream_id_));
  filter_->RemoveDelegate(stream_id_);
  stream_id_ = 0;

  Release();  // Release for the delegate, balances out the reference taken in
              // PepperPluginDelegateImpl::CreateAudio.
}

void PlatformAudioImpl::OnLowLatencyCreated(
    base::SharedMemoryHandle handle, base::SyncSocket::Handle socket_handle,
    uint32 length) {
#if defined(OS_WIN)
  DCHECK(handle);
  DCHECK(socket_handle);
#else
  DCHECK_NE(-1, handle.fd);
  DCHECK_NE(-1, socket_handle);
#endif
  DCHECK(length);

  if (MessageLoop::current() == main_message_loop_) {
    // Must dereference the client only on the main thread. Shutdown may have
    // occurred while the request was in-flight, so we need to NULL check.
    if (client_)
      client_->StreamCreated(handle, length, socket_handle);
  } else {
    main_message_loop_->PostTask(FROM_HERE,
        NewRunnableMethod(this, &PlatformAudioImpl::OnLowLatencyCreated,
                          handle, socket_handle, length));
  }
}

class DispatcherWrapper
    : public webkit::ppapi::PluginDelegate::OutOfProcessProxy {
 public:
  DispatcherWrapper() {}
  virtual ~DispatcherWrapper() {}

  bool Init(base::ProcessHandle plugin_process_handle,
            const IPC::ChannelHandle& channel_handle,
            PP_Module pp_module,
            pp::proxy::Dispatcher::GetInterfaceFunc local_get_interface);

  // OutOfProcessProxy implementation.
  virtual const void* GetProxiedInterface(const char* name) {
    return dispatcher_->GetProxiedInterface(name);
  }
  virtual void AddInstance(PP_Instance instance) {
    pp::proxy::HostDispatcher::SetForInstance(instance, dispatcher_.get());
  }
  virtual void RemoveInstance(PP_Instance instance) {
    pp::proxy::HostDispatcher::RemoveForInstance(instance);
  }

 private:
  scoped_ptr<pp::proxy::HostDispatcher> dispatcher_;
};

bool DispatcherWrapper::Init(
    base::ProcessHandle plugin_process_handle,
    const IPC::ChannelHandle& channel_handle,
    PP_Module pp_module,
    pp::proxy::Dispatcher::GetInterfaceFunc local_get_interface) {
  dispatcher_.reset(new pp::proxy::HostDispatcher(
      plugin_process_handle, pp_module, local_get_interface));

  if (!dispatcher_->InitWithChannel(PepperPluginRegistry::GetInstance(),
                                    channel_handle, true)) {
    dispatcher_.reset();
    return false;
  }
  dispatcher_->channel()->SetRestrictDispatchToSameChannel(true);
  return true;
}

}  // namespace

PepperPluginDelegateImpl::PepperPluginDelegateImpl(RenderView* render_view)
    : render_view_(render_view),
      has_saved_context_menu_action_(false),
      saved_context_menu_action_(0),
      id_generator_(0) {
}

PepperPluginDelegateImpl::~PepperPluginDelegateImpl() {
}

scoped_refptr<webkit::ppapi::PluginModule>
PepperPluginDelegateImpl::CreatePepperPlugin(
    const FilePath& path,
    bool* pepper_plugin_was_registered) {
  *pepper_plugin_was_registered = true;

  // See if a module has already been loaded for this plugin.
  scoped_refptr<webkit::ppapi::PluginModule> module =
      PepperPluginRegistry::GetInstance()->GetLiveModule(path);
  if (module)
    return module;

  // In-process plugins will have always been created up-front to avoid the
  // sandbox restrictions. So getting here implies it doesn't exist or should
  // be out of process.
  const PepperPluginInfo* info =
      PepperPluginRegistry::GetInstance()->GetInfoForPlugin(path);
  if (!info) {
    *pepper_plugin_was_registered = false;
    return scoped_refptr<webkit::ppapi::PluginModule>();
  } else if (!info->is_out_of_process) {
    // In-process plugin not preloaded, it probably couldn't be initialized.
    return scoped_refptr<webkit::ppapi::PluginModule>();
  }

  // Out of process: have the browser start the plugin process for us.
  base::ProcessHandle plugin_process_handle = base::kNullProcessHandle;
  IPC::ChannelHandle channel_handle;
  render_view_->Send(new ViewHostMsg_OpenChannelToPepperPlugin(
      path, &plugin_process_handle, &channel_handle));
  if (channel_handle.name.empty()) {
    // Couldn't be initialized.
    return scoped_refptr<webkit::ppapi::PluginModule>();
  }

  // Create a new HostDispatcher for the proxying, and hook it to a new
  // PluginModule. Note that AddLiveModule must be called before any early
  // returns since the module's destructor will remove itself.
  module = new webkit::ppapi::PluginModule(info->name, path,
                                           PepperPluginRegistry::GetInstance());
  PepperPluginRegistry::GetInstance()->AddLiveModule(path, module);
  scoped_ptr<DispatcherWrapper> dispatcher(new DispatcherWrapper);
  if (!dispatcher->Init(
          plugin_process_handle, channel_handle,
          module->pp_module(),
          webkit::ppapi::PluginModule::GetLocalGetInterfaceFunc()))
    return scoped_refptr<webkit::ppapi::PluginModule>();
  module->InitAsProxied(dispatcher.release());
  return module;
}

void PepperPluginDelegateImpl::ViewInitiatedPaint() {
  // Notify all of our instances that we started painting. This is used for
  // internal bookkeeping only, so we know that the set can not change under
  // us.
  for (std::set<webkit::ppapi::PluginInstance*>::iterator i =
           active_instances_.begin();
       i != active_instances_.end(); ++i)
    (*i)->ViewInitiatedPaint();
}

void PepperPluginDelegateImpl::ViewFlushedPaint() {
  // Notify all instances that we painted. This will call into the plugin, and
  // we it may ask to close itself as a result. This will, in turn, modify our
  // set, possibly invalidating the iterator. So we iterate on a copy that
  // won't change out from under us.
  std::set<webkit::ppapi::PluginInstance*> plugins = active_instances_;
  for (std::set<webkit::ppapi::PluginInstance*>::iterator i = plugins.begin();
       i != plugins.end(); ++i) {
    // The copy above makes sure our iterator is never invalid if some plugins
    // are destroyed. But some plugin may decide to close all of its views in
    // response to a paint in one of them, so we need to make sure each one is
    // still "current" before using it.
    //
    // It's possible that a plugin was destroyed, but another one was created
    // with the same address. In this case, we'll call ViewFlushedPaint on that
    // new plugin. But that's OK for this particular case since we're just
    // notifying all of our instances that the view flushed, and the new one is
    // one of our instances.
    //
    // What about the case where a new one is created in a callback at a new
    // address and we don't issue the callback? We're still OK since this
    // callback is used for flush callbacks and we could not have possibly
    // started a new paint (ViewInitiatedPaint) for the new plugin while
    // processing a previous paint for an existing one.
    if (active_instances_.find(*i) != active_instances_.end())
      (*i)->ViewFlushedPaint();
  }
}

webkit::ppapi::PluginInstance*
PepperPluginDelegateImpl::GetBitmapForOptimizedPluginPaint(
    const gfx::Rect& paint_bounds,
    TransportDIB** dib,
    gfx::Rect* location,
    gfx::Rect* clip) {
  for (std::set<webkit::ppapi::PluginInstance*>::iterator i =
           active_instances_.begin();
       i != active_instances_.end(); ++i) {
    webkit::ppapi::PluginInstance* instance = *i;
    if (instance->GetBitmapForOptimizedPluginPaint(
            paint_bounds, dib, location, clip))
      return *i;
  }
  return NULL;
}

void PepperPluginDelegateImpl::PluginCrashed(
    webkit::ppapi::PluginInstance* instance) {
  render_view_->PluginCrashed(instance->module()->path());
}

void PepperPluginDelegateImpl::InstanceCreated(
    webkit::ppapi::PluginInstance* instance) {
  active_instances_.insert(instance);

  // Set the initial focus.
  instance->SetContentAreaFocus(render_view_->has_focus());
}

void PepperPluginDelegateImpl::InstanceDeleted(
    webkit::ppapi::PluginInstance* instance) {
  active_instances_.erase(instance);
}

SkBitmap* PepperPluginDelegateImpl::GetSadPluginBitmap() {
  return content::GetContentClient()->renderer()->GetSadPluginBitmap();
}

webkit::ppapi::PluginDelegate::PlatformImage2D*
PepperPluginDelegateImpl::CreateImage2D(int width, int height) {
  uint32 buffer_size = width * height * 4;

  // Allocate the transport DIB and the PlatformCanvas pointing to it.
#if defined(OS_MACOSX)
  // On the Mac, shared memory has to be created in the browser in order to
  // work in the sandbox.  Do this by sending a message to the browser
  // requesting a TransportDIB (see also
  // chrome/renderer/webplugin_delegate_proxy.cc, method
  // WebPluginDelegateProxy::CreateBitmap() for similar code). The TransportDIB
  // is cached in the browser, and is freed (in typical cases) by the
  // PlatformImage2DImpl's destructor.
  TransportDIB::Handle dib_handle;
  IPC::Message* msg = new ViewHostMsg_AllocTransportDIB(buffer_size,
                                                        true,
                                                        &dib_handle);
  if (!RenderThread::current()->Send(msg))
    return NULL;
  if (!TransportDIB::is_valid_handle(dib_handle))
    return NULL;

  TransportDIB* dib = TransportDIB::Map(dib_handle);
#else
  static int next_dib_id = 0;
  TransportDIB* dib = TransportDIB::Create(buffer_size, next_dib_id++);
  if (!dib)
    return NULL;
#endif

  return new PlatformImage2DImpl(width, height, dib);
}

webkit::ppapi::PluginDelegate::PlatformContext3D*
    PepperPluginDelegateImpl::CreateContext3D() {
#ifdef ENABLE_GPU
  // If accelerated compositing of plugins is disabled, fail to create a 3D
  // context, because it won't be visible. This allows graceful fallback in the
  // modules.
  if (!render_view_->webkit_preferences().accelerated_plugins_enabled)
    return NULL;
  WebGraphicsContext3DCommandBufferImpl* context =
      static_cast<WebGraphicsContext3DCommandBufferImpl*>(
          render_view_->webview()->graphicsContext3D());
  if (!context || context->isContextLost())
    return NULL;

  RendererGLContext* parent_context = context->context();
  if (!parent_context)
    return NULL;

  return new PlatformContext3DImpl(parent_context);
#else
  return NULL;
#endif
}

webkit::ppapi::PluginDelegate::PlatformVideoDecoder*
PepperPluginDelegateImpl::CreateVideoDecoder(
    PP_VideoDecoderConfig_Dev* decoder_config) {
  // TODO(vmr): Implement.
  NOTIMPLEMENTED();
  return NULL;
}

void PepperPluginDelegateImpl::NumberOfFindResultsChanged(int identifier,
                                                          int total,
                                                          bool final_result) {
  render_view_->reportFindInPageMatchCount(identifier, total, final_result);
}

void PepperPluginDelegateImpl::SelectedFindResultChanged(int identifier,
                                                         int index) {
  render_view_->reportFindInPageSelection(
      identifier, index + 1, WebKit::WebRect());
}

webkit::ppapi::PluginDelegate::PlatformAudio*
PepperPluginDelegateImpl::CreateAudio(
    uint32_t sample_rate, uint32_t sample_count,
    webkit::ppapi::PluginDelegate::PlatformAudio::Client* client) {
  scoped_refptr<PlatformAudioImpl> audio(
      new PlatformAudioImpl(render_view_->audio_message_filter()));
  if (audio->Initialize(sample_rate, sample_count, client)) {
    // Balanced by Release invoked in PlatformAudioImpl::ShutDownOnIOThread().
    return audio.release();
  } else {
    return NULL;
  }
}

webkit::ppapi::PluginDelegate::PpapiBroker*
PepperPluginDelegateImpl::ConnectToPpapiBroker(
    webkit::ppapi::PluginInstance* instance,
    webkit::ppapi::PPB_Broker_Impl* client) {
  CHECK(instance);
  CHECK(client);

  // TODO(ddorwin): Add IPC to broker process to do the following.
  // 1) Check if there is an existing broker for instance->module().
  // 2) If not, create.
  // 3) broker->Connect(client).
  //     * Asynchronously launches broker if necessary, establishes pipe, and
  //       calls BrokerConnected.
  // 4) Return pointer to broker.

  return NULL;
}

bool PepperPluginDelegateImpl::RunFileChooser(
    const WebKit::WebFileChooserParams& params,
    WebKit::WebFileChooserCompletion* chooser_completion) {
  return render_view_->runFileChooser(params, chooser_completion);
}

bool PepperPluginDelegateImpl::AsyncOpenFile(const FilePath& path,
                                             int flags,
                                             AsyncOpenFileCallback* callback) {
  int message_id = id_generator_++;
  DCHECK(!messages_waiting_replies_.Lookup(message_id));
  messages_waiting_replies_.AddWithID(callback, message_id);
  IPC::Message* msg = new ViewHostMsg_AsyncOpenFile(
      render_view_->routing_id(), path, flags, message_id);
  return render_view_->Send(msg);
}

void PepperPluginDelegateImpl::OnAsyncFileOpened(
    base::PlatformFileError error_code,
    base::PlatformFile file,
    int message_id) {
  AsyncOpenFileCallback* callback =
      messages_waiting_replies_.Lookup(message_id);
  DCHECK(callback);
  messages_waiting_replies_.Remove(message_id);
  callback->Run(error_code, file);
  delete callback;
}

void PepperPluginDelegateImpl::OnSetFocus(bool has_focus) {
  for (std::set<webkit::ppapi::PluginInstance*>::iterator i =
         active_instances_.begin();
       i != active_instances_.end(); ++i)
    (*i)->SetContentAreaFocus(has_focus);
}

bool PepperPluginDelegateImpl::OpenFileSystem(
    const GURL& url,
    fileapi::FileSystemType type,
    long long size,
    fileapi::FileSystemCallbackDispatcher* dispatcher) {
  FileSystemDispatcher* file_system_dispatcher =
      ChildThread::current()->file_system_dispatcher();
  return file_system_dispatcher->OpenFileSystem(
      url, type, size, true /* create */, dispatcher);
}

bool PepperPluginDelegateImpl::MakeDirectory(
    const FilePath& path,
    bool recursive,
    fileapi::FileSystemCallbackDispatcher* dispatcher) {
  FileSystemDispatcher* file_system_dispatcher =
      ChildThread::current()->file_system_dispatcher();
  return file_system_dispatcher->Create(
      path, false, true, recursive, dispatcher);
}

bool PepperPluginDelegateImpl::Query(
    const FilePath& path,
    fileapi::FileSystemCallbackDispatcher* dispatcher) {
  FileSystemDispatcher* file_system_dispatcher =
      ChildThread::current()->file_system_dispatcher();
  return file_system_dispatcher->ReadMetadata(path, dispatcher);
}

bool PepperPluginDelegateImpl::Touch(
    const FilePath& path,
    const base::Time& last_access_time,
    const base::Time& last_modified_time,
    fileapi::FileSystemCallbackDispatcher* dispatcher) {
  FileSystemDispatcher* file_system_dispatcher =
      ChildThread::current()->file_system_dispatcher();
  return file_system_dispatcher->TouchFile(path, last_access_time,
                                           last_modified_time, dispatcher);
}

bool PepperPluginDelegateImpl::Delete(
    const FilePath& path,
    fileapi::FileSystemCallbackDispatcher* dispatcher) {
  FileSystemDispatcher* file_system_dispatcher =
      ChildThread::current()->file_system_dispatcher();
  return file_system_dispatcher->Remove(path, false /* recursive */,
                                        dispatcher);
}

bool PepperPluginDelegateImpl::Rename(
    const FilePath& file_path,
    const FilePath& new_file_path,
    fileapi::FileSystemCallbackDispatcher* dispatcher) {
  FileSystemDispatcher* file_system_dispatcher =
      ChildThread::current()->file_system_dispatcher();
  return file_system_dispatcher->Move(file_path, new_file_path, dispatcher);
}

bool PepperPluginDelegateImpl::ReadDirectory(
    const FilePath& directory_path,
    fileapi::FileSystemCallbackDispatcher* dispatcher) {
  FileSystemDispatcher* file_system_dispatcher =
      ChildThread::current()->file_system_dispatcher();
  return file_system_dispatcher->ReadDirectory(directory_path, dispatcher);
}

base::PlatformFileError PepperPluginDelegateImpl::OpenFile(
    const webkit::ppapi::PepperFilePath& path,
    int flags,
    base::PlatformFile* file) {
  IPC::PlatformFileForTransit transit_file;
  base::PlatformFileError error;
  IPC::Message* msg = new PepperFileMsg_OpenFile(
      path, flags, &error, &transit_file);
  if (!render_view_->Send(msg)) {
    *file = base::kInvalidPlatformFileValue;
    return base::PLATFORM_FILE_ERROR_FAILED;
  }
  *file = IPC::PlatformFileForTransitToPlatformFile(transit_file);
  return error;
}

base::PlatformFileError PepperPluginDelegateImpl::RenameFile(
    const webkit::ppapi::PepperFilePath& from_path,
    const webkit::ppapi::PepperFilePath& to_path) {
  base::PlatformFileError error;
  IPC::Message* msg = new PepperFileMsg_RenameFile(from_path, to_path, &error);
  if (!render_view_->Send(msg))
    return base::PLATFORM_FILE_ERROR_FAILED;
  return error;
}

base::PlatformFileError PepperPluginDelegateImpl::DeleteFileOrDir(
    const webkit::ppapi::PepperFilePath& path,
    bool recursive) {
  base::PlatformFileError error;
  IPC::Message* msg = new PepperFileMsg_DeleteFileOrDir(
      path, recursive, &error);
  if (!render_view_->Send(msg))
    return base::PLATFORM_FILE_ERROR_FAILED;
  return error;
}

base::PlatformFileError PepperPluginDelegateImpl::CreateDir(
    const webkit::ppapi::PepperFilePath& path) {
  base::PlatformFileError error;
  IPC::Message* msg = new PepperFileMsg_CreateDir(path, &error);
  if (!render_view_->Send(msg))
    return base::PLATFORM_FILE_ERROR_FAILED;
  return error;
}

base::PlatformFileError PepperPluginDelegateImpl::QueryFile(
    const webkit::ppapi::PepperFilePath& path,
    base::PlatformFileInfo* info) {
  base::PlatformFileError error;
  IPC::Message* msg = new PepperFileMsg_QueryFile(path, info, &error);
  if (!render_view_->Send(msg))
    return base::PLATFORM_FILE_ERROR_FAILED;
  return error;
}

base::PlatformFileError PepperPluginDelegateImpl::GetDirContents(
    const webkit::ppapi::PepperFilePath& path,
    webkit::ppapi::DirContents* contents) {
  base::PlatformFileError error;
  IPC::Message* msg = new PepperFileMsg_GetDirContents(path, contents, &error);
  if (!render_view_->Send(msg))
    return base::PLATFORM_FILE_ERROR_FAILED;
  return error;
}

scoped_refptr<base::MessageLoopProxy>
PepperPluginDelegateImpl::GetFileThreadMessageLoopProxy() {
  return RenderThread::current()->GetFileThreadMessageLoopProxy();
}

int32_t PepperPluginDelegateImpl::ConnectTcp(
    webkit::ppapi::PPB_Flash_NetConnector_Impl* connector,
    const char* host,
    uint16_t port) {
  int request_id = pending_connect_tcps_.Add(
      new scoped_refptr<webkit::ppapi::PPB_Flash_NetConnector_Impl>(connector));
  IPC::Message* msg =
      new PepperMsg_ConnectTcp(render_view_->routing_id(),
                               request_id,
                               std::string(host),
                               port);
  if (!render_view_->Send(msg)) {
    pending_connect_tcps_.Remove(request_id);
    return PP_ERROR_FAILED;
  }

  return PP_ERROR_WOULDBLOCK;
}

int32_t PepperPluginDelegateImpl::ConnectTcpAddress(
    webkit::ppapi::PPB_Flash_NetConnector_Impl* connector,
    const struct PP_Flash_NetAddress* addr) {
  int request_id = pending_connect_tcps_.Add(
      new scoped_refptr<webkit::ppapi::PPB_Flash_NetConnector_Impl>(connector));
  IPC::Message* msg =
      new PepperMsg_ConnectTcpAddress(render_view_->routing_id(),
                                      request_id,
                                      *addr);
  if (!render_view_->Send(msg)) {
    pending_connect_tcps_.Remove(request_id);
    return PP_ERROR_FAILED;
  }

  return PP_ERROR_WOULDBLOCK;
}

void PepperPluginDelegateImpl::OnConnectTcpACK(
    int request_id,
    base::PlatformFile socket,
    const PP_Flash_NetAddress& local_addr,
    const PP_Flash_NetAddress& remote_addr) {
  scoped_refptr<webkit::ppapi::PPB_Flash_NetConnector_Impl> connector =
      *pending_connect_tcps_.Lookup(request_id);
  pending_connect_tcps_.Remove(request_id);

  connector->CompleteConnectTcp(socket, local_addr, remote_addr);
}

int32_t PepperPluginDelegateImpl::ShowContextMenu(
    webkit::ppapi::PPB_Flash_Menu_Impl* menu,
    const gfx::Point& position) {
  int request_id = pending_context_menus_.Add(
      new scoped_refptr<webkit::ppapi::PPB_Flash_Menu_Impl>(menu));

  ContextMenuParams params;
  params.x = position.x();
  params.y = position.y();
  params.custom_context.is_pepper_menu = true;
  params.custom_context.request_id = request_id;
  params.custom_items = menu->menu_data();

  IPC::Message* msg = new ViewHostMsg_ContextMenu(render_view_->routing_id(),
                                                  params);
  if (!render_view_->Send(msg)) {
    pending_context_menus_.Remove(request_id);
    return PP_ERROR_FAILED;
  }

  return PP_ERROR_WOULDBLOCK;
}

void PepperPluginDelegateImpl::OnContextMenuClosed(
    const webkit_glue::CustomContextMenuContext& custom_context) {
  int request_id = custom_context.request_id;
  scoped_refptr<webkit::ppapi::PPB_Flash_Menu_Impl> menu =
      *pending_context_menus_.Lookup(request_id);
  if (!menu) {
    NOTREACHED() << "CompleteShowContextMenu() called twice for the same menu.";
    return;
  }
  pending_context_menus_.Remove(request_id);

  if (has_saved_context_menu_action_) {
    menu->CompleteShow(PP_OK, saved_context_menu_action_);
    has_saved_context_menu_action_ = false;
    saved_context_menu_action_ = 0;
  } else {
    menu->CompleteShow(PP_ERROR_USERCANCEL, 0);
  }
}

void PepperPluginDelegateImpl::OnCustomContextMenuAction(
    const webkit_glue::CustomContextMenuContext& custom_context,
    unsigned action) {
  // Just save the action.
  DCHECK(!has_saved_context_menu_action_);
  has_saved_context_menu_action_ = true;
  saved_context_menu_action_ = action;
}

webkit::ppapi::FullscreenContainer*
PepperPluginDelegateImpl::CreateFullscreenContainer(
    webkit::ppapi::PluginInstance* instance) {
  return render_view_->CreatePepperFullscreenContainer(instance);
}

gfx::Size PepperPluginDelegateImpl::GetScreenSize() {
  WebKit::WebScreenInfo info = render_view_->screenInfo();
  return gfx::Size(info.rect.width, info.rect.height);
}

std::string PepperPluginDelegateImpl::GetDefaultEncoding() {
  // TODO(brettw) bug 56615: Somehow get the preference for the default
  // encoding here rather than using the global default for the UI language.
  return content::GetContentClient()->renderer()->GetDefaultEncoding();
}

void PepperPluginDelegateImpl::ZoomLimitsChanged(double minimum_factor,
                                                 double maximum_factor) {
  double minimum_level = WebView::zoomFactorToZoomLevel(minimum_factor);
  double maximum_level = WebView::zoomFactorToZoomLevel(maximum_factor);
  render_view_->webview()->zoomLimitsChanged(minimum_level, maximum_level);
}

std::string PepperPluginDelegateImpl::ResolveProxy(const GURL& url) {
  int net_error;
  std::string proxy_result;
  RenderThread::current()->Send(
      new ChildProcessHostMsg_ResolveProxy(url, &net_error, &proxy_result));
  return proxy_result;
}

void PepperPluginDelegateImpl::DidStartLoading() {
  render_view_->DidStartLoadingForPlugin();
}

void PepperPluginDelegateImpl::DidStopLoading() {
  render_view_->DidStopLoadingForPlugin();
}

void PepperPluginDelegateImpl::SetContentRestriction(int restrictions) {
  render_view_->Send(new ViewHostMsg_UpdateContentRestrictions(
      render_view_->routing_id(), restrictions));
}

void PepperPluginDelegateImpl::HasUnsupportedFeature() {
  render_view_->Send(new ViewHostMsg_PDFHasUnsupportedFeature(
      render_view_->routing_id()));
}

P2PSocketDispatcher* PepperPluginDelegateImpl::GetP2PSocketDispatcher() {
  return render_view_->p2p_socket_dispatcher();
}
