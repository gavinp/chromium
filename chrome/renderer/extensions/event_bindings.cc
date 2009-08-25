// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/extensions/event_bindings.h"

#include "base/basictypes.h"
#include "base/singleton.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/url_constants.h"
#include "chrome/renderer/extensions/bindings_utils.h"
#include "chrome/renderer/extensions/event_bindings.h"
#include "chrome/renderer/extensions/js_only_v8_extensions.h"
#include "chrome/renderer/render_thread.h"
#include "chrome/renderer/render_view.h"
#include "grit/renderer_resources.h"
#include "webkit/api/public/WebDataSource.h"
#include "webkit/api/public/WebFrame.h"
#include "webkit/api/public/WebURLRequest.h"

using bindings_utils::CallFunctionInContext;
using bindings_utils::ContextInfo;
using bindings_utils::ContextList;
using bindings_utils::GetContexts;
using bindings_utils::GetStringResource;
using bindings_utils::ExtensionBase;
using bindings_utils::GetPendingRequestMap;
using bindings_utils::PendingRequestMap;
using WebKit::WebFrame;

namespace {

// Keep a local cache of RenderThread so that we can mock it out for unit tests.
static RenderThreadBase* render_thread = NULL;
static bool in_unit_tests = false;

// Set to true if these bindings are registered.  Will be false when extensions
// are disabled.
static bool bindings_registered = false;

struct ExtensionData {
  std::map<std::string, int> listener_count;
};
int EventIncrementListenerCount(const std::string& event_name) {
  ExtensionData *data = Singleton<ExtensionData>::get();
  return ++(data->listener_count[event_name]);
}
int EventDecrementListenerCount(const std::string& event_name) {
  ExtensionData *data = Singleton<ExtensionData>::get();
  return --(data->listener_count[event_name]);
}

class ExtensionImpl : public ExtensionBase {
 public:
  ExtensionImpl()
      : ExtensionBase(EventBindings::kName,
                      GetStringResource<IDR_EVENT_BINDINGS_JS>(),
                      0, NULL) {
  }
  ~ExtensionImpl() {}

  virtual v8::Handle<v8::FunctionTemplate> GetNativeFunction(
      v8::Handle<v8::String> name) {
    if (name->Equals(v8::String::New("AttachEvent"))) {
      return v8::FunctionTemplate::New(AttachEvent);
    } else if (name->Equals(v8::String::New("DetachEvent"))) {
      return v8::FunctionTemplate::New(DetachEvent);
    }
    return ExtensionBase::GetNativeFunction(name);
  }

  // Attach an event name to an object.
  static v8::Handle<v8::Value> AttachEvent(const v8::Arguments& args) {
    DCHECK(args.Length() == 1);
    // TODO(erikkay) should enforce that event name is a string in the bindings
    DCHECK(args[0]->IsString() || args[0]->IsUndefined());

    if (args[0]->IsString()) {
      std::string event_name(*v8::String::AsciiValue(args[0]));
      bool has_permission =
          ExtensionProcessBindings::CurrentContextHasPermission(event_name);
#if EXTENSION_TIME_TO_BREAK_API
      bool allow_api = has_permission;
#else
      bool allow_api = true;
#endif

      // Increment the count even if the caller doesn't have permission, so that
      // refcounts stay balanced.
      if (EventIncrementListenerCount(event_name) == 1 && allow_api) {
        EventBindings::GetRenderThread()->Send(
            new ViewHostMsg_ExtensionAddListener(event_name));
      }

      if (!has_permission) {
        return ExtensionProcessBindings::ThrowPermissionDeniedException(
            event_name);
      }
    }

    return v8::Undefined();
  }

  static v8::Handle<v8::Value> DetachEvent(const v8::Arguments& args) {
    DCHECK(args.Length() == 1);
    // TODO(erikkay) should enforce that event name is a string in the bindings
    DCHECK(args[0]->IsString() || args[0]->IsUndefined());

    if (args[0]->IsString()) {
      std::string event_name(*v8::String::AsciiValue(args[0]));
      if (EventDecrementListenerCount(event_name) == 0) {
        EventBindings::GetRenderThread()->Send(
          new ViewHostMsg_ExtensionRemoveListener(event_name));
      }
    }

    return v8::Undefined();
  }
};

}  // namespace

const char* EventBindings::kName = "chrome/EventBindings";

v8::Extension* EventBindings::Get() {
  static v8::Extension* extension = new ExtensionImpl();
  bindings_registered = true;
  return extension;
}

// static
void EventBindings::SetRenderThread(RenderThreadBase* thread) {
  render_thread = thread;
  in_unit_tests = true;
}

// static
RenderThreadBase* EventBindings::GetRenderThread() {
  return render_thread ? render_thread : RenderThread::current();
}

static void DeferredUnload(v8::Persistent<v8::Context> context) {
  v8::HandleScope handle_scope;
  CallFunctionInContext(context, "dispatchOnUnload", 0, NULL);
  context.Dispose();
  context.Clear();
}

static void UnregisterContext(ContextList::iterator context_iter, bool in_gc) {
  // Notify the bindings that they're going away.
  if (in_gc) {
    // We shouldn't call back into javascript during a garbage collect.  Do it
    // later.  We'll hang onto the context until this DeferredUnload is called.
    MessageLoop::current()->PostTask(FROM_HERE, NewRunnableFunction(
        DeferredUnload, (*context_iter)->context));
  } else {
    CallFunctionInContext((*context_iter)->context, "dispatchOnUnload",
                          0, NULL);
  }

  // Remove all pending requests for this context.
  PendingRequestMap& pending_requests = GetPendingRequestMap();
  for (PendingRequestMap::iterator it = pending_requests.begin();
       it != pending_requests.end(); ) {
    PendingRequestMap::iterator current = it++;
    if (current->second->context == (*context_iter)->context) {
      current->second->context.Dispose();
      current->second->context.Clear();
      pending_requests.erase(current);
    }
  }

  if (!(*context_iter)->parent_context.IsEmpty()) {
    (*context_iter)->parent_context.Dispose();
    (*context_iter)->parent_context.Clear();
  }

  // Remove it from our registered contexts.
  (*context_iter)->context.ClearWeak();
  if (!in_gc) {
    (*context_iter)->context.Dispose();
    (*context_iter)->context.Clear();
  }

  GetContexts().erase(context_iter);
}

static void ContextWeakReferenceCallback(v8::Persistent<v8::Value> context,
                                         void*) {
  // This should only get called for content script contexts.
  for (ContextList::iterator it = GetContexts().begin();
       it != GetContexts().end(); ++it) {
    if ((*it)->context == context) {
      UnregisterContext(it, true);
      return;
    }
  }

  NOTREACHED();
}

void EventBindings::HandleContextCreated(WebFrame* frame, bool content_script) {
  if (!bindings_registered)
    return;

  v8::HandleScope handle_scope;
  ContextList& contexts = GetContexts();
  v8::Local<v8::Context> frame_context = frame->mainWorldScriptContext();
  v8::Local<v8::Context> context = v8::Context::GetCurrent();
  DCHECK(!context.IsEmpty());
  DCHECK(bindings_utils::FindContext(context) == contexts.end());

  // Figure out the URL for the toplevel frame.  If the top frame is loading,
  // use its provisional URL, since we get this notification before commit.
  WebFrame* main_frame = frame->view()->GetMainFrame();
  WebKit::WebDataSource* ds = main_frame->provisionalDataSource();
  if (!ds)
    ds = main_frame->dataSource();
  GURL url = ds->request().url();
  std::string extension_id;
  if (url.SchemeIs(chrome::kExtensionScheme)) {
    extension_id = url.host();
  } else if (!content_script) {
    // This context is a regular non-extension web page.  Ignore it.  We only
    // care about content scripts and extension frames.
    // (Unless we're in unit tests, in which case we don't care what the URL
    // is).
    DCHECK(frame_context == context);
    if (!in_unit_tests)
      return;
  }

  v8::Persistent<v8::Context> persistent_context =
      v8::Persistent<v8::Context>::New(context);
  v8::Persistent<v8::Context> parent_context;

  if (content_script) {
    DCHECK(frame_context != context);

    parent_context = v8::Persistent<v8::Context>::New(frame_context);
    // Content script contexts can get GCed before their frame goes away, so
    // set up a GC callback.
    persistent_context.MakeWeak(NULL, &ContextWeakReferenceCallback);
  }

  RenderView* render_view = NULL;
  if (frame->view() && frame->view()->GetDelegate())
    render_view = static_cast<RenderView*>(frame->view()->GetDelegate());

  contexts.push_back(linked_ptr<ContextInfo>(
      new ContextInfo(persistent_context, extension_id, parent_context,
                      render_view)));

  v8::Handle<v8::Value> argv[1];
  argv[0] = v8::String::New(extension_id.c_str());
  CallFunctionInContext(context, "dispatchOnLoad", arraysize(argv), argv);
}

// static
void EventBindings::HandleContextDestroyed(WebFrame* frame) {
  if (!bindings_registered)
    return;

  v8::HandleScope handle_scope;
  v8::Local<v8::Context> context = frame->mainWorldScriptContext();
  DCHECK(!context.IsEmpty());

  ContextList::iterator context_iter = bindings_utils::FindContext(context);
  if (context_iter != GetContexts().end())
    UnregisterContext(context_iter, false);

  // Unload any content script contexts for this frame.  Note that the frame
  // itself might not be registered, but can still be a parent context.
  for (ContextList::iterator it = GetContexts().begin();
       it != GetContexts().end(); ) {
    ContextList::iterator current = it++;
    if ((*current)->parent_context == context)
      UnregisterContext(current, false);
  }
}

// static
void EventBindings::CallFunction(const std::string& function_name,
                                 int argc, v8::Handle<v8::Value>* argv,
                                 RenderView* render_view) {
  for (ContextList::iterator it = GetContexts().begin();
       it != GetContexts().end(); ++it) {
    if (render_view && render_view != (*it)->render_view)
      continue;
    v8::Handle<v8::Value> retval = CallFunctionInContext((*it)->context,
        function_name, argc, argv);
    // In debug, the js will validate the event parameters and return a 
    // string if a validation error has occured.
    // TODO(rafaelw): Consider only doing this check if function_name ==
    // "Event.dispatchJSON".
#ifdef _DEBUG
    if (!retval.IsEmpty() && !retval->IsUndefined()) {
      std::string error = *v8::String::AsciiValue(retval);
      DCHECK(false) << error;
    }
#endif
  }
}
