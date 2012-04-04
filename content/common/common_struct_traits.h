// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//#ifndef CONTENT_COMMON_COMMON_MESSAGE_TRAITS_H_
//#define CONTENT_COMMON_COMMON_MESSAGE_TRAITS_H_
//#pragma once

#include "content/public/common/referrer.h"

#include "ipc/ipc_message_macros.h"
#include "ipc/ipc_param_traits.h"

IPC_STRUCT_TRAITS_BEGIN(content::Referrer)
  IPC_STRUCT_TRAITS_MEMBER(url)
  IPC_STRUCT_TRAITS_MEMBER(policy)
IPC_STRUCT_TRAITS_END()

//#endif  // CONTENT_COMMON_COMMON_MESSAGE_TRAITS_H_
