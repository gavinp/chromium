// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/transport_client_socket_pool.h"

#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "base/metrics/histogram.h"
#include "base/string_util.h"
#include "base/time.h"
#include "net/base/net_log.h"
#include "net/base/net_errors.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/client_socket_pool_base.h"
#include "net/socket/tcp_client_socket.h"

using base::TimeDelta;

namespace net {

TransportSocketParams::TransportSocketParams(const HostPortPair& host_port_pair,
                                 RequestPriority priority, const GURL& referrer,
                                 bool disable_resolver_cache,
                                 bool ignore_limits)
    : destination_(host_port_pair), ignore_limits_(ignore_limits) {
  Initialize(priority, referrer, disable_resolver_cache);
}

TransportSocketParams::~TransportSocketParams() {}

void TransportSocketParams::Initialize(RequestPriority priority,
                                 const GURL& referrer,
                                 bool disable_resolver_cache) {
  // The referrer is used by the DNS prefetch system to correlate resolutions
  // with the page that triggered them. It doesn't impact the actual addresses
  // that we resolve to.
  destination_.set_referrer(referrer);
  destination_.set_priority(priority);
  if (disable_resolver_cache)
    destination_.set_allow_cached_response(false);
}

// TransportConnectJobs will time out after this many seconds.  Note this is
// the total time, including both host resolution and TCP connect() times.
//
// TODO(eroman): The use of this constant needs to be re-evaluated. The time
// needed for TCPClientSocketXXX::Connect() can be arbitrarily long, since
// the address list may contain many alternatives, and most of those may
// timeout. Even worse, the per-connect timeout threshold varies greatly
// between systems (anywhere from 20 seconds to 190 seconds).
// See comment #12 at http://crbug.com/23364 for specifics.
static const int kTransportConnectJobTimeoutInSeconds = 240;  // 4 minutes.

TransportConnectJob::TransportConnectJob(
    const std::string& group_name,
    const scoped_refptr<TransportSocketParams>& params,
    base::TimeDelta timeout_duration,
    ClientSocketFactory* client_socket_factory,
    HostResolver* host_resolver,
    Delegate* delegate,
    NetLog* net_log)
    : ConnectJob(group_name, timeout_duration, delegate,
                 BoundNetLog::Make(net_log, NetLog::SOURCE_CONNECT_JOB)),
      params_(params),
      client_socket_factory_(client_socket_factory),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          callback_(this,
                    &TransportConnectJob::OnIOComplete)),
      resolver_(host_resolver) {}

TransportConnectJob::~TransportConnectJob() {
  // We don't worry about cancelling the host resolution and TCP connect, since
  // ~SingleRequestHostResolver and ~ClientSocket will take care of it.
}

LoadState TransportConnectJob::GetLoadState() const {
  switch (next_state_) {
    case STATE_RESOLVE_HOST:
    case STATE_RESOLVE_HOST_COMPLETE:
      return LOAD_STATE_RESOLVING_HOST;
    case STATE_TRANSPORT_CONNECT:
    case STATE_TRANSPORT_CONNECT_COMPLETE:
      return LOAD_STATE_CONNECTING;
    default:
      NOTREACHED();
      return LOAD_STATE_IDLE;
  }
}

void TransportConnectJob::OnIOComplete(int result) {
  int rv = DoLoop(result);
  if (rv != ERR_IO_PENDING)
    NotifyDelegateOfCompletion(rv);  // Deletes |this|
}

int TransportConnectJob::DoLoop(int result) {
  DCHECK_NE(next_state_, STATE_NONE);

  int rv = result;
  do {
    State state = next_state_;
    next_state_ = STATE_NONE;
    switch (state) {
      case STATE_RESOLVE_HOST:
        DCHECK_EQ(OK, rv);
        rv = DoResolveHost();
        break;
      case STATE_RESOLVE_HOST_COMPLETE:
        rv = DoResolveHostComplete(rv);
        break;
      case STATE_TRANSPORT_CONNECT:
        DCHECK_EQ(OK, rv);
        rv = DoTransportConnect();
        break;
      case STATE_TRANSPORT_CONNECT_COMPLETE:
        rv = DoTransportConnectComplete(rv);
        break;
      default:
        NOTREACHED();
        rv = ERR_FAILED;
        break;
    }
  } while (rv != ERR_IO_PENDING && next_state_ != STATE_NONE);

  return rv;
}

int TransportConnectJob::DoResolveHost() {
  next_state_ = STATE_RESOLVE_HOST_COMPLETE;
  return resolver_.Resolve(params_->destination(), &addresses_, &callback_,
                           net_log());
}

int TransportConnectJob::DoResolveHostComplete(int result) {
  if (result == OK)
    next_state_ = STATE_TRANSPORT_CONNECT;
  return result;
}

int TransportConnectJob::DoTransportConnect() {
  next_state_ = STATE_TRANSPORT_CONNECT_COMPLETE;
  set_socket(client_socket_factory_->CreateTransportClientSocket(
        addresses_, net_log().net_log(), net_log().source()));
  connect_start_time_ = base::TimeTicks::Now();
  return socket()->Connect(&callback_);
}

int TransportConnectJob::DoTransportConnectComplete(int result) {
  if (result == OK) {
    DCHECK(connect_start_time_ != base::TimeTicks());
    DCHECK(start_time_ != base::TimeTicks());
    base::TimeTicks now = base::TimeTicks::Now();
    base::TimeDelta total_duration = now - start_time_;
    UMA_HISTOGRAM_CUSTOM_TIMES(
        "Net.DNS_Resolution_And_TCP_Connection_Latency2",
        total_duration,
        base::TimeDelta::FromMilliseconds(1),
        base::TimeDelta::FromMinutes(10),
        100);

    base::TimeDelta connect_duration = now - connect_start_time_;
    UMA_HISTOGRAM_CUSTOM_TIMES("Net.TCP_Connection_Latency",
        connect_duration,
        base::TimeDelta::FromMilliseconds(1),
        base::TimeDelta::FromMinutes(10),
        100);
  } else {
    // Delete the socket on error.
    set_socket(NULL);
  }

  return result;
}

int TransportConnectJob::ConnectInternal() {
  next_state_ = STATE_RESOLVE_HOST;
  start_time_ = base::TimeTicks::Now();
  return DoLoop(OK);
}

ConnectJob*
    TransportClientSocketPool::TransportConnectJobFactory::NewConnectJob(
    const std::string& group_name,
    const PoolBase::Request& request,
    ConnectJob::Delegate* delegate) const {
  return new TransportConnectJob(group_name,
                                 request.params(),
                                 ConnectionTimeout(),
                                 client_socket_factory_,
                                 host_resolver_,
                                 delegate,
                                 net_log_);
}

base::TimeDelta
    TransportClientSocketPool::TransportConnectJobFactory::ConnectionTimeout()
    const {
  return base::TimeDelta::FromSeconds(kTransportConnectJobTimeoutInSeconds);
}

TransportClientSocketPool::TransportClientSocketPool(
    int max_sockets,
    int max_sockets_per_group,
    ClientSocketPoolHistograms* histograms,
    HostResolver* host_resolver,
    ClientSocketFactory* client_socket_factory,
    NetLog* net_log)
    : base_(max_sockets, max_sockets_per_group, histograms,
            base::TimeDelta::FromSeconds(
                ClientSocketPool::unused_idle_socket_timeout()),
            base::TimeDelta::FromSeconds(kUsedIdleSocketTimeout),
            new TransportConnectJobFactory(client_socket_factory,
                                     host_resolver, net_log)) {
  base_.EnableConnectBackupJobs();
}

TransportClientSocketPool::~TransportClientSocketPool() {}

int TransportClientSocketPool::RequestSocket(
    const std::string& group_name,
    const void* params,
    RequestPriority priority,
    ClientSocketHandle* handle,
    CompletionCallback* callback,
    const BoundNetLog& net_log) {
  const scoped_refptr<TransportSocketParams>* casted_params =
      static_cast<const scoped_refptr<TransportSocketParams>*>(params);

  if (net_log.IsLoggingAllEvents()) {
    // TODO(eroman): Split out the host and port parameters.
    net_log.AddEvent(
        NetLog::TYPE_TCP_CLIENT_SOCKET_POOL_REQUESTED_SOCKET,
        make_scoped_refptr(new NetLogStringParameter(
            "host_and_port",
            casted_params->get()->destination().host_port_pair().ToString())));
  }

  return base_.RequestSocket(group_name, *casted_params, priority, handle,
                             callback, net_log);
}

void TransportClientSocketPool::RequestSockets(
    const std::string& group_name,
    const void* params,
    int num_sockets,
    const BoundNetLog& net_log) {
  const scoped_refptr<TransportSocketParams>* casted_params =
      static_cast<const scoped_refptr<TransportSocketParams>*>(params);

  if (net_log.IsLoggingAllEvents()) {
    // TODO(eroman): Split out the host and port parameters.
    net_log.AddEvent(
        NetLog::TYPE_TCP_CLIENT_SOCKET_POOL_REQUESTED_SOCKETS,
        make_scoped_refptr(new NetLogStringParameter(
            "host_and_port",
            casted_params->get()->destination().host_port_pair().ToString())));
  }

  base_.RequestSockets(group_name, *casted_params, num_sockets, net_log);
}

void TransportClientSocketPool::CancelRequest(
    const std::string& group_name,
    ClientSocketHandle* handle) {
  base_.CancelRequest(group_name, handle);
}

void TransportClientSocketPool::ReleaseSocket(
    const std::string& group_name,
    ClientSocket* socket,
    int id) {
  base_.ReleaseSocket(group_name, socket, id);
}

void TransportClientSocketPool::Flush() {
  base_.Flush();
}

void TransportClientSocketPool::CloseIdleSockets() {
  base_.CloseIdleSockets();
}

int TransportClientSocketPool::IdleSocketCount() const {
  return base_.idle_socket_count();
}

int TransportClientSocketPool::IdleSocketCountInGroup(
    const std::string& group_name) const {
  return base_.IdleSocketCountInGroup(group_name);
}

LoadState TransportClientSocketPool::GetLoadState(
    const std::string& group_name, const ClientSocketHandle* handle) const {
  return base_.GetLoadState(group_name, handle);
}

DictionaryValue* TransportClientSocketPool::GetInfoAsValue(
    const std::string& name,
    const std::string& type,
    bool include_nested_pools) const {
  return base_.GetInfoAsValue(name, type);
}

base::TimeDelta TransportClientSocketPool::ConnectionTimeout() const {
  return base_.ConnectionTimeout();
}

ClientSocketPoolHistograms* TransportClientSocketPool::histograms() const {
  return base_.histograms();
}

}  // namespace net
