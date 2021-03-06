// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file implements the Windows service controlling Me2Me host processes
// running within user sessions.

#include "remoting/host/wts_session_process_launcher_win.h"

#include <windows.h>
#include <sddl.h>
#include <limits>

#include "base/logging.h"
#include "base/process_util.h"
#include "base/rand_util.h"
#include "base/string16.h"
#include "base/stringprintf.h"
#include "base/threading/thread.h"
#include "base/utf_string_conversions.h"
#include "base/win/scoped_handle.h"
#include "ipc/ipc_channel_proxy.h"
#include "ipc/ipc_message.h"
#include "ipc/ipc_message_macros.h"

#include "remoting/host/chromoting_messages.h"
#include "remoting/host/sas_injector.h"
#include "remoting/host/wts_console_monitor_win.h"

using base::win::ScopedHandle;
using base::TimeDelta;

namespace {

// The minimum and maximum delays between attempts to inject host process into
// a session.
const int kMaxLaunchDelaySeconds = 60;
const int kMinLaunchDelaySeconds = 1;

// Name of the default session desktop.
const char kDefaultDesktopName[] = "winsta0\\default";

// Match the pipe name prefix used by Chrome IPC channels.
const char kChromePipeNamePrefix[] = "\\\\.\\pipe\\chrome.";

// Generates the command line of the host process.
const char kHostProcessCommandLineFormat[] = "\"%ls\" --chromoting-ipc=%ls";

// The security descriptor of the Chromoting IPC channel. It gives full access
// to LocalSystem and denies access by anyone else.
const char kChromotingChannelSecurityDescriptor[] =
    "O:SY" "G:SY" "D:(A;;GA;;;SY)";

// Takes the process token and makes a copy of it. The returned handle will have
// |desired_access| rights.
bool CopyProcessToken(DWORD desired_access,
                      ScopedHandle* token_out) {

  HANDLE handle;
  if (!OpenProcessToken(GetCurrentProcess(),
                        TOKEN_DUPLICATE | desired_access,
                        &handle)) {
    LOG_GETLASTERROR(ERROR) << "Failed to open process token";
    return false;
  }

  ScopedHandle process_token(handle);

  if (!DuplicateTokenEx(process_token,
                        desired_access,
                        NULL,
                        SecurityImpersonation,
                        TokenPrimary,
                        &handle)) {
    LOG_GETLASTERROR(ERROR) << "Failed to duplicate the process token";
    return false;
  }

  token_out->Set(handle);
  return true;
}

// Creates a copy of the current process with SE_TCB_NAME privilege enabled.
bool CreatePrivilegedToken(ScopedHandle* token_out) {
  ScopedHandle privileged_token;
  DWORD desired_access = TOKEN_ADJUST_PRIVILEGES | TOKEN_IMPERSONATE |
                         TOKEN_DUPLICATE | TOKEN_QUERY;
  if (!CopyProcessToken(desired_access, &privileged_token)) {
    return false;
  }

  // Get the LUID for the SE_TCB_NAME privilege.
  TOKEN_PRIVILEGES state;
  state.PrivilegeCount = 1;
  state.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
  if (!LookupPrivilegeValue(NULL, SE_TCB_NAME, &state.Privileges[0].Luid)) {
    LOG_GETLASTERROR(ERROR) <<
        "Failed to lookup the LUID for the SE_TCB_NAME privilege";
    return false;
  }

  // Enable the SE_TCB_NAME privilege.
  if (!AdjustTokenPrivileges(privileged_token, FALSE, &state, 0, NULL, 0)) {
    LOG_GETLASTERROR(ERROR) <<
        "Failed to enable SE_TCB_NAME privilege in a token";
    return false;
  }

  token_out->Set(privileged_token.Take());
  return true;
}

// Creates a copy of the current process token for the given |session_id| so
// it can be used to launch a process in that session.
bool CreateSessionToken(uint32 session_id,
                        ScopedHandle* token_out) {

  ScopedHandle session_token;
  DWORD desired_access = TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID |
                         TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY;
  if (!CopyProcessToken(desired_access, &session_token)) {
    return false;
  }

  // Change the session ID of the token.
  DWORD new_session_id = session_id;
  if (!SetTokenInformation(session_token,
                           TokenSessionId,
                           &new_session_id,
                           sizeof(new_session_id))) {
    LOG_GETLASTERROR(ERROR) <<
        "Failed to change session ID of a token";
    return false;
  }

  token_out->Set(session_token.Take());
  return true;
}

// Generates random channel ID.
// N.B. Stolen from src/content/common/child_process_host_impl.cc
string16 GenerateRandomChannelId(void* instance) {
  return base::StringPrintf(ASCIIToUTF16("%d.%p.%d").c_str(),
                            base::GetCurrentProcId(), instance,
                            base::RandInt(0, std::numeric_limits<int>::max()));
}

// Creates the server end of the Chromoting IPC channel.
// N.B. This code is based on IPC::Channel's implementation.
bool CreatePipeForIpcChannel(void* instance,
                             string16* channel_name_out,
                             ScopedHandle* pipe_out) {
  // Create security descriptor for the channel.
  SECURITY_ATTRIBUTES security_attributes;
  security_attributes.nLength = sizeof(security_attributes);
  security_attributes.bInheritHandle = FALSE;

  ULONG security_descriptor_length = 0;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(
           kChromotingChannelSecurityDescriptor,
           SDDL_REVISION_1,
           reinterpret_cast<PSECURITY_DESCRIPTOR*>(
               &security_attributes.lpSecurityDescriptor),
           &security_descriptor_length)) {
    LOG_GETLASTERROR(ERROR) <<
        "Failed to create a security descriptor for the Chromoting IPC channel";
    return false;
  }

  // Generate a random channel name.
  string16 channel_name(GenerateRandomChannelId(instance));

  // Convert it to the pipe name.
  string16 pipe_name(ASCIIToUTF16(kChromePipeNamePrefix));
  pipe_name.append(channel_name);

  // Create the server end of the pipe. This code should match the code in
  // IPC::Channel with exception of passing a non-default security descriptor.
  HANDLE pipe = CreateNamedPipeW(pipe_name.c_str(),
                                 PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
                                     FILE_FLAG_FIRST_PIPE_INSTANCE,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE,
                                 1,
                                 IPC::Channel::kReadBufferSize,
                                 IPC::Channel::kReadBufferSize,
                                 5000,
                                 &security_attributes);
  if (pipe == INVALID_HANDLE_VALUE) {
    LOG_GETLASTERROR(ERROR) <<
        "Failed to create the server end of the Chromoting IPC channel";
    LocalFree(security_attributes.lpSecurityDescriptor);
    return false;
  }

  LocalFree(security_attributes.lpSecurityDescriptor);

  *channel_name_out = channel_name;
  pipe_out->Set(pipe);
  return true;
}

// Launches |binary| in the security context of the supplied |user_token|.
bool LaunchProcessAsUser(const FilePath& binary,
                         const string16& command_line,
                         HANDLE user_token,
                         base::Process* process_out) {
  string16 application_name = binary.value();
  string16 desktop = ASCIIToUTF16(kDefaultDesktopName);

  PROCESS_INFORMATION process_info;
  STARTUPINFOW startup_info;

  memset(&startup_info, 0, sizeof(startup_info));
  startup_info.cb = sizeof(startup_info);
  startup_info.lpDesktop = const_cast<LPWSTR>(desktop.c_str());

  if (!CreateProcessAsUserW(user_token,
                            application_name.c_str(),
                            const_cast<LPWSTR>(command_line.c_str()),
                            NULL,
                            NULL,
                            FALSE,
                            0,
                            NULL,
                            NULL,
                            &startup_info,
                            &process_info)) {
    LOG_GETLASTERROR(ERROR) <<
        "Failed to launch a process with a user token";
    return false;
  }

  CloseHandle(process_info.hThread);
  process_out->set_handle(process_info.hProcess);
  return true;
}

} // namespace

namespace remoting {

WtsSessionProcessLauncher::WtsSessionProcessLauncher(
    WtsConsoleMonitor* monitor,
    const FilePath& host_binary,
    base::Thread* io_thread)
    : host_binary_(host_binary),
      io_thread_(io_thread),
      monitor_(monitor),
      state_(StateDetached) {
  monitor_->AddWtsConsoleObserver(this);
}

WtsSessionProcessLauncher::~WtsSessionProcessLauncher() {
  DCHECK(state_ == StateDetached);
  DCHECK(!timer_.IsRunning());
  DCHECK(process_.handle() == NULL);
  DCHECK(process_watcher_.GetWatchedObject() == NULL);
  DCHECK(chromoting_channel_.get() == NULL);

  monitor_->RemoveWtsConsoleObserver(this);
}

void WtsSessionProcessLauncher::LaunchProcess() {
  DCHECK(state_ == StateStarting);
  DCHECK(!timer_.IsRunning());
  DCHECK(process_.handle() == NULL);
  DCHECK(process_watcher_.GetWatchedObject() == NULL);
  DCHECK(chromoting_channel_.get() == NULL);

  launch_time_ = base::Time::Now();

  string16 channel_name;
  ScopedHandle pipe;
  if (CreatePipeForIpcChannel(this, &channel_name, &pipe)) {
    // Wrap the pipe into an IPC channel.
    chromoting_channel_.reset(new IPC::ChannelProxy(
        IPC::ChannelHandle(pipe.Get()),
        IPC::Channel::MODE_SERVER,
        this,
        io_thread_->message_loop_proxy().get()));

    string16 command_line =
        base::StringPrintf(ASCIIToUTF16(kHostProcessCommandLineFormat).c_str(),
                           host_binary_.value().c_str(),
                           channel_name.c_str());

    // Try to launch the process and attach an object watcher to the returned
    // handle so that we get notified when the process terminates.
    if (LaunchProcessAsUser(host_binary_, command_line, session_token_,
                            &process_)) {
      if (process_watcher_.StartWatching(process_.handle(), this)) {
        state_ = StateAttached;
        return;
      } else {
        LOG(ERROR) << "Failed to arm the process watcher.";
        process_.Terminate(0);
        process_.Close();
      }
    }

    chromoting_channel_.reset();
  }

  // Something went wrong. Try to launch the host again later. The attempts rate
  // is limited by exponential backoff.
  launch_backoff_ = std::max(launch_backoff_ * 2,
                             TimeDelta::FromSeconds(kMinLaunchDelaySeconds));
  launch_backoff_ = std::min(launch_backoff_,
                             TimeDelta::FromSeconds(kMaxLaunchDelaySeconds));
  timer_.Start(FROM_HERE, launch_backoff_,
               this, &WtsSessionProcessLauncher::LaunchProcess);
}

void WtsSessionProcessLauncher::OnObjectSignaled(HANDLE object) {
  DCHECK(state_ == StateAttached);
  DCHECK(!timer_.IsRunning());
  DCHECK(process_.handle() != NULL);
  DCHECK(process_watcher_.GetWatchedObject() == NULL);
  DCHECK(chromoting_channel_.get() != NULL);

  // The host process has been terminated for some reason. The handle can now be
  // closed.
  process_.Close();
  chromoting_channel_.reset();

  // Expand the backoff interval if the process has died quickly or reset it if
  // it was up longer than the maximum backoff delay.
  base::TimeDelta delta = base::Time::Now() - launch_time_;
  if (delta < base::TimeDelta() ||
      delta >= base::TimeDelta::FromSeconds(kMaxLaunchDelaySeconds)) {
    launch_backoff_ = base::TimeDelta();
  } else {
    launch_backoff_ = std::max(launch_backoff_ * 2,
                               TimeDelta::FromSeconds(kMinLaunchDelaySeconds));
    launch_backoff_ = std::min(launch_backoff_,
                               TimeDelta::FromSeconds(kMaxLaunchDelaySeconds));
  }

  // Try to restart the host.
  state_ = StateStarting;
  timer_.Start(FROM_HERE, launch_backoff_,
               this, &WtsSessionProcessLauncher::LaunchProcess);
}

bool WtsSessionProcessLauncher::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(WtsSessionProcessLauncher, message)
      IPC_MESSAGE_HANDLER(ChromotingHostMsg_SendSasToConsole,
                          OnSendSasToConsole)
      IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void WtsSessionProcessLauncher::OnSendSasToConsole() {
  if (state_ == StateAttached) {
    if (sas_injector_.get() == NULL) {
      sas_injector_ = SasInjector::Create();
    }

    if (sas_injector_.get() != NULL) {
      sas_injector_->InjectSas();
    }
  }
}

void WtsSessionProcessLauncher::OnSessionAttached(uint32 session_id) {
  DCHECK(state_ == StateDetached);
  DCHECK(!timer_.IsRunning());
  DCHECK(process_.handle() == NULL);
  DCHECK(process_watcher_.GetWatchedObject() == NULL);
  DCHECK(chromoting_channel_.get() == NULL);

  // Temporarily enable the SE_TCB_NAME privilege. The privileged token is
  // created as needed and kept for later reuse.
  if (privileged_token_.Get() == NULL) {
    if (!CreatePrivilegedToken(&privileged_token_)) {
      return;
    }
  }

  if (!ImpersonateLoggedOnUser(privileged_token_)) {
    LOG_GETLASTERROR(ERROR) <<
        "Failed to impersonate the privileged token";
    return;
  }

  // While the SE_TCB_NAME privilege is enabled, create a session token for
  // the launched process.
  bool result = CreateSessionToken(session_id, &session_token_);

  // Revert to the default token. The default token is sufficient to call
  // CreateProcessAsUser() successfully.
  CHECK(RevertToSelf());

  if (!result)
    return;

  // Now try to launch the host.
  state_ = StateStarting;
  LaunchProcess();
}

void WtsSessionProcessLauncher::OnSessionDetached() {
  DCHECK(state_ == StateDetached ||
         state_ == StateStarting ||
         state_ == StateAttached);

  switch (state_) {
    case StateDetached:
      DCHECK(!timer_.IsRunning());
      DCHECK(process_.handle() == NULL);
      DCHECK(process_watcher_.GetWatchedObject() == NULL);
      DCHECK(chromoting_channel_.get() == NULL);
      break;

    case StateStarting:
      DCHECK(timer_.IsRunning());
      DCHECK(process_.handle() == NULL);
      DCHECK(process_watcher_.GetWatchedObject() == NULL);
      DCHECK(chromoting_channel_.get() == NULL);

      timer_.Stop();
      launch_backoff_ = base::TimeDelta();
      state_ = StateDetached;
      break;

    case StateAttached:
      DCHECK(!timer_.IsRunning());
      DCHECK(process_.handle() != NULL);
      DCHECK(process_watcher_.GetWatchedObject() != NULL);
      DCHECK(chromoting_channel_.get() != NULL);

      process_watcher_.StopWatching();
      process_.Terminate(0);
      process_.Close();
      chromoting_channel_.reset();
      state_ = StateDetached;
      break;
  }
}

} // namespace remoting
