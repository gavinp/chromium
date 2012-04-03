// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/client_session.h"

#include <algorithm>

#include "base/message_loop_proxy.h"
#include "remoting/host/capturer.h"
#include "remoting/proto/event.pb.h"

namespace remoting {

using protocol::KeyEvent;
using protocol::MouseEvent;

ClientSession::ClientSession(
    EventHandler* event_handler,
    scoped_ptr<protocol::ConnectionToClient> connection,
    protocol::HostEventStub* host_event_stub,
    Capturer* capturer)
    : event_handler_(event_handler),
      connection_(connection.Pass()),
      client_jid_(connection_->session()->jid()),
      host_event_stub_(host_event_stub),
      input_tracker_(host_event_stub),
      remote_input_filter_(&input_tracker_),
      capturer_(capturer) {
  connection_->SetEventHandler(this);

  // TODO(sergeyu): Currently ConnectionToClient expects stubs to be
  // set before channels are connected. Make it possible to set stubs
  // later and set them only when connection is authenticated.
  connection_->set_clipboard_stub(this);
  connection_->set_host_stub(this);
  connection_->set_input_stub(&auth_input_filter_);
}

ClientSession::~ClientSession() {
}

void ClientSession::InjectClipboardEvent(
    const protocol::ClipboardEvent& event) {
  DCHECK(CalledOnValidThread());

  // TODO(wez): Disable clipboard in both directions on local activity, and
  // replace these tests with a HostInputFilter (or ClipboardFilter).
  if (auth_input_filter_.input_stub() == NULL)
    return;
  if (disable_input_filter_.input_stub() == NULL)
    return;

  host_event_stub_->InjectClipboardEvent(event);
}

void ClientSession::InjectKeyEvent(const KeyEvent& event) {
  DCHECK(CalledOnValidThread());
  auth_input_filter_.InjectKeyEvent(event);
}

void ClientSession::InjectMouseEvent(const MouseEvent& event) {
  DCHECK(CalledOnValidThread());

  MouseEvent event_to_inject = event;
  if (event.has_x() && event.has_y()) {
    // In case the client sends events with off-screen coordinates, modify
    // the event to lie within the current screen area.  This is better than
    // simply discarding the event, which might lose a button-up event at the
    // end of a drag'n'drop (or cause other related problems).
    SkIPoint pos(SkIPoint::Make(event.x(), event.y()));
    const SkISize& screen = capturer_->size_most_recent();
    pos.setX(std::max(0, std::min(screen.width() - 1, pos.x())));
    pos.setY(std::max(0, std::min(screen.height() - 1, pos.y())));
    event_to_inject.set_x(pos.x());
    event_to_inject.set_y(pos.y());
  }
  auth_input_filter_.InjectMouseEvent(event_to_inject);
}

void ClientSession::OnConnectionAuthenticated(
    protocol::ConnectionToClient* connection) {
  DCHECK(CalledOnValidThread());
  DCHECK_EQ(connection_.get(), connection);
  auth_input_filter_.set_input_stub(&disable_input_filter_);
  event_handler_->OnSessionAuthenticated(this);
}

void ClientSession::OnConnectionChannelsConnected(
    protocol::ConnectionToClient* connection) {
  DCHECK(CalledOnValidThread());
  DCHECK_EQ(connection_.get(), connection);
  SetDisableInputs(false);
  event_handler_->OnSessionChannelsConnected(this);
}

void ClientSession::OnConnectionClosed(
    protocol::ConnectionToClient* connection,
    protocol::ErrorCode error) {
  DCHECK(CalledOnValidThread());
  DCHECK_EQ(connection_.get(), connection);
  if (!auth_input_filter_.input_stub())
    event_handler_->OnSessionAuthenticationFailed(this);
  auth_input_filter_.set_input_stub(NULL);

  // Ensure that any pressed keys or buttons are released.
  input_tracker_.ReleaseAll();

  // TODO(sergeyu): Log failure reason?
  event_handler_->OnSessionClosed(this);
}

void ClientSession::OnSequenceNumberUpdated(
    protocol::ConnectionToClient* connection, int64 sequence_number) {
  DCHECK(CalledOnValidThread());
  DCHECK_EQ(connection_.get(), connection);
  event_handler_->OnSessionSequenceNumber(this, sequence_number);
}

void ClientSession::OnRouteChange(
    protocol::ConnectionToClient* connection,
    const std::string& channel_name,
    const protocol::TransportRoute& route) {
  DCHECK(CalledOnValidThread());
  DCHECK_EQ(connection_.get(), connection);
  event_handler_->OnSessionRouteChange(this, channel_name, route);
}

void ClientSession::Disconnect() {
  DCHECK(CalledOnValidThread());
  DCHECK(connection_.get());

  // This triggers OnConnectionClosed(), and the session may be destroyed
  // as the result, so this call must be the last in this method.
  connection_->Disconnect();
}

void ClientSession::LocalMouseMoved(const SkIPoint& mouse_pos) {
  DCHECK(CalledOnValidThread());
  remote_input_filter_.LocalMouseMoved(mouse_pos);
}

void ClientSession::SetDisableInputs(bool disable_inputs) {
  DCHECK(CalledOnValidThread());

  if (disable_inputs) {
    disable_input_filter_.set_input_stub(NULL);
    input_tracker_.ReleaseAll();
  } else {
    disable_input_filter_.set_input_stub(&remote_input_filter_);
  }
}

}  // namespace remoting
