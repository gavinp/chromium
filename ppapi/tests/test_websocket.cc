// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/tests/test_websocket.h"

#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

#include "ppapi/c/dev/ppb_testing_dev.h"
#include "ppapi/c/pp_bool.h"
#include "ppapi/c/pp_completion_callback.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/c/pp_instance.h"
#include "ppapi/c/pp_resource.h"
#include "ppapi/c/pp_var.h"
#include "ppapi/c/ppb_core.h"
#include "ppapi/c/ppb_var.h"
#include "ppapi/c/ppb_var_array_buffer.h"
#include "ppapi/c/ppb_websocket.h"
#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/var_array_buffer.h"
#include "ppapi/cpp/websocket.h"
#include "ppapi/tests/test_utils.h"
#include "ppapi/tests/testing_instance.h"
#include "ppapi/utility/websocket/websocket_api.h"

// These servers are provided by pywebsocket server side handlers in
// LayoutTests/http/tests/websocket/tests/hybi/*_wsh.
// pywebsocket server itself is launched in ppapi_ui_test.cc.
const char kEchoServerURL[] = "websocket/tests/hybi/echo-with-no-extension";

const char kCloseServerURL[] = "websocket/tests/hybi/close";

const char kProtocolTestServerURL[] =
    "websocket/tests/hybi/protocol-test?protocol=";

const char* const kInvalidURLs[] = {
  "http://www.google.com/invalid_scheme",
  "ws://www.google.com/invalid#fragment",
  "ws://www.google.com:65535/invalid_port",
  NULL
};

// Internal packet sizes.
const uint64_t kCloseFrameSize = 6;
const uint64_t kMessageFrameOverhead = 6;

namespace {

struct WebSocketEvent {
  enum EventType {
    EVENT_OPEN,
    EVENT_MESSAGE,
    EVENT_ERROR,
    EVENT_CLOSE
  };

  WebSocketEvent(EventType type,
                 bool was_clean,
                 uint16_t close_code,
                 const pp::Var& var)
      : event_type(type),
        was_clean(was_clean),
        close_code(close_code),
        var(var) {
  }
  EventType event_type;
  bool was_clean;
  uint16_t close_code;
  pp::Var var;
};

class TestWebSocketAPI : public pp::WebSocketAPI {
 public:
  explicit TestWebSocketAPI(pp::Instance* instance)
      : pp::WebSocketAPI(instance),
        connected_(false),
        received_(false),
        closed_(false),
        wait_for_connected_(false),
        wait_for_received_(false),
        wait_for_closed_(false),
        instance_(instance->pp_instance()) {
  }

  virtual void WebSocketDidOpen() {
    events_.push_back(
        WebSocketEvent(WebSocketEvent::EVENT_OPEN, true, 0U, pp::Var()));
    connected_ = true;
    if (wait_for_connected_) {
      GetTestingInterface()->QuitMessageLoop(instance_);
      wait_for_connected_ = false;
    }
  }

  virtual void WebSocketDidClose(
      bool was_clean, uint16_t code, const pp::Var& reason) {
    events_.push_back(
        WebSocketEvent(WebSocketEvent::EVENT_CLOSE, was_clean, code, reason));
    connected_ = true;
    closed_ = true;
    if (wait_for_connected_ || wait_for_closed_) {
      GetTestingInterface()->QuitMessageLoop(instance_);
      wait_for_connected_ = false;
      wait_for_closed_ = false;
    }
  }

  virtual void HandleWebSocketMessage(const pp::Var &message) {
    events_.push_back(
        WebSocketEvent(WebSocketEvent::EVENT_MESSAGE, true, 0U, message));
    received_ = true;
    if (wait_for_received_) {
      GetTestingInterface()->QuitMessageLoop(instance_);
      wait_for_received_ = false;
      received_ = false;
    }
  }

  virtual void HandleWebSocketError() {
    events_.push_back(
        WebSocketEvent(WebSocketEvent::EVENT_ERROR, true, 0U, pp::Var()));
  }

  void WaitForConnected() {
    if (!connected_) {
      wait_for_connected_ = true;
      GetTestingInterface()->RunMessageLoop(instance_);
    }
  }

  void WaitForReceived() {
    if (!received_) {
      wait_for_received_ = true;
      GetTestingInterface()->RunMessageLoop(instance_);
    }
  }

  void WaitForClosed() {
    if (!closed_) {
      wait_for_closed_ = true;
      GetTestingInterface()->RunMessageLoop(instance_);
    }
  }

  const std::vector<WebSocketEvent>& GetSeenEvents() const {
    return events_;
  }

 private:
  std::vector<WebSocketEvent> events_;
  bool connected_;
  bool received_;
  bool closed_;
  bool wait_for_connected_;
  bool wait_for_received_;
  bool wait_for_closed_;
  PP_Instance instance_;
};

}  // namespace

REGISTER_TEST_CASE(WebSocket);

bool TestWebSocket::Init() {
  websocket_interface_ = static_cast<const PPB_WebSocket*>(
      pp::Module::Get()->GetBrowserInterface(PPB_WEBSOCKET_INTERFACE));
  var_interface_ = static_cast<const PPB_Var*>(
      pp::Module::Get()->GetBrowserInterface(PPB_VAR_INTERFACE));
  arraybuffer_interface_ = static_cast<const PPB_VarArrayBuffer*>(
      pp::Module::Get()->GetBrowserInterface(
          PPB_VAR_ARRAY_BUFFER_INTERFACE));
  core_interface_ = static_cast<const PPB_Core*>(
      pp::Module::Get()->GetBrowserInterface(PPB_CORE_INTERFACE));
  if (!websocket_interface_ || !var_interface_ || !arraybuffer_interface_ ||
      !core_interface_)
    return false;

  return CheckTestingInterface();
}

void TestWebSocket::RunTests(const std::string& filter) {
  RUN_TEST_WITH_REFERENCE_CHECK(IsWebSocket, filter);
  RUN_TEST_WITH_REFERENCE_CHECK(UninitializedPropertiesAccess, filter);
  RUN_TEST_WITH_REFERENCE_CHECK(InvalidConnect, filter);
  RUN_TEST_WITH_REFERENCE_CHECK(Protocols, filter);
  RUN_TEST_WITH_REFERENCE_CHECK(GetURL, filter);
  RUN_TEST_WITH_REFERENCE_CHECK(ValidConnect, filter);
  RUN_TEST_WITH_REFERENCE_CHECK(InvalidClose, filter);
  RUN_TEST_WITH_REFERENCE_CHECK(ValidClose, filter);
  RUN_TEST_WITH_REFERENCE_CHECK(GetProtocol, filter);
  RUN_TEST_WITH_REFERENCE_CHECK(TextSendReceive, filter);
  RUN_TEST_WITH_REFERENCE_CHECK(BinarySendReceive, filter);
  RUN_TEST_WITH_REFERENCE_CHECK(StressedSendReceive, filter);
  RUN_TEST_WITH_REFERENCE_CHECK(BufferedAmount, filter);

  RUN_TEST_WITH_REFERENCE_CHECK(CcInterfaces, filter);

  RUN_TEST_WITH_REFERENCE_CHECK(UtilityInvalidConnect, filter);
  RUN_TEST_WITH_REFERENCE_CHECK(UtilityProtocols, filter);
  RUN_TEST_WITH_REFERENCE_CHECK(UtilityGetURL, filter);
  RUN_TEST_WITH_REFERENCE_CHECK(UtilityValidConnect, filter);
  RUN_TEST_WITH_REFERENCE_CHECK(UtilityInvalidClose, filter);
  RUN_TEST_WITH_REFERENCE_CHECK(UtilityValidClose, filter);
  RUN_TEST_WITH_REFERENCE_CHECK(UtilityGetProtocol, filter);
  RUN_TEST_WITH_REFERENCE_CHECK(UtilityTextSendReceive, filter);
  RUN_TEST_WITH_REFERENCE_CHECK(UtilityBinarySendReceive, filter);
  RUN_TEST_WITH_REFERENCE_CHECK(UtilityBufferedAmount, filter);
}

std::string TestWebSocket::GetFullURL(const char* url) {
  std::string rv = "ws://localhost";
  // Some WebSocket tests don't start the server so there'll be no port.
  if (instance_->websocket_port() != -1) {
    char buffer[10];
    sprintf(buffer, ":%d", instance_->websocket_port());
    rv += std::string(buffer);
  }
  rv += "/";
  rv += url;
  return rv;
}

PP_Var TestWebSocket::CreateVarString(const std::string& string) {
  return var_interface_->VarFromUtf8(string.c_str(), string.size());
}

PP_Var TestWebSocket::CreateVarBinary(const std::vector<uint8_t>& binary) {
  PP_Var var = arraybuffer_interface_->Create(binary.size());
  uint8_t* var_data = static_cast<uint8_t*>(arraybuffer_interface_->Map(var));
  std::copy(binary.begin(), binary.end(), var_data);
  return var;
}

void TestWebSocket::ReleaseVar(const PP_Var& var) {
  var_interface_->Release(var);
}

bool TestWebSocket::AreEqualWithString(const PP_Var& var,
                                       const std::string& string) {
  if (var.type != PP_VARTYPE_STRING)
    return false;
  uint32_t utf8_length;
  const char* utf8 = var_interface_->VarToUtf8(var, &utf8_length);
  if (utf8_length != string.size())
    return false;
  if (string.compare(utf8))
    return false;
  return true;
}

bool TestWebSocket::AreEqualWithBinary(const PP_Var& var,
                                       const std::vector<uint8_t>& binary) {
  uint32_t buffer_size = 0;
  PP_Bool success = arraybuffer_interface_->ByteLength(var, &buffer_size);
  if (!success || buffer_size != binary.size())
    return false;
  if (!std::equal(binary.begin(), binary.end(),
      static_cast<uint8_t*>(arraybuffer_interface_->Map(var))))
    return false;
  return true;
}

PP_Resource TestWebSocket::Connect(const std::string& url,
                                   int32_t* result,
                                   const std::string& protocol) {
  PP_Var protocols[] = { PP_MakeUndefined() };
  PP_Resource ws = websocket_interface_->Create(instance_->pp_instance());
  if (!ws)
    return 0;
  PP_Var url_var = CreateVarString(url);
  TestCompletionCallback callback(instance_->pp_instance(), force_async_);
  uint32_t protocol_count = 0U;
  if (protocol.size()) {
    protocols[0] = CreateVarString(protocol);
    protocol_count = 1U;
  }
  *result = websocket_interface_->Connect(
      ws, url_var, protocols, protocol_count,
      static_cast<pp::CompletionCallback>(callback).pp_completion_callback());
  ReleaseVar(url_var);
  if (protocol.size())
    ReleaseVar(protocols[0]);
  if (*result == PP_OK_COMPLETIONPENDING)
    *result = callback.WaitForResult();
  return ws;
}

std::string TestWebSocket::TestIsWebSocket() {
  // Test that a NULL resource isn't a websocket.
  pp::Resource null_resource;
  PP_Bool result =
      websocket_interface_->IsWebSocket(null_resource.pp_resource());
  ASSERT_FALSE(result);

  PP_Resource ws = websocket_interface_->Create(instance_->pp_instance());
  ASSERT_TRUE(ws);

  result = websocket_interface_->IsWebSocket(ws);
  ASSERT_TRUE(result);

  core_interface_->ReleaseResource(ws);

  PASS();
}

std::string TestWebSocket::TestUninitializedPropertiesAccess() {
  PP_Resource ws = websocket_interface_->Create(instance_->pp_instance());
  ASSERT_TRUE(ws);

  uint64_t bufferedAmount = websocket_interface_->GetBufferedAmount(ws);
  ASSERT_EQ(0U, bufferedAmount);

  uint16_t close_code = websocket_interface_->GetCloseCode(ws);
  ASSERT_EQ(0U, close_code);

  PP_Var close_reason = websocket_interface_->GetCloseReason(ws);
  ASSERT_TRUE(AreEqualWithString(close_reason, ""));
  ReleaseVar(close_reason);

  PP_Bool close_was_clean = websocket_interface_->GetCloseWasClean(ws);
  ASSERT_EQ(PP_FALSE, close_was_clean);

  PP_Var extensions = websocket_interface_->GetExtensions(ws);
  ASSERT_TRUE(AreEqualWithString(extensions, ""));
  ReleaseVar(extensions);

  PP_Var protocol = websocket_interface_->GetProtocol(ws);
  ASSERT_TRUE(AreEqualWithString(protocol, ""));
  ReleaseVar(protocol);

  PP_WebSocketReadyState ready_state =
      websocket_interface_->GetReadyState(ws);
  ASSERT_EQ(PP_WEBSOCKETREADYSTATE_INVALID, ready_state);

  PP_Var url = websocket_interface_->GetURL(ws);
  ASSERT_TRUE(AreEqualWithString(url, ""));
  ReleaseVar(url);

  core_interface_->ReleaseResource(ws);

  PASS();
}

std::string TestWebSocket::TestInvalidConnect() {
  PP_Var protocols[] = { PP_MakeUndefined() };

  PP_Resource ws = websocket_interface_->Create(instance_->pp_instance());
  ASSERT_TRUE(ws);

  TestCompletionCallback callback(instance_->pp_instance(), force_async_);
  int32_t result = websocket_interface_->Connect(
      ws, PP_MakeUndefined(), protocols, 1U,
      static_cast<pp::CompletionCallback>(callback).pp_completion_callback());
  ASSERT_EQ(PP_ERROR_BADARGUMENT, result);

  result = websocket_interface_->Connect(
      ws, PP_MakeUndefined(), protocols, 1U,
      static_cast<pp::CompletionCallback>(callback).pp_completion_callback());
  ASSERT_EQ(PP_ERROR_INPROGRESS, result);

  core_interface_->ReleaseResource(ws);

  for (int i = 0; kInvalidURLs[i]; ++i) {
    ws = Connect(kInvalidURLs[i], &result, "");
    ASSERT_TRUE(ws);
    ASSERT_EQ(PP_ERROR_BADARGUMENT, result);

    core_interface_->ReleaseResource(ws);
  }

  PASS();
}

std::string TestWebSocket::TestProtocols() {
  PP_Var url = CreateVarString(GetFullURL(kEchoServerURL).c_str());
  PP_Var bad_protocols[] = {
    CreateVarString("x-test"),
    CreateVarString("x-test")
  };
  PP_Var good_protocols[] = {
    CreateVarString("x-test"),
    CreateVarString("x-yatest")
  };

  PP_Resource ws = websocket_interface_->Create(instance_->pp_instance());
  ASSERT_TRUE(ws);
  TestCompletionCallback callback(instance_->pp_instance(), force_async_);
  int32_t result = websocket_interface_->Connect(
      ws, url, bad_protocols, 2U,
      static_cast<pp::CompletionCallback>(callback).pp_completion_callback());
  if (result == PP_OK_COMPLETIONPENDING)
    result = callback.WaitForResult();
  ASSERT_EQ(PP_ERROR_BADARGUMENT, result);
  core_interface_->ReleaseResource(ws);

  ws = websocket_interface_->Create(instance_->pp_instance());
  ASSERT_TRUE(ws);
  result = websocket_interface_->Connect(
      ws, url, good_protocols, 2U, PP_BlockUntilComplete());
  ASSERT_EQ(PP_ERROR_BLOCKS_MAIN_THREAD, result);
  core_interface_->ReleaseResource(ws);

  ReleaseVar(url);
  for (int i = 0; i < 2; ++i) {
    ReleaseVar(bad_protocols[i]);
    ReleaseVar(good_protocols[i]);
  }
  core_interface_->ReleaseResource(ws);

  PASS();
}

std::string TestWebSocket::TestGetURL() {
  for (int i = 0; kInvalidURLs[i]; ++i) {
    int32_t result;
    PP_Resource ws = Connect(kInvalidURLs[i], &result, "");
    ASSERT_TRUE(ws);
    PP_Var url = websocket_interface_->GetURL(ws);
    ASSERT_TRUE(AreEqualWithString(url, kInvalidURLs[i]));
    ASSERT_EQ(PP_ERROR_BADARGUMENT, result);

    ReleaseVar(url);
    core_interface_->ReleaseResource(ws);
  }

  PASS();
}

std::string TestWebSocket::TestValidConnect() {
  int32_t result;
  PP_Resource ws = Connect(GetFullURL(kEchoServerURL), &result, "");
  ASSERT_TRUE(ws);
  ASSERT_EQ(PP_OK, result);
  PP_Var extensions = websocket_interface_->GetExtensions(ws);
  ASSERT_TRUE(AreEqualWithString(extensions, ""));
  core_interface_->ReleaseResource(ws);

  PASS();
}

std::string TestWebSocket::TestInvalidClose() {
  PP_Var reason = CreateVarString("close for test");
  TestCompletionCallback callback(instance_->pp_instance());

  // Close before connect.
  PP_Resource ws = websocket_interface_->Create(instance_->pp_instance());
  int32_t result = websocket_interface_->Close(
      ws, PP_WEBSOCKETSTATUSCODE_NORMAL_CLOSURE, reason,
      static_cast<pp::CompletionCallback>(callback).pp_completion_callback());
  ASSERT_EQ(PP_ERROR_FAILED, result);
  core_interface_->ReleaseResource(ws);

  // Close with bad arguments.
  ws = Connect(GetFullURL(kEchoServerURL), &result, "");
  ASSERT_TRUE(ws);
  ASSERT_EQ(PP_OK, result);
  result = websocket_interface_->Close(ws, 1U, reason,
      static_cast<pp::CompletionCallback>(callback).pp_completion_callback());
  ASSERT_EQ(PP_ERROR_NOACCESS, result);
  core_interface_->ReleaseResource(ws);

  ReleaseVar(reason);

  PASS();
}

std::string TestWebSocket::TestValidClose() {
  PP_Var reason = CreateVarString("close for test");
  PP_Var url = CreateVarString(GetFullURL(kEchoServerURL).c_str());
  PP_Var protocols[] = { PP_MakeUndefined() };
  TestCompletionCallback callback(instance_->pp_instance());
  TestCompletionCallback another_callback(instance_->pp_instance());

  // Close.
  int32_t result;
  PP_Resource ws = Connect(GetFullURL(kEchoServerURL), &result, "");
  ASSERT_TRUE(ws);
  ASSERT_EQ(PP_OK, result);
  result = websocket_interface_->Close(ws,
      PP_WEBSOCKETSTATUSCODE_NORMAL_CLOSURE, reason,
      static_cast<pp::CompletionCallback>(callback).pp_completion_callback());
  ASSERT_EQ(PP_OK_COMPLETIONPENDING, result);
  result = callback.WaitForResult();
  ASSERT_EQ(PP_OK, result);
  core_interface_->ReleaseResource(ws);

  // Close in connecting.
  // The ongoing connect failed with PP_ERROR_ABORTED, then the close is done
  // successfully.
  ws = websocket_interface_->Create(instance_->pp_instance());
  result = websocket_interface_->Connect(ws, url, protocols, 0U,
      static_cast<pp::CompletionCallback>(callback).pp_completion_callback());
  ASSERT_EQ(PP_OK_COMPLETIONPENDING, result);
  result = websocket_interface_->Close(ws,
      PP_WEBSOCKETSTATUSCODE_NORMAL_CLOSURE, reason,
      static_cast<pp::CompletionCallback>(
          another_callback).pp_completion_callback());
  ASSERT_EQ(PP_OK_COMPLETIONPENDING, result);
  result = callback.WaitForResult();
  ASSERT_EQ(PP_ERROR_ABORTED, result);
  result = another_callback.WaitForResult();
  ASSERT_EQ(PP_OK, result);
  core_interface_->ReleaseResource(ws);

  // Close in closing.
  // The first close will be done successfully, then the second one failed with
  // with PP_ERROR_INPROGRESS immediately.
  ws = Connect(GetFullURL(kEchoServerURL), &result, "");
  ASSERT_TRUE(ws);
  ASSERT_EQ(PP_OK, result);
  result = websocket_interface_->Close(ws,
      PP_WEBSOCKETSTATUSCODE_NORMAL_CLOSURE, reason,
      static_cast<pp::CompletionCallback>(callback).pp_completion_callback());
  ASSERT_EQ(PP_OK_COMPLETIONPENDING, result);
  result = websocket_interface_->Close(ws,
      PP_WEBSOCKETSTATUSCODE_NORMAL_CLOSURE, reason,
      static_cast<pp::CompletionCallback>(
          another_callback).pp_completion_callback());
  ASSERT_EQ(PP_ERROR_INPROGRESS, result);
  result = callback.WaitForResult();
  ASSERT_EQ(PP_OK, result);
  core_interface_->ReleaseResource(ws);

  // Close with ongoing receive message.
  ws = Connect(GetFullURL(kEchoServerURL), &result, "");
  ASSERT_TRUE(ws);
  ASSERT_EQ(PP_OK, result);
  PP_Var receive_message_var;
  result = websocket_interface_->ReceiveMessage(ws, &receive_message_var,
      static_cast<pp::CompletionCallback>(callback).pp_completion_callback());
  ASSERT_EQ(PP_OK_COMPLETIONPENDING, result);
  result = websocket_interface_->Close(ws,
      PP_WEBSOCKETSTATUSCODE_NORMAL_CLOSURE, reason,
      static_cast<pp::CompletionCallback>(
          another_callback).pp_completion_callback());
  ASSERT_EQ(PP_OK_COMPLETIONPENDING, result);
  result = callback.WaitForResult();
  ASSERT_EQ(PP_ERROR_ABORTED, result);
  result = another_callback.WaitForResult();
  ASSERT_EQ(PP_OK, result);
  core_interface_->ReleaseResource(ws);

  ReleaseVar(reason);
  ReleaseVar(url);

  PASS();
}

std::string TestWebSocket::TestGetProtocol() {
  const char* expected_protocols[] = {
    "x-chat",
    "hoehoe",
    NULL
  };
  for (int i = 0; expected_protocols[i]; ++i) {
    std::string url(GetFullURL(kProtocolTestServerURL));
    url += expected_protocols[i];
    int32_t result;
    PP_Resource ws = Connect(url.c_str(), &result, expected_protocols[i]);
    ASSERT_TRUE(ws);
    ASSERT_EQ(PP_OK, result);

    PP_Var protocol = websocket_interface_->GetProtocol(ws);
    ASSERT_TRUE(AreEqualWithString(protocol, expected_protocols[i]));

    ReleaseVar(protocol);
    core_interface_->ReleaseResource(ws);
  }

  PASS();
}

std::string TestWebSocket::TestTextSendReceive() {
  // Connect to test echo server.
  int32_t connect_result;
  PP_Resource ws = Connect(GetFullURL(kEchoServerURL), &connect_result, "");
  ASSERT_TRUE(ws);
  ASSERT_EQ(PP_OK, connect_result);

  // Send 'hello pepper' text message.
  const char* message = "hello pepper";
  PP_Var message_var = CreateVarString(message);
  int32_t result = websocket_interface_->SendMessage(ws, message_var);
  ReleaseVar(message_var);
  ASSERT_EQ(PP_OK, result);

  // Receive echoed 'hello pepper'.
  TestCompletionCallback callback(instance_->pp_instance(), force_async_);
  PP_Var received_message;
  result = websocket_interface_->ReceiveMessage(ws, &received_message,
      static_cast<pp::CompletionCallback>(callback).pp_completion_callback());
  ASSERT_FALSE(result != PP_OK && result != PP_OK_COMPLETIONPENDING);
  if (result == PP_OK_COMPLETIONPENDING)
    result = callback.WaitForResult();
  ASSERT_EQ(PP_OK, result);
  ASSERT_TRUE(AreEqualWithString(received_message, message));
  ReleaseVar(received_message);
  core_interface_->ReleaseResource(ws);

  PASS();
}

std::string TestWebSocket::TestBinarySendReceive() {
  // Connect to test echo server.
  int32_t connect_result;
  PP_Resource ws = Connect(GetFullURL(kEchoServerURL), &connect_result, "");
  ASSERT_TRUE(ws);
  ASSERT_EQ(PP_OK, connect_result);

  // Send binary message.
  std::vector<uint8_t> binary(256);
  for (uint32_t i = 0; i < binary.size(); ++i)
    binary[i] = i;
  PP_Var message_var = CreateVarBinary(binary);
  int32_t result = websocket_interface_->SendMessage(ws, message_var);
  ReleaseVar(message_var);
  ASSERT_EQ(PP_OK, result);

  // Receive echoed binary.
  TestCompletionCallback callback(instance_->pp_instance(), force_async_);
  PP_Var received_message;
  result = websocket_interface_->ReceiveMessage(ws, &received_message,
      static_cast<pp::CompletionCallback>(callback).pp_completion_callback());
  ASSERT_TRUE(result == PP_OK || result == PP_OK_COMPLETIONPENDING);
  if (result == PP_OK_COMPLETIONPENDING)
    result = callback.WaitForResult();
  ASSERT_EQ(PP_OK, result);
  ASSERT_TRUE(AreEqualWithBinary(received_message, binary));
  ReleaseVar(received_message);
  core_interface_->ReleaseResource(ws);

  PASS();
}

std::string TestWebSocket::TestStressedSendReceive() {
  // Connect to test echo server.
  int32_t connect_result;
  PP_Resource ws = Connect(GetFullURL(kEchoServerURL), &connect_result, "");
  ASSERT_TRUE(ws);
  ASSERT_EQ(PP_OK, connect_result);

  // Prepare PP_Var objects to send.
  const char* text = "hello pepper";
  PP_Var text_var = CreateVarString(text);
  std::vector<uint8_t> binary(256);
  for (uint32_t i = 0; i < binary.size(); ++i)
    binary[i] = i;
  PP_Var binary_var = CreateVarBinary(binary);

  // Send many messages.
  for (int i = 0; i < 256; ++i) {
    int32_t result = websocket_interface_->SendMessage(ws, text_var);
    ASSERT_EQ(PP_OK, result);
    result = websocket_interface_->SendMessage(ws, binary_var);
    ASSERT_EQ(PP_OK, result);
  }
  ReleaseVar(text_var);
  ReleaseVar(binary_var);

  // Receive echoed data.
  for (int i = 0; i < 512; ++i) {
    TestCompletionCallback callback(instance_->pp_instance(), force_async_);
    PP_Var received_message;
    int32_t result = websocket_interface_->ReceiveMessage(
        ws, &received_message, static_cast<pp::CompletionCallback>(
            callback).pp_completion_callback());
    ASSERT_TRUE(result == PP_OK || result == PP_OK_COMPLETIONPENDING);
    if (result == PP_OK_COMPLETIONPENDING)
      result = callback.WaitForResult();
    ASSERT_EQ(PP_OK, result);
    if (i & 1) {
      ASSERT_TRUE(AreEqualWithBinary(received_message, binary));
    } else {
      ASSERT_TRUE(AreEqualWithString(received_message, text));
    }
    ReleaseVar(received_message);
  }
  core_interface_->ReleaseResource(ws);

  PASS();
}

std::string TestWebSocket::TestBufferedAmount() {
  // Connect to test echo server.
  int32_t connect_result;
  PP_Resource ws = Connect(GetFullURL(kEchoServerURL), &connect_result, "");
  ASSERT_TRUE(ws);
  ASSERT_EQ(PP_OK, connect_result);

  // Prepare a large message that is not aligned with the internal buffer
  // sizes.
  std::string message(8193, 'x');
  PP_Var message_var = CreateVarString(message);

  uint64_t buffered_amount = 0;
  int32_t result;
  for (int i = 0; i < 100; i++) {
    result = websocket_interface_->SendMessage(ws, message_var);
    ASSERT_EQ(PP_OK, result);
    buffered_amount = websocket_interface_->GetBufferedAmount(ws);
    // Buffered amount size 262144 is too big for the internal buffer size.
    if (buffered_amount > 262144)
      break;
  }

  // Close connection.
  std::string reason_str = "close while busy";
  PP_Var reason = CreateVarString(reason_str.c_str());
  TestCompletionCallback callback(instance_->pp_instance());
  result = websocket_interface_->Close(ws,
      PP_WEBSOCKETSTATUSCODE_NORMAL_CLOSURE, reason,
      static_cast<pp::CompletionCallback>(callback).pp_completion_callback());
  ASSERT_EQ(PP_OK_COMPLETIONPENDING, result);
  ASSERT_EQ(PP_WEBSOCKETREADYSTATE_CLOSING,
      websocket_interface_->GetReadyState(ws));

  result = callback.WaitForResult();
  ASSERT_EQ(PP_OK, result);
  ASSERT_EQ(PP_WEBSOCKETREADYSTATE_CLOSED,
      websocket_interface_->GetReadyState(ws));

  uint64_t base_buffered_amount = websocket_interface_->GetBufferedAmount(ws);

  // After connection closure, all sending requests fail and just increase
  // the bufferedAmount property.
  PP_Var empty_string = CreateVarString("");
  result = websocket_interface_->SendMessage(ws, empty_string);
  ASSERT_EQ(PP_ERROR_FAILED, result);
  buffered_amount = websocket_interface_->GetBufferedAmount(ws);
  ASSERT_EQ(base_buffered_amount + kMessageFrameOverhead, buffered_amount);
  base_buffered_amount = buffered_amount;

  result = websocket_interface_->SendMessage(ws, reason);
  ASSERT_EQ(PP_ERROR_FAILED, result);
  buffered_amount = websocket_interface_->GetBufferedAmount(ws);
  uint64_t reason_frame_size = kMessageFrameOverhead + reason_str.length();
  ASSERT_EQ(base_buffered_amount + reason_frame_size, buffered_amount);

  ReleaseVar(message_var);
  ReleaseVar(reason);
  ReleaseVar(empty_string);
  core_interface_->ReleaseResource(ws);

  PASS();
}

std::string TestWebSocket::TestCcInterfaces() {
  // C++ bindings is simple straightforward, then just verifies interfaces work
  // as a interface bridge fine.
  pp::WebSocket ws(instance_);

  // Check uninitialized properties access.
  ASSERT_EQ(0, ws.GetBufferedAmount());
  ASSERT_EQ(0, ws.GetCloseCode());
  ASSERT_TRUE(AreEqualWithString(ws.GetCloseReason().pp_var(), ""));
  ASSERT_EQ(false, ws.GetCloseWasClean());
  ASSERT_TRUE(AreEqualWithString(ws.GetExtensions().pp_var(), ""));
  ASSERT_TRUE(AreEqualWithString(ws.GetProtocol().pp_var(), ""));
  ASSERT_EQ(PP_WEBSOCKETREADYSTATE_INVALID, ws.GetReadyState());
  ASSERT_TRUE(AreEqualWithString(ws.GetURL().pp_var(), ""));

  // Check communication interfaces (connect, send, receive, and close).
  TestCompletionCallback connect_callback(instance_->pp_instance());
  int32_t result = ws.Connect(pp::Var(GetFullURL(kCloseServerURL)), NULL, 0U,
      connect_callback);
  ASSERT_EQ(PP_OK_COMPLETIONPENDING, result);
  result = connect_callback.WaitForResult();
  ASSERT_EQ(PP_OK, result);

  std::string text_message("hello C++");
  result = ws.SendMessage(pp::Var(text_message));
  ASSERT_EQ(PP_OK, result);

  std::vector<uint8_t> binary(256);
  for (uint32_t i = 0; i < binary.size(); ++i)
    binary[i] = i;
  result = ws.SendMessage(
      pp::Var(pp::PASS_REF, CreateVarBinary(binary)));
  ASSERT_EQ(PP_OK, result);

  pp::Var text_receive_var;
  TestCompletionCallback text_receive_callback(instance_->pp_instance());
  result = ws.ReceiveMessage(&text_receive_var, text_receive_callback);
  if (result == PP_OK_COMPLETIONPENDING)
    result = text_receive_callback.WaitForResult();
  ASSERT_EQ(PP_OK, result);
  ASSERT_TRUE(
      AreEqualWithString(text_receive_var.pp_var(), text_message.c_str()));

  pp::Var binary_receive_var;
  TestCompletionCallback binary_receive_callback(instance_->pp_instance());
  result = ws.ReceiveMessage(&binary_receive_var, binary_receive_callback);
  if (result == PP_OK_COMPLETIONPENDING)
    result = binary_receive_callback.WaitForResult();
  ASSERT_EQ(PP_OK, result);
  ASSERT_TRUE(AreEqualWithBinary(binary_receive_var.pp_var(), binary));

  TestCompletionCallback close_callback(instance_->pp_instance());
  std::string reason("bye");
  result = ws.Close(
      PP_WEBSOCKETSTATUSCODE_NORMAL_CLOSURE, pp::Var(reason), close_callback);
  ASSERT_EQ(PP_OK_COMPLETIONPENDING, result);
  result = close_callback.WaitForResult();
  ASSERT_EQ(PP_OK, result);

  // Check initialized properties access.
  ASSERT_EQ(0, ws.GetBufferedAmount());
  ASSERT_EQ(PP_WEBSOCKETSTATUSCODE_NORMAL_CLOSURE, ws.GetCloseCode());
  ASSERT_TRUE(AreEqualWithString(ws.GetCloseReason().pp_var(), reason.c_str()));
  ASSERT_EQ(true, ws.GetCloseWasClean());
  ASSERT_TRUE(AreEqualWithString(ws.GetProtocol().pp_var(), ""));
  ASSERT_EQ(PP_WEBSOCKETREADYSTATE_CLOSED, ws.GetReadyState());
  ASSERT_TRUE(AreEqualWithString(ws.GetURL().pp_var(), GetFullURL(kCloseServerURL).c_str()));

  PASS();
}

std::string TestWebSocket::TestUtilityInvalidConnect() {
  const pp::Var protocols[] = { pp::Var() };

  TestWebSocketAPI websocket(instance_);
  int32_t result = websocket.Connect(pp::Var(), protocols, 1U);
  ASSERT_EQ(PP_ERROR_BADARGUMENT, result);
  ASSERT_EQ(0U, websocket.GetSeenEvents().size());

  result = websocket.Connect(pp::Var(), protocols, 1U);
  ASSERT_EQ(PP_ERROR_INPROGRESS, result);
  ASSERT_EQ(0U, websocket.GetSeenEvents().size());

  for (int i = 0; kInvalidURLs[i]; ++i) {
    TestWebSocketAPI ws(instance_);
    result = ws.Connect(pp::Var(std::string(kInvalidURLs[i])), protocols, 0U);
    ASSERT_EQ(PP_ERROR_BADARGUMENT, result);
    ASSERT_EQ(0U, ws.GetSeenEvents().size());
  }

  PASS();
}

std::string TestWebSocket::TestUtilityProtocols() {
  const pp::Var bad_protocols[] = {
      pp::Var(std::string("x-test")), pp::Var(std::string("x-test")) };
  const pp::Var good_protocols[] = {
      pp::Var(std::string("x-test")), pp::Var(std::string("x-yatest")) };

  {
    TestWebSocketAPI websocket(instance_);
    int32_t result = websocket.Connect(
        pp::Var(GetFullURL(kEchoServerURL)), bad_protocols, 2U);
    ASSERT_EQ(PP_ERROR_BADARGUMENT, result);
    ASSERT_EQ(0U, websocket.GetSeenEvents().size());
  }

  {
    TestWebSocketAPI websocket(instance_);
    int32_t result = websocket.Connect(
        pp::Var(GetFullURL(kEchoServerURL)), good_protocols, 2U);
    ASSERT_EQ(PP_OK_COMPLETIONPENDING, result);
    websocket.WaitForConnected();
    const std::vector<WebSocketEvent>& events = websocket.GetSeenEvents();
    // Protocol arguments are valid, but this test run without a WebSocket
    // server. As a result, OnError() and OnClose() are invoked because of
    // a connection establishment failure.
    ASSERT_EQ(2U, events.size());
    ASSERT_EQ(WebSocketEvent::EVENT_ERROR, events[0].event_type);
    ASSERT_EQ(WebSocketEvent::EVENT_CLOSE, events[1].event_type);
    ASSERT_FALSE(events[1].was_clean);
  }

  PASS();
}

std::string TestWebSocket::TestUtilityGetURL() {
  const pp::Var protocols[] = { pp::Var() };

  for (int i = 0; kInvalidURLs[i]; ++i) {
    TestWebSocketAPI websocket(instance_);
    int32_t result = websocket.Connect(
        pp::Var(std::string(kInvalidURLs[i])), protocols, 0U);
    ASSERT_EQ(PP_ERROR_BADARGUMENT, result);
    pp::Var url = websocket.GetURL();
    ASSERT_TRUE(AreEqualWithString(url.pp_var(), kInvalidURLs[i]));
    ASSERT_EQ(0U, websocket.GetSeenEvents().size());
  }

  PASS();
}

std::string TestWebSocket::TestUtilityValidConnect() {
  const pp::Var protocols[] = { pp::Var() };
  TestWebSocketAPI websocket(instance_);
  int32_t result = websocket.Connect(
      pp::Var(GetFullURL(kEchoServerURL)), protocols, 0U);
  ASSERT_EQ(PP_OK_COMPLETIONPENDING, result);
  websocket.WaitForConnected();
  const std::vector<WebSocketEvent>& events = websocket.GetSeenEvents();
  ASSERT_EQ(1U, events.size());
  ASSERT_EQ(WebSocketEvent::EVENT_OPEN, events[0].event_type);
  ASSERT_TRUE(AreEqualWithString(websocket.GetExtensions().pp_var(), ""));

  PASS();
}

std::string TestWebSocket::TestUtilityInvalidClose() {
  const pp::Var reason = pp::Var(std::string("close for test"));

  // Close before connect.
  {
    TestWebSocketAPI websocket(instance_);
    int32_t result = websocket.Close(
        PP_WEBSOCKETSTATUSCODE_NORMAL_CLOSURE, reason);
    ASSERT_EQ(PP_ERROR_FAILED, result);
    ASSERT_EQ(0U, websocket.GetSeenEvents().size());
  }

  // Close with bad arguments.
  {
    TestWebSocketAPI websocket(instance_);
    int32_t result = websocket.Connect(pp::Var(GetFullURL(kEchoServerURL)),
        NULL, 0);
    ASSERT_EQ(PP_OK_COMPLETIONPENDING, result);
    websocket.WaitForConnected();
    result = websocket.Close(1U, reason);
    ASSERT_EQ(PP_ERROR_NOACCESS, result);
    const std::vector<WebSocketEvent>& events = websocket.GetSeenEvents();
    ASSERT_EQ(1U, events.size());
    ASSERT_EQ(WebSocketEvent::EVENT_OPEN, events[0].event_type);
  }

  PASS();
}

std::string TestWebSocket::TestUtilityValidClose() {
  std::string reason("close for test");
  pp::Var url = pp::Var(GetFullURL(kCloseServerURL));

  // Close.
  {
    TestWebSocketAPI websocket(instance_);
    int32_t result = websocket.Connect(url, NULL, 0U);
    ASSERT_EQ(PP_OK_COMPLETIONPENDING, result);
    websocket.WaitForConnected();
    result = websocket.Close(
        PP_WEBSOCKETSTATUSCODE_NORMAL_CLOSURE, pp::Var(reason));
    ASSERT_EQ(PP_OK_COMPLETIONPENDING, result);
    websocket.WaitForClosed();
    const std::vector<WebSocketEvent>& events = websocket.GetSeenEvents();
    ASSERT_EQ(2U, events.size());
    ASSERT_EQ(WebSocketEvent::EVENT_OPEN, events[0].event_type);
    ASSERT_EQ(WebSocketEvent::EVENT_CLOSE, events[1].event_type);
    ASSERT_TRUE(events[1].was_clean);
    ASSERT_EQ(PP_WEBSOCKETSTATUSCODE_NORMAL_CLOSURE, events[1].close_code);
    ASSERT_TRUE(AreEqualWithString(events[1].var.pp_var(), reason.c_str()));
  }

  // Close in connecting.
  // The ongoing connect failed with PP_ERROR_ABORTED, then the close is done
  // successfully.
  {
    TestWebSocketAPI websocket(instance_);
    int32_t result = websocket.Connect(url, NULL, 0U);
    ASSERT_EQ(PP_OK_COMPLETIONPENDING, result);
    result = websocket.Close(
        PP_WEBSOCKETSTATUSCODE_NORMAL_CLOSURE, pp::Var(reason));
    ASSERT_EQ(PP_OK_COMPLETIONPENDING, result);
    websocket.WaitForClosed();
    const std::vector<WebSocketEvent>& events = websocket.GetSeenEvents();
    ASSERT_TRUE(events.size() == 2 || events.size() == 3);
    int index = 0;
    if (events.size() == 3)
      ASSERT_EQ(WebSocketEvent::EVENT_OPEN, events[index++].event_type);
    ASSERT_EQ(WebSocketEvent::EVENT_ERROR, events[index++].event_type);
    ASSERT_EQ(WebSocketEvent::EVENT_CLOSE, events[index].event_type);
    ASSERT_FALSE(events[index].was_clean);
  }

  // Close in closing.
  // The first close will be done successfully, then the second one failed with
  // with PP_ERROR_INPROGRESS immediately.
  {
    TestWebSocketAPI websocket(instance_);
    int32_t result = websocket.Connect(url, NULL, 0U);
    result = websocket.Close(
        PP_WEBSOCKETSTATUSCODE_NORMAL_CLOSURE, pp::Var(reason));
    ASSERT_EQ(PP_OK_COMPLETIONPENDING, result);
    result = websocket.Close(
        PP_WEBSOCKETSTATUSCODE_NORMAL_CLOSURE, pp::Var(reason));
    ASSERT_EQ(PP_ERROR_INPROGRESS, result);
    websocket.WaitForClosed();
    const std::vector<WebSocketEvent>& events = websocket.GetSeenEvents();
    ASSERT_TRUE(events.size() == 2 || events.size() == 3)
    int index = 0;
    if (events.size() == 3)
      ASSERT_EQ(WebSocketEvent::EVENT_OPEN, events[index++].event_type);
    ASSERT_EQ(WebSocketEvent::EVENT_ERROR, events[index++].event_type);
    ASSERT_EQ(WebSocketEvent::EVENT_CLOSE, events[index].event_type);
    ASSERT_FALSE(events[index].was_clean);
  }

  PASS();
}

std::string TestWebSocket::TestUtilityGetProtocol() {
  const std::string protocol("x-chat");
  const pp::Var protocols[] = { pp::Var(protocol) };
  std::string url(GetFullURL(kProtocolTestServerURL));
  url += protocol;
  TestWebSocketAPI websocket(instance_);
  int32_t result = websocket.Connect(pp::Var(url), protocols, 1U);
  ASSERT_EQ(PP_OK_COMPLETIONPENDING, result);
  websocket.WaitForReceived();
  ASSERT_TRUE(AreEqualWithString(
      websocket.GetProtocol().pp_var(), protocol.c_str()));
  const std::vector<WebSocketEvent>& events = websocket.GetSeenEvents();
  // The server to which this test connect returns the decided protocol as a
  // text frame message. So the WebSocketEvent records EVENT_MESSAGE event
  // after EVENT_OPEN event.
  ASSERT_EQ(2U, events.size());
  ASSERT_EQ(WebSocketEvent::EVENT_OPEN, events[0].event_type);
  ASSERT_EQ(WebSocketEvent::EVENT_MESSAGE, events[1].event_type);
  ASSERT_TRUE(AreEqualWithString(events[1].var.pp_var(), protocol.c_str()));
  ASSERT_TRUE(events[1].was_clean);

  PASS();
}

std::string TestWebSocket::TestUtilityTextSendReceive() {
  const pp::Var protocols[] = { pp::Var() };
  TestWebSocketAPI websocket(instance_);
  int32_t result =
      websocket.Connect(pp::Var(GetFullURL(kEchoServerURL)), protocols, 0U);
  ASSERT_EQ(PP_OK_COMPLETIONPENDING, result);
  websocket.WaitForConnected();

  // Send 'hello pepper'.
  std::string message1("hello pepper");
  result = websocket.Send(pp::Var(std::string(message1)));
  ASSERT_EQ(PP_OK, result);

  // Receive echoed 'hello pepper'.
  websocket.WaitForReceived();

  // Send 'goodbye pepper'.
  std::string message2("goodbye pepper");
  result = websocket.Send(pp::Var(std::string(message2)));

  // Receive echoed 'goodbye pepper'.
  websocket.WaitForReceived();

  const std::vector<WebSocketEvent>& events = websocket.GetSeenEvents();
  ASSERT_EQ(3U, events.size());
  ASSERT_EQ(WebSocketEvent::EVENT_OPEN, events[0].event_type);
  ASSERT_EQ(WebSocketEvent::EVENT_MESSAGE, events[1].event_type);
  ASSERT_TRUE(AreEqualWithString(events[1].var.pp_var(), message1.c_str()));
  ASSERT_EQ(WebSocketEvent::EVENT_MESSAGE, events[2].event_type);
  ASSERT_TRUE(AreEqualWithString(events[2].var.pp_var(), message2.c_str()));

  PASS();
}

std::string TestWebSocket::TestUtilityBinarySendReceive() {
  const pp::Var protocols[] = { pp::Var() };
  TestWebSocketAPI websocket(instance_);
  int32_t result =
      websocket.Connect(pp::Var(GetFullURL(kEchoServerURL)), protocols, 0U);
  ASSERT_EQ(PP_OK_COMPLETIONPENDING, result);
  websocket.WaitForConnected();

  // Send binary message.
  uint32_t len = 256;
  std::vector<uint8_t> binary(len);
  for (uint32_t i = 0; i < len; ++i)
    binary[i] = i;
  pp::VarArrayBuffer message(len);
  uint8_t* var_data = static_cast<uint8_t*>(message.Map());
  std::copy(binary.begin(), binary.end(), var_data);
  result = websocket.Send(message);
  ASSERT_EQ(PP_OK, result);

  // Receive echoed binary message.
  websocket.WaitForReceived();

  const std::vector<WebSocketEvent>& events = websocket.GetSeenEvents();
  ASSERT_EQ(2U, events.size());
  ASSERT_EQ(WebSocketEvent::EVENT_OPEN, events[0].event_type);
  ASSERT_EQ(WebSocketEvent::EVENT_MESSAGE, events[1].event_type);
  ASSERT_TRUE(AreEqualWithBinary(events[1].var.pp_var(), binary));

  PASS();
}

std::string TestWebSocket::TestUtilityBufferedAmount() {
  // Connect to test echo server.
  const pp::Var protocols[] = { pp::Var() };
  TestWebSocketAPI websocket(instance_);
  int32_t result =
      websocket.Connect(pp::Var(GetFullURL(kEchoServerURL)), protocols, 0U);
  ASSERT_EQ(PP_OK_COMPLETIONPENDING, result);
  websocket.WaitForConnected();

  // Prepare a large message that is not aligned with the internal buffer
  // sizes.
  std::string message(8193, 'x');
  uint64_t buffered_amount = 0;
  uint32_t sent;
  for (sent = 0; sent < 100; sent++) {
    result = websocket.Send(pp::Var(message));
    ASSERT_EQ(PP_OK, result);
    buffered_amount = websocket.GetBufferedAmount();
    // Buffered amount size 262144 is too big for the internal buffer size.
    if (buffered_amount > 262144)
      break;
  }

  // Close connection.
  std::string reason = "close while busy";
  result = websocket.Close(
      PP_WEBSOCKETSTATUSCODE_NORMAL_CLOSURE, pp::Var(reason));
  ASSERT_EQ(PP_WEBSOCKETREADYSTATE_CLOSING, websocket.GetReadyState());
  websocket.WaitForClosed();
  ASSERT_EQ(PP_WEBSOCKETREADYSTATE_CLOSED, websocket.GetReadyState());

  uint64_t base_buffered_amount = websocket.GetBufferedAmount();
  size_t events_on_closed = websocket.GetSeenEvents().size();

  // After connection closure, all sending requests fail and just increase
  // the bufferedAmount property.
  result = websocket.Send(pp::Var(std::string("")));
  ASSERT_EQ(PP_ERROR_FAILED, result);
  buffered_amount = websocket.GetBufferedAmount();
  ASSERT_EQ(base_buffered_amount + kMessageFrameOverhead, buffered_amount);
  base_buffered_amount = buffered_amount;

  result = websocket.Send(pp::Var(reason));
  ASSERT_EQ(PP_ERROR_FAILED, result);
  buffered_amount = websocket.GetBufferedAmount();
  uint64_t reason_frame_size = kMessageFrameOverhead + reason.length();
  ASSERT_EQ(base_buffered_amount + reason_frame_size, buffered_amount);

  const std::vector<WebSocketEvent>& events = websocket.GetSeenEvents();
  ASSERT_EQ(events_on_closed, events.size());
  ASSERT_EQ(WebSocketEvent::EVENT_OPEN, events[0].event_type);
  size_t last_event = events_on_closed - 1;
  for (uint32_t i = 1; i < last_event; ++i) {
    ASSERT_EQ(WebSocketEvent::EVENT_MESSAGE, events[i].event_type);
    ASSERT_TRUE(AreEqualWithString(events[i].var.pp_var(), message));
  }
  ASSERT_EQ(WebSocketEvent::EVENT_CLOSE, events[last_event].event_type);
  ASSERT_TRUE(events[last_event].was_clean);

  PASS();
}
