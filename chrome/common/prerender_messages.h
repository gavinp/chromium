// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Multiply-included message file, no traditional include guard.
#include "content/public/common/referrer.h"
#include "googleurl/src/gurl.h"
#include "ipc/ipc_message.h"
#include "ipc/ipc_message_macros.h"
#include "ipc/ipc_param_traits.h"
#include "ui/gfx/size.h"

#define IPC_MESSAGE_START PrerenderMsgStart

IPC_ENUM_TRAITS(WebKit::WebReferrerPolicy)

IPC_STRUCT_TRAITS_BEGIN(content::Referrer)
  IPC_STRUCT_TRAITS_MEMBER(url)
  IPC_STRUCT_TRAITS_MEMBER(policy)
IPC_STRUCT_TRAITS_END()

// Prerender Link Manager Messages
// These are messages sent from the renderer to the browser in
// relation to <link rel=prerender> elements.

// Notifies of the insertion of a <link rel=prerender> element in the
// document.
IPC_MESSAGE_CONTROL5(PrerenderMsg_AddPrerender,
                     int /* id, assigned by WebKit */,
                     GURL /* url */,
                     content::Referrer,
                     gfx::Size,
                     int /* render_view_route_id of launcher */)

// Notifies on removal of a <link rel=prerender> element from the document.
IPC_MESSAGE_CONTROL1(PrerenderMsg_CancelPrerender,
                     int /* id, assigned by WebKit */)

// Notifies on unloading a <link rel=prerender> element from a frame.
IPC_MESSAGE_CONTROL1(PrerenderMsg_AbandonPrerender,
                     int /* id, assigned by WebKit */)

// Prerender View Host Messages
// These are messages sent in relation to running prerenders.

// Tells a renderer if it's currently being prerendered.  Must only be set
// to true before any navigation occurs, and only set to false at most once
// after that.
IPC_MESSAGE_ROUTED1(PrerenderMsg_SetIsPrerendering,
                    bool /* whether the RenderView is prerendering */)

// Specifies that a URL is currently being prerendered.
IPC_MESSAGE_CONTROL1(PrerenderMsg_AddPrerenderURL,
                     GURL /* url */)

// Specifies that a URL is no longer being prerendered.
IPC_MESSAGE_CONTROL1(PrerenderMsg_RemovePrerenderURL,
                     GURL /* url */)
