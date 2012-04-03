// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/nacl/nacl_listener.h"

#include <errno.h>
#include <stdlib.h>

#include "base/command_line.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "chrome/common/nacl_messages.h"
#include "chrome/nacl/nacl_validation_db.h"
#include "chrome/nacl/nacl_validation_query.h"
#include "ipc/ipc_sync_channel.h"
#include "ipc/ipc_sync_message_filter.h"
#include "ipc/ipc_switches.h"
#include "native_client/src/trusted/service_runtime/sel_main_chrome.h"

#if defined(OS_LINUX)
#include "content/public/common/child_process_sandbox_support_linux.h"
#endif

#if defined(OS_WIN)
#include <fcntl.h>
#include <io.h>
#endif

namespace {
#if defined(OS_MACOSX)

// On Mac OS X, shm_open() works in the sandbox but does not give us
// an FD that we can map as PROT_EXEC.  Rather than doing an IPC to
// get an executable SHM region when CreateMemoryObject() is called,
// we preallocate one on startup, since NaCl's sel_ldr only needs one
// of them.  This saves a round trip.

base::subtle::Atomic32 g_shm_fd = -1;

int CreateMemoryObject(size_t size, int executable) {
  if (executable && size > 0) {
    int result_fd = base::subtle::NoBarrier_AtomicExchange(&g_shm_fd, -1);
    if (result_fd != -1) {
      // ftruncate() is disallowed by the Mac OS X sandbox and
      // returns EPERM.  Luckily, we can get the same effect with
      // lseek() + write().
      if (lseek(result_fd, size - 1, SEEK_SET) == -1) {
        LOG(ERROR) << "lseek() failed: " << errno;
        return -1;
      }
      if (write(result_fd, "", 1) != 1) {
        LOG(ERROR) << "write() failed: " << errno;
        return -1;
      }
      return result_fd;
    }
  }
  // Fall back to NaCl's default implementation.
  return -1;
}

#elif defined(OS_LINUX)

int CreateMemoryObject(size_t size, int executable) {
  return content::MakeSharedMemorySegmentViaIPC(size, executable);
}

#endif

// Use an env var because command line args are eaten by nacl_helper.
bool CheckEnvVar(const char* name, bool default_value) {
  bool result = default_value;
  const char* var = getenv(name);
  if (var && strlen(var) > 0) {
    result = var[0] != '0';
  }
  return result;
}

}  // namespace

class BrowserValidationDBProxy : public NaClValidationDB {
 public:
  explicit BrowserValidationDBProxy(NaClListener* listener)
      : listener_(listener) {
  }

  bool QueryKnownToValidate(const std::string& signature) {
    // Initialize to false so that if the Send fails to write to the return
    // value we're safe.  For example if the message is (for some reason)
    // dispatched as an async message the return parameter will not be written.
    bool result = false;
    if (!listener_->Send(new NaClProcessMsg_QueryKnownToValidate(signature,
                                                                 &result))) {
      LOG(ERROR) << "Failed to query NaCl validation cache.";
      result = false;
    }
    return result;
  }

  void SetKnownToValidate(const std::string& signature) {
    // Caching is optional: NaCl will still work correctly if the IPC fails.
    if (!listener_->Send(new NaClProcessMsg_SetKnownToValidate(signature))) {
      LOG(ERROR) << "Failed to update NaCl validation cache.";
    }
  }

 private:
  // The listener never dies, otherwise this might be a dangling reference.
  NaClListener* listener_;
};


NaClListener::NaClListener() : shutdown_event_(true, false),
                               io_thread_("NaCl_IOThread"),
                               main_loop_(NULL),
                               debug_enabled_(false) {
  io_thread_.StartWithOptions(base::Thread::Options(MessageLoop::TYPE_IO, 0));
}

NaClListener::~NaClListener() {
  NOTREACHED();
  shutdown_event_.Signal();
}

bool NaClListener::Send(IPC::Message* msg) {
  DCHECK(main_loop_ != NULL);
  if (MessageLoop::current() == main_loop_) {
    // This thread owns the channel.
    return channel_->Send(msg);
  } else {
    // This thread does not own the channel.
    return filter_->Send(msg);
  }
}

void NaClListener::Listen() {
  std::string channel_name =
      CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kProcessChannelID);
  channel_.reset(new IPC::SyncChannel(this, io_thread_.message_loop_proxy(),
                                      &shutdown_event_));
  filter_.reset(new IPC::SyncMessageFilter(&shutdown_event_));
  channel_->AddFilter(filter_.get());
  channel_->Init(channel_name, IPC::Channel::MODE_CLIENT, true);
  main_loop_ = MessageLoop::current();
  main_loop_->Run();
}

bool NaClListener::OnMessageReceived(const IPC::Message& msg) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(NaClListener, msg)
      IPC_MESSAGE_HANDLER(NaClProcessMsg_Start, OnStartSelLdr)
      IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void NaClListener::OnStartSelLdr(std::vector<nacl::FileDescriptor> handles,
                                 const std::string& validation_cache_key,
                                 const std::string& version,
                                 bool enable_exception_handling) {
  struct NaClChromeMainArgs *args = NaClChromeMainArgsCreate();
  if (args == NULL) {
    LOG(ERROR) << "NaClChromeMainArgsCreate() failed";
    return;
  }

#if defined(OS_LINUX) || defined(OS_MACOSX)
  args->create_memory_object_func = CreateMemoryObject;
# if defined(OS_MACOSX)
  CHECK(handles.size() >= 1);
  g_shm_fd = nacl::ToNativeHandle(handles[handles.size() - 1]);
  handles.pop_back();
# endif
#endif

  CHECK(handles.size() >= 1);
  NaClHandle irt_handle = nacl::ToNativeHandle(handles[handles.size() - 1]);
  handles.pop_back();

#if defined(OS_WIN)
  args->irt_fd = _open_osfhandle(reinterpret_cast<intptr_t>(irt_handle),
                                 _O_RDONLY | _O_BINARY);
  if (args->irt_fd < 0) {
    LOG(ERROR) << "_open_osfhandle() failed";
    return;
  }
#else
  args->irt_fd = irt_handle;
#endif

  if (CheckEnvVar("NACL_VALIDATION_CACHE", false)) {
    LOG(INFO) << "NaCl validation cache enabled.";
    // The cache structure is not freed and exists until the NaCl process exits.
    args->validation_cache = CreateValidationCache(
        new BrowserValidationDBProxy(this), validation_cache_key, version);
  }

  CHECK(handles.size() == 1);
  args->imc_bootstrap_handle = nacl::ToNativeHandle(handles[0]);
  args->enable_exception_handling = enable_exception_handling;
  args->enable_debug_stub = debug_enabled_;
  NaClChromeMainStart(args);
  NOTREACHED();
}
