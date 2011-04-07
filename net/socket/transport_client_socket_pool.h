// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_TRANSPORT_CLIENT_SOCKET_POOL_H_
#define NET_SOCKET_TRANSPORT_CLIENT_SOCKET_POOL_H_
#pragma once

#include <string>

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/time.h"
#include "base/timer.h"
#include "net/base/host_port_pair.h"
#include "net/base/host_resolver.h"
#include "net/socket/client_socket_pool_base.h"
#include "net/socket/client_socket_pool_histograms.h"
#include "net/socket/client_socket_pool.h"

namespace net {

class ClientSocketFactory;

class TransportSocketParams : public base::RefCounted<TransportSocketParams> {
 public:
  TransportSocketParams(const HostPortPair& host_port_pair,
                        RequestPriority priority,
                        const GURL& referrer,
                        bool disable_resolver_cache,
                        bool ignore_limits);

  const HostResolver::RequestInfo& destination() const { return destination_; }
  bool ignore_limits() const { return ignore_limits_; }

 private:
  friend class base::RefCounted<TransportSocketParams>;
  ~TransportSocketParams();

  void Initialize(RequestPriority priority, const GURL& referrer,
                  bool disable_resolver_cache);

  HostResolver::RequestInfo destination_;
  bool ignore_limits_;

  DISALLOW_COPY_AND_ASSIGN(TransportSocketParams);
};

// TransportConnectJob handles the host resolution necessary for socket creation
// and the transport (likely TCP) connect.
class TransportConnectJob : public ConnectJob {
 public:
  TransportConnectJob(const std::string& group_name,
                const scoped_refptr<TransportSocketParams>& params,
                base::TimeDelta timeout_duration,
                ClientSocketFactory* client_socket_factory,
                HostResolver* host_resolver,
                Delegate* delegate,
                NetLog* net_log);
  virtual ~TransportConnectJob();

  // ConnectJob methods.
  virtual LoadState GetLoadState() const;

 private:
  enum State {
    STATE_RESOLVE_HOST,
    STATE_RESOLVE_HOST_COMPLETE,
    STATE_TRANSPORT_CONNECT,
    STATE_TRANSPORT_CONNECT_COMPLETE,
    STATE_NONE,
  };

  void OnIOComplete(int result);

  // Runs the state transition loop.
  int DoLoop(int result);

  int DoResolveHost();
  int DoResolveHostComplete(int result);
  int DoTransportConnect();
  int DoTransportConnectComplete(int result);

  // Begins the host resolution and the TCP connect.  Returns OK on success
  // and ERR_IO_PENDING if it cannot immediately service the request.
  // Otherwise, it returns a net error code.
  virtual int ConnectInternal();

  scoped_refptr<TransportSocketParams> params_;
  ClientSocketFactory* const client_socket_factory_;
  CompletionCallbackImpl<TransportConnectJob> callback_;
  SingleRequestHostResolver resolver_;
  AddressList addresses_;
  State next_state_;

  // The time Connect() was called.
  base::TimeTicks start_time_;

  // The time the connect was started (after DNS finished).
  base::TimeTicks connect_start_time_;

  DISALLOW_COPY_AND_ASSIGN(TransportConnectJob);
};

class TransportClientSocketPool : public ClientSocketPool {
 public:
  TransportClientSocketPool(
      int max_sockets,
      int max_sockets_per_group,
      ClientSocketPoolHistograms* histograms,
      HostResolver* host_resolver,
      ClientSocketFactory* client_socket_factory,
      NetLog* net_log);

  virtual ~TransportClientSocketPool();

  // ClientSocketPool methods:

  virtual int RequestSocket(const std::string& group_name,
                            const void* resolve_info,
                            RequestPriority priority,
                            ClientSocketHandle* handle,
                            CompletionCallback* callback,
                            const BoundNetLog& net_log);

  virtual void RequestSockets(const std::string& group_name,
                              const void* params,
                              int num_sockets,
                              const BoundNetLog& net_log);

  virtual void CancelRequest(const std::string& group_name,
                             ClientSocketHandle* handle);

  virtual void ReleaseSocket(const std::string& group_name,
                             ClientSocket* socket,
                             int id);

  virtual void Flush();

  virtual void CloseIdleSockets();

  virtual int IdleSocketCount() const;

  virtual int IdleSocketCountInGroup(const std::string& group_name) const;

  virtual LoadState GetLoadState(const std::string& group_name,
                                 const ClientSocketHandle* handle) const;

  virtual DictionaryValue* GetInfoAsValue(const std::string& name,
                                          const std::string& type,
                                          bool include_nested_pools) const;

  virtual base::TimeDelta ConnectionTimeout() const;

  virtual ClientSocketPoolHistograms* histograms() const;

 private:
  typedef ClientSocketPoolBase<TransportSocketParams> PoolBase;

  class TransportConnectJobFactory
      : public PoolBase::ConnectJobFactory {
   public:
    TransportConnectJobFactory(ClientSocketFactory* client_socket_factory,
                         HostResolver* host_resolver,
                         NetLog* net_log)
        : client_socket_factory_(client_socket_factory),
          host_resolver_(host_resolver),
          net_log_(net_log) {}

    virtual ~TransportConnectJobFactory() {}

    // ClientSocketPoolBase::ConnectJobFactory methods.

    virtual ConnectJob* NewConnectJob(
        const std::string& group_name,
        const PoolBase::Request& request,
        ConnectJob::Delegate* delegate) const;

    virtual base::TimeDelta ConnectionTimeout() const;

   private:
    ClientSocketFactory* const client_socket_factory_;
    HostResolver* const host_resolver_;
    NetLog* net_log_;

    DISALLOW_COPY_AND_ASSIGN(TransportConnectJobFactory);
  };

  PoolBase base_;

  DISALLOW_COPY_AND_ASSIGN(TransportClientSocketPool);
};

REGISTER_SOCKET_PARAMS_FOR_POOL(TransportClientSocketPool,
                                TransportSocketParams);

}  // namespace net

#endif  // NET_SOCKET_TRANSPORT_CLIENT_SOCKET_POOL_H_
