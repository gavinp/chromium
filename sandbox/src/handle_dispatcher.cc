// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sandbox/src/handle_dispatcher.h"

#include "base/win/scoped_handle.h"
#include "sandbox/src/handle_interception.h"
#include "sandbox/src/handle_policy.h"
#include "sandbox/src/ipc_tags.h"
#include "sandbox/src/policy_broker.h"
#include "sandbox/src/policy_params.h"
#include "sandbox/src/sandbox.h"
#include "sandbox/src/sandbox_nt_util.h"
#include "sandbox/src/sandbox_types.h"
#include "sandbox/src/sandbox_utils.h"

namespace sandbox {

HandleDispatcher::HandleDispatcher(PolicyBase* policy_base)
    : policy_base_(policy_base) {
  static const IPCCall duplicate_handle_proxy = {
    {IPC_DUPLICATEHANDLEPROXY_TAG, VOIDPTR_TYPE, ULONG_TYPE, ULONG_TYPE,
     ULONG_TYPE},
    reinterpret_cast<CallbackGeneric>(&HandleDispatcher::DuplicateHandleProxy)
  };

  ipc_calls_.push_back(duplicate_handle_proxy);
}

bool HandleDispatcher::SetupService(InterceptionManager* manager,
                                    int service) {
  // We perform no interceptions for handles right now.
  switch (service) {
    case IPC_DUPLICATEHANDLEPROXY_TAG:
    return true;
  }

  return false;
}

bool HandleDispatcher::DuplicateHandleProxy(IPCInfo* ipc,
                                            HANDLE source_handle,
                                            DWORD target_process_id,
                                            DWORD desired_access,
                                            DWORD options) {
  NTSTATUS error;
  static NtQueryObject QueryObject = NULL;
  if (!QueryObject)
    ResolveNTFunctionPtr("NtQueryObject", &QueryObject);

  // Get a copy of the handle for use in the broker process.
  base::win::ScopedHandle handle;
  if (!::DuplicateHandle(ipc->client_info->process, source_handle,
                         ::GetCurrentProcess(), handle.Receive(),
                         0, FALSE, 0)) {
    ipc->return_info.win32_result = ::GetLastError();
    return false;
  }

  // Get the object type (32 characters is safe; current max is 14).
  BYTE buffer[sizeof(OBJECT_TYPE_INFORMATION) + 32 * sizeof(wchar_t)];
  OBJECT_TYPE_INFORMATION* type_info =
      reinterpret_cast<OBJECT_TYPE_INFORMATION*>(buffer);
  ULONG size = sizeof(buffer) - sizeof(wchar_t);
  error = QueryObject(handle, ObjectTypeInformation, type_info, size, &size);
  if (!NT_SUCCESS(error)) {
    ipc->return_info.win32_result = error;
    return false;
  }
  type_info->Name.Buffer[type_info->Name.Length / sizeof(wchar_t)] = L'\0';

  CountedParameterSet<NameBased> params;
  params[NameBased::NAME] = ParamPickerMake(type_info->Name.Buffer);

  EvalResult eval = policy_base_->EvalPolicy(IPC_DUPLICATEHANDLEPROXY_TAG,
                                             params.GetBase());
  ipc->return_info.win32_result =
      HandlePolicy::DuplicateHandleProxyAction(eval, *ipc->client_info,
                                               source_handle,
                                               target_process_id,
                                               &ipc->return_info.handle,
                                               desired_access, options);
  return true;
}

}  // namespace sandbox

