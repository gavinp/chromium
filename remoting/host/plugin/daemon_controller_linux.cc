// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/plugin/daemon_controller.h"

#include <unistd.h>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/environment.h"
#include "base/file_path.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/process_util.h"
#include "base/string_split.h"
#include "base/threading/thread.h"
#include "base/values.h"

namespace remoting {

namespace {

const char* kDaemonScript = "me2me_virtual_host.py";
const int64 kDaemonTimeoutMs = 5000;

// TODO(sergeyu): This is a very hacky implementation of
// DaemonController interface for linux. Current version works, but
// there are sevaral problems with it:
//   * All calls are executed synchronously, even though this API is
//     supposed to be asynchronous.
//   * The host is configured by passing configuration data as CL
//     argument - this is obviously not secure.
// Rewrite this code to solve these two problems.
// http://crbug.com/120950 .
class DaemonControllerLinux : public remoting::DaemonController {
 public:
  DaemonControllerLinux();

  virtual State GetState() OVERRIDE;
  virtual void GetConfig(const GetConfigCallback& callback) OVERRIDE;
  virtual void SetConfigAndStart(
      scoped_ptr<base::DictionaryValue> config,
      const CompletionCallback& done_callback) OVERRIDE;
  virtual void SetPin(const std::string& pin,
                      const CompletionCallback& done_callback) OVERRIDE;
  virtual void Stop(const CompletionCallback& done_callback) OVERRIDE;

 private:
  void DoSetConfigAndStart(scoped_ptr<base::DictionaryValue> config,
                           const CompletionCallback& done_callback);
  void DoSetPin(const std::string& pin,
                const CompletionCallback& done_callback);
  void DoStop(const CompletionCallback& done_callback);

  base::Thread file_io_thread_;

  DISALLOW_COPY_AND_ASSIGN(DaemonControllerLinux);
};

DaemonControllerLinux::DaemonControllerLinux()
    : file_io_thread_("DaemonControllerFileIO") {
  file_io_thread_.Start();
}

// TODO(jamiewalch): We'll probably be able to do a better job of
// detecting whether or not the daemon is installed once we have a
// proper installer. For now, detecting whether or not the binary
// is on the PATH is good enough.
static bool GetScriptPath(FilePath* result) {
  base::Environment* environment = base::Environment::Create();
  std::string path;
  if (environment->GetVar("PATH", &path)) {
    std::vector<std::string> path_directories;
    base::SplitString(path, ':', &path_directories);
    for (unsigned int i = 0; i < path_directories.size(); ++i) {
      FilePath candidate_exe(path_directories[i]);
      candidate_exe = candidate_exe.Append(kDaemonScript);
      if (access(candidate_exe.value().c_str(), X_OK) == 0) {
        *result = candidate_exe;
        return true;
      }
    }
  }
  return false;
}

static bool RunScript(const std::vector<std::string>& args, int* exit_code) {
  // As long as we're relying on running an external binary from the
  // PATH, don't do it as root.
  if (getuid() == 0) {
    return false;
  }
  FilePath script_path;
  if (!GetScriptPath(&script_path)) {
    return false;
  }
  CommandLine command_line(script_path);
  for (unsigned int i = 0; i < args.size(); ++i) {
    command_line.AppendArg(args[i]);
  }
  base::ProcessHandle process_handle;
  bool result = base::LaunchProcess(command_line,
                                    base::LaunchOptions(),
                                    &process_handle);
  if (result) {
    if (exit_code) {
      result = base::WaitForExitCodeWithTimeout(process_handle,
                                                exit_code,
                                                kDaemonTimeoutMs);
    }
    base::CloseProcessHandle(process_handle);
  }
  return result;
}

remoting::DaemonController::State DaemonControllerLinux::GetState() {
  std::vector<std::string> args;
  args.push_back("--check-running");
  int exit_code = 0;
  if (!RunScript(args, &exit_code)) {
    // TODO(jamiewalch): When we have a good story for installing, return
    // NOT_INSTALLED rather than NOT_IMPLEMENTED (the former suppresses
    // the relevant UI in the web-app).
    return remoting::DaemonController::STATE_NOT_IMPLEMENTED;
  } else if (exit_code == 0) {
    return remoting::DaemonController::STATE_STARTED;
  } else {
    return remoting::DaemonController::STATE_STOPPED;
  }
}

void DaemonControllerLinux::GetConfig(const GetConfigCallback& callback) {
  NOTIMPLEMENTED();
}

void DaemonControllerLinux::SetConfigAndStart(
    scoped_ptr<base::DictionaryValue> config,
    const CompletionCallback& done_callback) {
  // base::Unretained() is safe because we control lifetime of the thread.
  file_io_thread_.message_loop()->PostTask(FROM_HERE, base::Bind(
      &DaemonControllerLinux::DoSetConfigAndStart, base::Unretained(this),
      base::Passed(&config), done_callback));
}

void DaemonControllerLinux::SetPin(const std::string& pin,
                                   const CompletionCallback& done_callback) {
  file_io_thread_.message_loop()->PostTask(FROM_HERE, base::Bind(
      &DaemonControllerLinux::DoSetPin, base::Unretained(this),
      pin, done_callback));
}

void DaemonControllerLinux::Stop(const CompletionCallback& done_callback) {
  file_io_thread_.message_loop()->PostTask(FROM_HERE, base::Bind(
      &DaemonControllerLinux::DoStop, base::Unretained(this),
      done_callback));
}

void DaemonControllerLinux::DoSetConfigAndStart(
    scoped_ptr<base::DictionaryValue> config,
    const CompletionCallback& done_callback) {
  std::vector<std::string> args;
  args.push_back("--explicit-config");
  std::string config_json;
  base::JSONWriter::Write(config.get(), &config_json);
  args.push_back(config_json);
  std::vector<std::string> no_args;
  int exit_code = 0;
  AsyncResult result;
  if (RunScript(args, &exit_code)) {
    result = (exit_code == 0) ? RESULT_OK : RESULT_FAILED;
  } else {
    result = RESULT_FAILED;
  }
  done_callback.Run(result);
}

void DaemonControllerLinux::DoSetPin(const std::string& pin,
                                     const CompletionCallback& done_callback) {
  std::vector<std::string> args;
  args.push_back("--explicit-pin");
  args.push_back(pin);
  int exit_code = 0;
  AsyncResult result;
  if (RunScript(args, &exit_code)) {
    result = (exit_code == 0) ? RESULT_OK : RESULT_FAILED;
  } else {
    result = RESULT_FAILED;
  }
  done_callback.Run(result);
}

void DaemonControllerLinux::DoStop(const CompletionCallback& done_callback) {
  std::vector<std::string> args;
  args.push_back("--stop");
  int exit_code = 0;
  AsyncResult result;
  if (RunScript(args, &exit_code)) {
    result = (exit_code == 0) ? RESULT_OK : RESULT_FAILED;
  } else {
    result = RESULT_FAILED;
  }
  done_callback.Run(result);
}

}  // namespace

scoped_ptr<DaemonController> remoting::DaemonController::Create() {
  return scoped_ptr<DaemonController>(new DaemonControllerLinux());
}

}  // namespace remoting
