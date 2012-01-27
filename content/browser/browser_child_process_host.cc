// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/browser_child_process_host.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/file_path.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/path_service.h"
#include "base/process_util.h"
#include "base/stl_util.h"
#include "base/string_util.h"
#include "content/browser/profiler_message_filter.h"
#include "content/browser/renderer_host/resource_message_filter.h"
#include "content/browser/trace_message_filter.h"
#include "content/common/child_process_host_impl.h"
#include "content/common/plugin_messages.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/child_process_data.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/result_codes.h"

#if defined(OS_WIN)
#include "base/synchronization/waitable_event.h"
#else
#include "base/bind.h"
#endif

using content::BrowserThread;
using content::ChildProcessHost;
using content::ChildProcessHostImpl;

namespace {

typedef std::list<BrowserChildProcessHost*> ChildProcessList;
static base::LazyInstance<ChildProcessList> g_child_process_list =
    LAZY_INSTANCE_INITIALIZER;

// Helper functions since the child process related notifications happen on the
// UI thread.
void ChildNotificationHelper(int notification_type,
                             content::ChildProcessData data) {
  content::NotificationService::current()->
        Notify(notification_type, content::NotificationService::AllSources(),
               content::Details<content::ChildProcessData>(&data));
}

}  // namespace

BrowserChildProcessHost::BrowserChildProcessHost(
    content::ProcessType type)
    : ALLOW_THIS_IN_INITIALIZER_LIST(client_(this)),
#if !defined(OS_WIN)
      ALLOW_THIS_IN_INITIALIZER_LIST(task_factory_(this)),
#endif
      disconnect_was_alive_(false) {
  data_.type = type;
  data_.id = ChildProcessHostImpl::GenerateChildProcessUniqueId();

  child_process_host_.reset(ChildProcessHost::Create(this));
  child_process_host_->AddFilter(new TraceMessageFilter);
  child_process_host_->AddFilter(new ProfilerMessageFilter);

  g_child_process_list.Get().push_back(this);
}

BrowserChildProcessHost::~BrowserChildProcessHost() {
  g_child_process_list.Get().remove(this);
}

// static
void BrowserChildProcessHost::TerminateAll() {
  // Make a copy since the ChildProcessHost dtor mutates the original list.
  ChildProcessList copy = g_child_process_list.Get();
  STLDeleteElements(&copy);
}

void BrowserChildProcessHost::Launch(
#if defined(OS_WIN)
    const FilePath& exposed_dir,
#elif defined(OS_POSIX)
    bool use_zygote,
    const base::environment_vector& environ,
#endif
    CommandLine* cmd_line) {

  content::GetContentClient()->browser()->AppendExtraCommandLineSwitches(
      cmd_line, id());

  child_process_.reset(new ChildProcessLauncher(
#if defined(OS_WIN)
      exposed_dir,
#elif defined(OS_POSIX)
      use_zygote,
      environ,
      child_process_host()->TakeClientFileDescriptor(),
#endif
      cmd_line,
      &client_));
}

base::ProcessHandle BrowserChildProcessHost::GetChildProcessHandle() const {
  DCHECK(child_process_.get())
      << "Requesting a child process handle before launching.";
  DCHECK(child_process_->GetHandle())
      << "Requesting a child process handle before launch has completed OK.";
  return child_process_->GetHandle();
}

void BrowserChildProcessHost::ForceShutdown() {
  g_child_process_list.Get().remove(this);
  child_process_host_->ForceShutdown();
}

void BrowserChildProcessHost::SetTerminateChildOnShutdown(
  bool terminate_on_shutdown) {
  child_process_->SetTerminateChildOnShutdown(terminate_on_shutdown);
}

void BrowserChildProcessHost::Notify(int type) {
  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(&ChildNotificationHelper, type, data_));
}

base::TerminationStatus BrowserChildProcessHost::GetChildTerminationStatus(
    int* exit_code) {
  if (!child_process_.get())  // If the delegate doesn't use Launch() helper.
    return base::GetTerminationStatus(handle(), exit_code);
  return child_process_->GetChildTerminationStatus(exit_code);
}

bool BrowserChildProcessHost::OnMessageReceived(const IPC::Message& message) {
  return false;
}

void BrowserChildProcessHost::OnChannelConnected(int32 peer_pid) {
  Notify(content::NOTIFICATION_CHILD_PROCESS_HOST_CONNECTED);
}

bool BrowserChildProcessHost::CanShutdown() {
  return true;
}

// Normally a ChildProcessHostDelegate deletes itself from this callback, but at
// this layer and below we need to have the final child process exit code to
// properly bucket crashes vs kills. On Windows we can do this if we wait until
// the process handle is signaled; on the rest of the platforms, we schedule a
// delayed task to wait for an exit code. However, this means that this method
// may be called twice: once from the actual channel error and once from
// OnWaitableEventSignaled() or the delayed task.
void BrowserChildProcessHost::OnChildDisconnected() {
  DCHECK(handle() != base::kNullProcessHandle);
  int exit_code;
  base::TerminationStatus status = GetChildTerminationStatus(&exit_code);
  switch (status) {
    case base::TERMINATION_STATUS_PROCESS_CRASHED:
    case base::TERMINATION_STATUS_ABNORMAL_TERMINATION: {
      OnProcessCrashed(exit_code);
      // Report that this child process crashed.
      Notify(content::NOTIFICATION_CHILD_PROCESS_CRASHED);
      UMA_HISTOGRAM_ENUMERATION("ChildProcess.Crashed",
                                this->type(),
                                content::PROCESS_TYPE_MAX);
      if (disconnect_was_alive_) {
        UMA_HISTOGRAM_ENUMERATION("ChildProcess.CrashedWasAlive",
                                  this->type(),
                                  content::PROCESS_TYPE_MAX);
      }
      break;
    }
    case base::TERMINATION_STATUS_PROCESS_WAS_KILLED: {
      OnProcessCrashed(exit_code);
      // Report that this child process was killed.
      UMA_HISTOGRAM_ENUMERATION("ChildProcess.Killed",
                                this->type(),
                                content::PROCESS_TYPE_MAX);
      if (disconnect_was_alive_) {
        UMA_HISTOGRAM_ENUMERATION("ChildProcess.KilledWasAlive",
                                  this->type(),
                                  content::PROCESS_TYPE_MAX);
      }
      break;
    }
    case base::TERMINATION_STATUS_STILL_RUNNING: {
      // Exit code not yet available. Ensure we don't wait forever for an exit
      // code.
      if (disconnect_was_alive_) {
        UMA_HISTOGRAM_ENUMERATION("ChildProcess.DisconnectedAlive",
                                  this->type(),
                                  content::PROCESS_TYPE_MAX);
        break;
      }
      disconnect_was_alive_ = true;
#if defined(OS_WIN)
      child_watcher_.StartWatching(new base::WaitableEvent(handle()), this);
#else
      // On non-Windows platforms, give the child process some time to die after
      // disconnecting the channel so that the exit code and termination status
      // become available. This is best effort -- if the process doesn't die
      // within the time limit, this object gets destroyed.
      const int kExitCodeWaitMs = 250;
      MessageLoop::current()->PostDelayedTask(
          FROM_HERE,
          base::Bind(&BrowserChildProcessHost::OnChildDisconnected,
                     task_factory_.GetWeakPtr()),
          kExitCodeWaitMs);
#endif
      return;
    }

    default:
      break;
  }
  UMA_HISTOGRAM_ENUMERATION("ChildProcess.Disconnected",
                            this->type(),
                            content::PROCESS_TYPE_MAX);
  // Notify in the main loop of the disconnection.
  Notify(content::NOTIFICATION_CHILD_PROCESS_HOST_DISCONNECTED);
  delete this;
}

// The child process handle has been signaled so the exit code is finally
// available. Unfortunately STILL_ACTIVE (0x103) is a valid exit code in
// which case we should not call OnChildDisconnected() or else we will be
// waiting forever.
void BrowserChildProcessHost::OnWaitableEventSignaled(
    base::WaitableEvent* waitable_event) {
#if defined (OS_WIN)
  unsigned long exit_code = 0;
  GetExitCodeProcess(waitable_event->Release(), &exit_code);
  delete waitable_event;
  if (exit_code == STILL_ACTIVE) {
    delete this;
  } else {
    BrowserChildProcessHost::OnChildDisconnected();
  }
#endif
}

bool BrowserChildProcessHost::Send(IPC::Message* message) {
  return child_process_host_->Send(message);
}

void BrowserChildProcessHost::ShutdownStarted() {
  // Must remove the process from the list now, in case it gets used for a
  // new instance before our watcher tells us that the process terminated.
  g_child_process_list.Get().remove(this);
}

BrowserChildProcessHost::ClientHook::ClientHook(BrowserChildProcessHost* host)
    : host_(host) {
}

void BrowserChildProcessHost::ClientHook::OnProcessLaunched() {
  if (!host_->child_process_->GetHandle()) {
    delete host_;
    return;
  }
  host_->data_.handle = host_->child_process_->GetHandle();
  host_->OnProcessLaunched();
}

BrowserChildProcessHost::Iterator::Iterator()
    : all_(true), type_(content::PROCESS_TYPE_UNKNOWN) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO)) <<
          "BrowserChildProcessHost::Iterator must be used on the IO thread.";
  iterator_ = g_child_process_list.Get().begin();
}

BrowserChildProcessHost::Iterator::Iterator(content::ProcessType type)
    : all_(false), type_(type) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO)) <<
          "BrowserChildProcessHost::Iterator must be used on the IO thread.";
  iterator_ = g_child_process_list.Get().begin();
  if (!Done() && (*iterator_)->type() != type_)
    ++(*this);
}

BrowserChildProcessHost* BrowserChildProcessHost::Iterator::operator++() {
  do {
    ++iterator_;
    if (Done())
      break;

    if (!all_ && (*iterator_)->type() != type_)
      continue;

    return *iterator_;
  } while (true);

  return NULL;
}

bool BrowserChildProcessHost::Iterator::Done() {
  return iterator_ == g_child_process_list.Get().end();
}
