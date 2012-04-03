// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/plugin/daemon_controller.h"

#include <launch.h>
#include <sys/types.h>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/mac/authorization_util.h"
#include "base/mac/launchd.h"
#include "base/mac/mac_logging.h"
#include "base/mac/scoped_authorizationref.h"
#include "base/mac/scoped_launch_data.h"
#include "base/threading/thread.h"
#include "base/values.h"
#include "remoting/host/json_host_config.h"

namespace remoting {

namespace {

// The name of the Remoting Host service that is registered with launchd.
#define kServiceName "org.chromium.chromoting"
#define kConfigDir "/Library/PrivilegedHelperTools/"

// This helper script is executed as root.  It is passed a command-line option
// (--enable or --disable), which causes it to create or remove a trigger file.
// The trigger file (defined in the service's plist file) informs launchd
// whether the Host service should be running.  Creating the trigger file causes
// launchd to immediately start the service.  Deleting the trigger file has no
// immediate effect, but it prevents the service from being restarted if it
// becomes stopped.
const char kStartStopTool[] = kConfigDir kServiceName ".me2me.sh";

// Use a single configuration file, instead of separate "auth" and "host" files.
// This is because the SetConfigAndStart() API only provides a single
// dictionary, and splitting this into two dictionaries would require
// knowledge of which keys belong in which files.
const char kHostConfigFile[] = kConfigDir kServiceName ".json";

class DaemonControllerMac : public remoting::DaemonController {
 public:
  DaemonControllerMac();
  virtual ~DaemonControllerMac();

  virtual State GetState() OVERRIDE;
  virtual void GetConfig(const GetConfigCallback& callback) OVERRIDE;
  virtual void SetConfigAndStart(
      scoped_ptr<base::DictionaryValue> config,
      const CompletionCallback& done_callback) OVERRIDE;
  virtual void SetPin(const std::string& pin,
                      const CompletionCallback& done_callback) OVERRIDE;
  virtual void Stop(const CompletionCallback& done_callback) OVERRIDE;

 private:
  void DoGetConfig(const GetConfigCallback& callback);
  void DoSetConfigAndStart(scoped_ptr<base::DictionaryValue> config,
                           const CompletionCallback& done_callback);
  void DoStop(const CompletionCallback& done_callback);

  bool RunToolScriptAsRoot(const char* command);
  bool StopService();

  // The API for gaining root privileges is blocking (it prompts the user for
  // a password). Since Start() and Stop() must not block the main thread, they
  // need to post their tasks to a separate thread.
  base::Thread auth_thread_;

  DISALLOW_COPY_AND_ASSIGN(DaemonControllerMac);
};

DaemonControllerMac::DaemonControllerMac()
    : auth_thread_("Auth thread") {
  auth_thread_.Start();
}

DaemonControllerMac::~DaemonControllerMac() {
  // This will block if the thread is waiting on a root password prompt.  There
  // doesn't seem to be an easy solution for this, other than to spawn a
  // separate process to do the root elevation.

  // TODO(lambroslambrou): Improve this, either by finding a way to terminate
  // the thread, or by moving to a separate process.
  auth_thread_.Stop();
}

DaemonController::State DaemonControllerMac::GetState() {
  pid_t job_pid = base::mac::PIDForJob(kServiceName);
  if (job_pid < 0) {
    // TODO(lambroslambrou): Change this to STATE_NOT_INSTALLED when the
    // installation process is implemented.
    return DaemonController::STATE_NOT_IMPLEMENTED;
  } else if (job_pid == 0) {
    // Service is stopped, or a start attempt failed.
    return DaemonController::STATE_STOPPED;
  } else {
    return DaemonController::STATE_STARTED;
  }
}

void DaemonControllerMac::GetConfig(const GetConfigCallback& callback) {
  // base::Unretained() is safe, since this object owns the thread and therefore
  // outlives it.
  auth_thread_.message_loop_proxy()->PostTask(
      FROM_HERE,
      base::Bind(&DaemonControllerMac::DoGetConfig, base::Unretained(this),
                 callback));
}

void DaemonControllerMac::SetConfigAndStart(
    scoped_ptr<base::DictionaryValue> config,
    const CompletionCallback& done_callback) {
  auth_thread_.message_loop_proxy()->PostTask(
      FROM_HERE, base::Bind(
          &DaemonControllerMac::DoSetConfigAndStart, base::Unretained(this),
          base::Passed(&config), done_callback));
}

void DaemonControllerMac::SetPin(const std::string& pin,
                                 const CompletionCallback& done_callback) {
  NOTIMPLEMENTED();
  done_callback.Run(RESULT_FAILED);
}

void DaemonControllerMac::Stop(const CompletionCallback& done_callback) {
  auth_thread_.message_loop_proxy()->PostTask(
      FROM_HERE, base::Bind(
          &DaemonControllerMac::DoStop, base::Unretained(this), done_callback));
}

void DaemonControllerMac::DoGetConfig(const GetConfigCallback& callback) {
  JsonHostConfig host_config(FilePath(kHostConfigFile),
                             base::MessageLoopProxy::current());
  host_config.Read();

  scoped_ptr<base::DictionaryValue> config(new base::DictionaryValue());

  const char* key = "host_id";
  std::string value;
  if (host_config.GetString(key, &value))
    config.get()->SetString(key, value);
  key = "xmpp_login";
  if (host_config.GetString(key, &value))
    config.get()->SetString(key, value);

  callback.Run(config.Pass());
}

void DaemonControllerMac::DoSetConfigAndStart(
    scoped_ptr<base::DictionaryValue> config,
    const CompletionCallback& done_callback) {
  // JsonHostConfig doesn't provide a way to save on the current thread, wait
  // for completion, and know whether the save succeeded.  Instead, use
  // base::JSONWriter directly.

  // TODO(lambroslambrou): Improve the JsonHostConfig interface.
  std::string file_content;
  base::JSONWriter::Write(config.get(), &file_content);
  if (file_util::WriteFile(FilePath(kHostConfigFile), file_content.c_str(),
                           file_content.size()) !=
      static_cast<int>(file_content.size())) {
    LOG(ERROR) << "Failed to write config file: " << kHostConfigFile;
    done_callback.Run(RESULT_FAILED);
    return;
  }

  // Creating the trigger file causes launchd to start the service, so the
  // extra step performed in DoStop() is not necessary here.
  bool result = RunToolScriptAsRoot("--enable");
  done_callback.Run(result ? RESULT_OK : RESULT_FAILED);
}

void DaemonControllerMac::DoStop(const CompletionCallback& done_callback) {
  if (!RunToolScriptAsRoot("--disable")) {
    done_callback.Run(RESULT_FAILED);
    return;
  }

  // Deleting the trigger file does not cause launchd to stop the service.
  // Since the service is running for the local user's desktop (not as root),
  // it has to be stopped for that user.  This cannot easily be done in the
  // shell-script running as root, so it is done here instead.
  bool result = StopService();
  done_callback.Run(result ? RESULT_OK : RESULT_FAILED);
}

bool DaemonControllerMac::RunToolScriptAsRoot(const char* command) {
  // TODO(lambroslambrou): Supply a localized prompt string here.
  base::mac::ScopedAuthorizationRef authorization(
      base::mac::AuthorizationCreateToRunAsRoot(CFSTR("")));
  if (!authorization) {
    LOG(ERROR) << "Failed to get root privileges.";
    return false;
  }

  if (!file_util::VerifyPathControlledByAdmin(FilePath(kStartStopTool))) {
    LOG(ERROR) << "Security check failed for: " << kStartStopTool;
    return false;
  }

  // TODO(lambroslambrou): Use sandbox-exec to minimize exposure -
  // http://crbug.com/120903
  const char* arguments[] = { command, NULL };
  int exit_status;
  OSStatus status = base::mac::ExecuteWithPrivilegesAndWait(
      authorization.get(),
      kStartStopTool,
      kAuthorizationFlagDefaults,
      arguments,
      NULL,
      &exit_status);
  if (status != errAuthorizationSuccess) {
    OSSTATUS_LOG(ERROR, status) << "AuthorizationExecuteWithPrivileges";
    return false;
  }
  if (exit_status != 0) {
    LOG(ERROR) << kStartStopTool << " failed with exit status " << exit_status;
    return false;
  }

  return true;
}

bool DaemonControllerMac::StopService() {
  base::mac::ScopedLaunchData response(
      base::mac::MessageForJob(kServiceName, LAUNCH_KEY_STOPJOB));
  if (!response) {
    LOG(ERROR) << "Failed to send message to launchd";
    return false;
  }

  // Got a response, so check if launchd sent a non-zero error code, otherwise
  // assume the command was successful.
  if (launch_data_get_type(response.get()) == LAUNCH_DATA_ERRNO) {
    int error = launch_data_get_errno(response.get());
    if (error) {
      LOG(ERROR) << "launchd returned error " << error;
      return false;
    }
  }
  return true;
}

}  // namespace

scoped_ptr<DaemonController> remoting::DaemonController::Create() {
  return scoped_ptr<DaemonController>(new DaemonControllerMac());
}

}  // namespace remoting
