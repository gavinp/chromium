// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_CLIENT_SESSION_H_
#define REMOTING_HOST_CLIENT_SESSION_H_

#include <list>

#include "base/time.h"
#include "base/threading/non_thread_safe.h"
#include "remoting/host/remote_input_filter.h"
#include "remoting/protocol/clipboard_stub.h"
#include "remoting/protocol/connection_to_client.h"
#include "remoting/protocol/host_event_stub.h"
#include "remoting/protocol/host_stub.h"
#include "remoting/protocol/input_event_tracker.h"
#include "remoting/protocol/input_filter.h"
#include "remoting/protocol/input_stub.h"
#include "third_party/skia/include/core/SkPoint.h"

namespace remoting {

class Capturer;

// A ClientSession keeps a reference to a connection to a client, and maintains
// per-client state.
class ClientSession : public protocol::HostEventStub,
                      public protocol::HostStub,
                      public protocol::ConnectionToClient::EventHandler,
                      public base::NonThreadSafe {
 public:
  // Callback interface for passing events to the ChromotingHost.
  class EventHandler {
   public:
    virtual ~EventHandler() {}

    // Called after authentication has finished successfully.
    virtual void OnSessionAuthenticated(ClientSession* client) = 0;

    // Called after we've finished connecting all channels.
    virtual void OnSessionChannelsConnected(ClientSession* client) = 0;

    // Called after authentication has failed. Must not tear down this
    // object. OnSessionClosed() is notified after this handler
    // returns.
    virtual void OnSessionAuthenticationFailed(ClientSession* client) = 0;

    // Called after connection has failed or after the client closed it.
    virtual void OnSessionClosed(ClientSession* client) = 0;

    // Called to notify of each message's sequence number. The
    // callback must not tear down this object.
    virtual void OnSessionSequenceNumber(ClientSession* client,
                                         int64 sequence_number) = 0;

    // Called on notification of a route change event, when a channel is
    // connected.
    virtual void OnSessionRouteChange(
        ClientSession* client,
        const std::string& channel_name,
        const protocol::TransportRoute& route) = 0;
  };

  ClientSession(EventHandler* event_handler,
                scoped_ptr<protocol::ConnectionToClient> connection,
                protocol::HostEventStub* host_event_stub,
                Capturer* capturer);
  virtual ~ClientSession();

  // protocol::ClipboardStub interface.
  virtual void InjectClipboardEvent(
      const protocol::ClipboardEvent& event) OVERRIDE;

  // protocol::InputStub interface.
  virtual void InjectKeyEvent(const protocol::KeyEvent& event) OVERRIDE;
  virtual void InjectMouseEvent(const protocol::MouseEvent& event) OVERRIDE;

  // protocol::ConnectionToClient::EventHandler interface.
  virtual void OnConnectionAuthenticated(
      protocol::ConnectionToClient* connection) OVERRIDE;
  virtual void OnConnectionChannelsConnected(
      protocol::ConnectionToClient* connection) OVERRIDE;
  virtual void OnConnectionClosed(protocol::ConnectionToClient* connection,
                                  protocol::ErrorCode error) OVERRIDE;
  virtual void OnSequenceNumberUpdated(
      protocol::ConnectionToClient* connection, int64 sequence_number) OVERRIDE;
  virtual void OnRouteChange(
      protocol::ConnectionToClient* connection,
      const std::string& channel_name,
      const protocol::TransportRoute& route) OVERRIDE;

  // Disconnects the session and destroys the transport. Event handler
  // is guaranteed not to be called after this method is called. Can
  // be called multiple times. The object should not be used after
  // this method returns.
  void Disconnect();

  protocol::ConnectionToClient* connection() const {
    return connection_.get();
  }

  const std::string& client_jid() { return client_jid_; }

  // Indicate that local mouse activity has been detected. This causes remote
  // inputs to be ignored for a short time so that the local user will always
  // have the upper hand in 'pointer wars'.
  void LocalMouseMoved(const SkIPoint& new_pos);

  // Disable handling of input events from this client. If the client has any
  // keys or mouse buttons pressed then these will be released.
  void SetDisableInputs(bool disable_inputs);

 private:
  EventHandler* event_handler_;

  // The connection to the client.
  scoped_ptr<protocol::ConnectionToClient> connection_;

  std::string client_jid_;

  // The host event stub to which this object delegates.
  protocol::HostEventStub* host_event_stub_;

  // Tracker used to release pressed keys and buttons when disconnecting.
  protocol::InputEventTracker input_tracker_;

  // Filter used to disable remote inputs during local input activity.
  RemoteInputFilter remote_input_filter_;

  // Filter used to manage enabling & disabling of client input events.
  protocol::InputFilter disable_input_filter_;

  // Filter used to disable inputs when we're not authenticated.
  protocol::InputFilter auth_input_filter_;

  // Capturer, used to determine current screen size for ensuring injected
  // mouse events fall within the screen area.
  // TODO(lambroslambrou): Move floor-control logic, and clamping to screen
  // area, out of this class (crbug.com/96508).
  Capturer* capturer_;

  DISALLOW_COPY_AND_ASSIGN(ClientSession);
};

}  // namespace remoting

#endif  // REMOTING_HOST_CLIENT_SESSION_H_
