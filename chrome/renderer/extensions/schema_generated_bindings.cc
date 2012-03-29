// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/extensions/schema_generated_bindings.h"

#include <map>
#include <set>
#include <string>

#include "base/callback.h"
#include "base/command_line.h"
#include "base/json/json_reader.h"
#include "base/lazy_instance.h"
#include "base/memory/scoped_ptr.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_action.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/extensions/extension_messages.h"
#include "chrome/common/extensions/extension_set.h"
#include "chrome/common/extensions/url_pattern.h"
#include "chrome/common/extensions/api/extension_api.h"
#include "chrome/common/render_messages.h"
#include "chrome/renderer/chrome_render_process_observer.h"
#include "chrome/renderer/extensions/chrome_v8_context.h"
#include "chrome/renderer/extensions/chrome_v8_context_set.h"
#include "chrome/renderer/extensions/event_bindings.h"
#include "chrome/renderer/extensions/extension_dispatcher.h"
#include "chrome/renderer/extensions/miscellaneous_bindings.h"
#include "chrome/renderer/extensions/user_script_slave.h"
#include "content/public/renderer/render_thread.h"
#include "content/public/renderer/render_view.h"
#include "content/public/renderer/v8_value_converter.h"
#include "grit/common_resources.h"
#include "grit/renderer_resources.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDocument.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFrame.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebSecurityOrigin.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/resource/resource_bundle.h"
#include "v8/include/v8.h"
#include "webkit/glue/webkit_glue.h"

using content::RenderThread;
using content::V8ValueConverter;
using extensions::ExtensionAPI;
using extensions::Feature;
using WebKit::WebFrame;
using WebKit::WebSecurityOrigin;

namespace {

// Contains info relevant to a pending API request.
struct PendingRequest {
 public :
  PendingRequest(v8::Persistent<v8::Context> context, const std::string& name,
                 const std::string& extension_id)
      : context(context), name(name), extension_id(extension_id) {
  }
  v8::Persistent<v8::Context> context;
  std::string name;
  std::string extension_id;
};

class PendingRequestMap {
 public:
  PendingRequestMap() {
  }

  void InsertRequest(int request_id, PendingRequest* pending_request) {
    pending_requests_[request_id].reset(pending_request);
  }

  PendingRequest* GetPendingRequest(int request_id) {
    PendingRequests::iterator i = pending_requests_.find(request_id);
    if (i == pending_requests_.end()) {
      return NULL;
    }
    return i->second.get();
  }

  void RemoveRequest(int request_id) {
    PendingRequest* request = GetPendingRequest(request_id);
    if (!request)
      return;
    request->context.Dispose();
    request->context.Clear();
    pending_requests_.erase(request_id);
  }

 private:
  typedef std::map<int, linked_ptr<PendingRequest> > PendingRequests;
  PendingRequests pending_requests_;

  DISALLOW_COPY_AND_ASSIGN(PendingRequestMap);
};

// TODO(koz): Make this owned by ExtensionDispatcher and pass it into
// SchemaGeneratedBindings.
base::LazyInstance<PendingRequestMap> g_pending_requests =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace

namespace extensions {

SchemaGeneratedBindings::SchemaGeneratedBindings(
    ExtensionDispatcher* extension_dispatcher)
    : ChromeV8Extension(extension_dispatcher) {
  RouteFunction("GetExtensionAPIDefinition",
                base::Bind(&SchemaGeneratedBindings::GetExtensionAPIDefinition,
                           base::Unretained(this)));
  RouteFunction("GetNextRequestId",
                base::Bind(&SchemaGeneratedBindings::GetNextRequestId,
                           base::Unretained(this)));
  RouteFunction("StartRequest",
                base::Bind(&SchemaGeneratedBindings::StartRequest,
                           base::Unretained(this)));
  RouteFunction("SetIconCommon",
                base::Bind(&SchemaGeneratedBindings::SetIconCommon,
                           base::Unretained(this)));
}

v8::Handle<v8::Value> SchemaGeneratedBindings::GetExtensionAPIDefinition(
    const v8::Arguments& args) {
  ChromeV8Context* v8_context =
      extension_dispatcher()->v8_context_set().GetCurrent();
  CHECK(v8_context);

  // TODO(kalman): This is being calculated twice, first in
  // ExtensionDispatcher then again here. It might as well be a property of
  // ChromeV8Context, however, this would require making ChromeV8Context take
  // an Extension rather than an extension ID.  In itself this is fine,
  // however it does not play correctly with the "IsTestExtensionId" checks.
  // We need to remove that first.
  scoped_ptr<std::set<std::string> > apis;

  const std::string& extension_id = v8_context->extension_id();
  if (extension_dispatcher()->IsTestExtensionId(extension_id)) {
    apis.reset(new std::set<std::string>());
    // The minimal set of APIs that tests need.
    apis->insert("extension");
  } else {
    apis = ExtensionAPI::GetInstance()->GetAPIsForContext(
        v8_context->context_type(),
        extension_dispatcher()->extensions()->GetByID(extension_id),
        UserScriptSlave::GetDataSourceURLForFrame(v8_context->web_frame()));
  }

  return extension_dispatcher()->v8_schema_registry()->GetSchemas(*apis);
}

v8::Handle<v8::Value> SchemaGeneratedBindings::GetNextRequestId(
    const v8::Arguments& args) {
  static int next_request_id = 0;
  return v8::Integer::New(next_request_id++);
}

v8::Handle<v8::Value> SchemaGeneratedBindings::StartRequestCommon(
    const v8::Arguments& args, ListValue* value_args) {

  const ChromeV8ContextSet& contexts =
      extension_dispatcher()->v8_context_set();
  ChromeV8Context* current_context = contexts.GetCurrent();
  if (!current_context)
    return v8::Undefined();

  // Get the current RenderView so that we can send a routed IPC message from
  // the correct source.
  content::RenderView* renderview = current_context->GetRenderView();
  if (!renderview)
    return v8::Undefined();

  std::string name = *v8::String::AsciiValue(args[0]);
  const std::set<std::string>& function_names =
      extension_dispatcher_->function_names();
  if (function_names.find(name) == function_names.end()) {
    NOTREACHED() << "Unexpected function " << name <<
        ". Did you remember to register it with ExtensionFunctionRegistry?";
    return v8::Undefined();
  }

  if (!CheckCurrentContextAccessToExtensionAPI(name))
    return v8::Undefined();

  GURL source_url;
  WebSecurityOrigin source_origin;
  WebFrame* webframe = current_context->web_frame();
  if (webframe) {
    source_url = webframe->document().url();
    source_origin = webframe->document().securityOrigin();
  }

  int request_id = args[2]->Int32Value();
  bool has_callback = args[3]->BooleanValue();
  bool for_io_thread = args[4]->BooleanValue();

  v8::Persistent<v8::Context> v8_context =
      v8::Persistent<v8::Context>::New(v8::Context::GetCurrent());
  DCHECK(!v8_context.IsEmpty());
  g_pending_requests.Get().InsertRequest(request_id, new PendingRequest(
      v8_context, name, current_context->extension_id()));

  ExtensionHostMsg_Request_Params params;
  params.name = name;
  params.arguments.Swap(value_args);
  params.extension_id = current_context->extension_id();
  params.source_url = source_url;
  params.source_origin = source_origin.toString();
  params.request_id = request_id;
  params.has_callback = has_callback;
  params.user_gesture =
      webframe ? webframe->isProcessingUserGesture() : false;
  if (for_io_thread) {
    renderview->Send(new ExtensionHostMsg_RequestForIOThread(
        renderview->GetRoutingID(), params));
  } else {
    renderview->Send(new ExtensionHostMsg_Request(
        renderview->GetRoutingID(), params));
  }

  return v8::Undefined();
}

// Starts an API request to the browser, with an optional callback.  The
// callback will be dispatched to EventBindings::HandleResponse.
v8::Handle<v8::Value> SchemaGeneratedBindings::StartRequest(
    const v8::Arguments& args) {
  std::string str_args = *v8::String::Utf8Value(args[1]);
  base::JSONReader reader;
  scoped_ptr<Value> value_args;
  value_args.reset(reader.JsonToValue(str_args, false, false));

  // Since we do the serialization in the v8 extension, we should always get
  // valid JSON.
  if (!value_args.get() || !value_args->IsType(Value::TYPE_LIST)) {
    NOTREACHED() << "Invalid JSON passed to StartRequest.";
    return v8::Undefined();
  }

  return StartRequestCommon(args, static_cast<ListValue*>(value_args.get()));
}

bool SchemaGeneratedBindings::ConvertImageDataToBitmapValue(
    const v8::Arguments& args, Value** bitmap_value) {
  v8::Local<v8::Object> extension_args = args[1]->ToObject();
  v8::Local<v8::Object> details =
      extension_args->Get(v8::String::New("0"))->ToObject();
  v8::Local<v8::Object> image_data =
      details->Get(v8::String::New("imageData"))->ToObject();
  v8::Local<v8::Object> data =
      image_data->Get(v8::String::New("data"))->ToObject();
  int width = image_data->Get(v8::String::New("width"))->Int32Value();
  int height = image_data->Get(v8::String::New("height"))->Int32Value();

  int data_length = data->Get(v8::String::New("length"))->Int32Value();
  if (data_length != 4 * width * height) {
    NOTREACHED() << "Invalid argument to setIcon. Expecting ImageData.";
    return false;
  }

  SkBitmap bitmap;
  bitmap.setConfig(SkBitmap::kARGB_8888_Config, width, height);
  bitmap.allocPixels();
  bitmap.eraseARGB(0, 0, 0, 0);

  uint32_t* pixels = bitmap.getAddr32(0, 0);
  for (int t = 0; t < width*height; t++) {
    // |data| is RGBA, pixels is ARGB.
    pixels[t] = SkPreMultiplyColor(
        ((data->Get(v8::Integer::New(4*t + 3))->Int32Value() & 0xFF) << 24) |
        ((data->Get(v8::Integer::New(4*t + 0))->Int32Value() & 0xFF) << 16) |
        ((data->Get(v8::Integer::New(4*t + 1))->Int32Value() & 0xFF) << 8) |
        ((data->Get(v8::Integer::New(4*t + 2))->Int32Value() & 0xFF) << 0));
  }

  // Construct the Value object.
  IPC::Message bitmap_pickle;
  IPC::WriteParam(&bitmap_pickle, bitmap);
  *bitmap_value = base::BinaryValue::CreateWithCopiedBuffer(
      static_cast<const char*>(bitmap_pickle.data()), bitmap_pickle.size());

  return true;
}

v8::Handle<v8::Value> SchemaGeneratedBindings::SetIconCommon(
    const v8::Arguments& args) {
  Value* bitmap_value = NULL;
  if (!ConvertImageDataToBitmapValue(args, &bitmap_value))
    return v8::Undefined();

  v8::Local<v8::Object> extension_args = args[1]->ToObject();
  v8::Local<v8::Object> details =
      extension_args->Get(v8::String::New("0"))->ToObject();

  DictionaryValue* dict = new DictionaryValue();
  dict->Set("imageData", bitmap_value);

  if (details->Has(v8::String::New("tabId"))) {
    dict->SetInteger("tabId",
                     details->Get(v8::String::New("tabId"))->Int32Value());
  }

  ListValue list_value;
  list_value.Append(dict);

  return StartRequestCommon(args, &list_value);
}

// static
void SchemaGeneratedBindings::HandleResponse(const ChromeV8ContextSet& contexts,
                                             int request_id,
                                             bool success,
                                             const std::string& response,
                                             const std::string& error,
                                             std::string* extension_id) {
  PendingRequest* request =
      g_pending_requests.Get().GetPendingRequest(request_id);
  if (!request) {
    // This should not be able to happen since we only remove requests when they
    // are handled.
    LOG(ERROR) << "Could not find specified request id: " << request_id;
    return;
  }

  ChromeV8Context* v8_context =
      contexts.GetByV8Context(request->context);
  if (!v8_context)
    return;  // The frame went away.

  v8::HandleScope handle_scope;
  v8::Handle<v8::Value> argv[5];
  argv[0] = v8::Integer::New(request_id);
  argv[1] = v8::String::New(request->name.c_str());
  argv[2] = v8::Boolean::New(success);
  argv[3] = v8::String::New(response.c_str());
  argv[4] = v8::String::New(error.c_str());

  v8::Handle<v8::Value> retval;
  CHECK(v8_context->CallChromeHiddenMethod("handleResponse",
                                           arraysize(argv),
                                           argv,
                                           &retval));
  // In debug, the js will validate the callback parameters and return a
  // string if a validation error has occured.
#ifndef NDEBUG
  if (!retval.IsEmpty() && !retval->IsUndefined()) {
    std::string error = *v8::String::AsciiValue(retval);
    DCHECK(false) << error;
  }
#endif

  // Save the extension id before erasing the request.
  *extension_id = request->extension_id;

  g_pending_requests.Get().RemoveRequest(request_id);
}

}  // namespace extensions
