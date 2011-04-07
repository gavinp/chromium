// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "jingle/notifier/listener/mediator_thread_impl.h"

#include "base/logging.h"
#include "base/message_loop.h"
#include "jingle/notifier/base/const_communicator.h"
#include "jingle/notifier/base/notifier_options_util.h"
#include "jingle/notifier/base/task_pump.h"
#include "jingle/notifier/communicator/connection_options.h"
#include "jingle/notifier/communicator/xmpp_connection_generator.h"
#include "jingle/notifier/listener/push_notifications_send_update_task.h"
#include "net/base/cert_verifier.h"
#include "net/base/host_port_pair.h"
#include "net/base/host_resolver.h"
#include "talk/xmpp/xmppclientsettings.h"

namespace notifier {

MediatorThreadImpl::MediatorThreadImpl(
    const NotifierOptions& notifier_options)
    : observers_(new ObserverListThreadSafe<Observer>()),
      construction_message_loop_(MessageLoop::current()),
      method_message_loop_(NULL),
      notifier_options_(notifier_options),
      worker_thread_("MediatorThread worker thread") {
  DCHECK(construction_message_loop_);
}

MediatorThreadImpl::~MediatorThreadImpl() {
  DCHECK_EQ(MessageLoop::current(), construction_message_loop_);
  // If the worker thread is still around, we need to call Logout() so
  // that all the variables living it get destroyed properly (i.e., on
  // the worker thread).
  if (worker_thread_.IsRunning()) {
    Logout();
  }
}

void MediatorThreadImpl::AddObserver(Observer* observer) {
  CheckOrSetValidThread();
  observers_->AddObserver(observer);
}

void MediatorThreadImpl::RemoveObserver(Observer* observer) {
  CheckOrSetValidThread();
  observers_->RemoveObserver(observer);
}

void MediatorThreadImpl::Start() {
  DCHECK_EQ(MessageLoop::current(), construction_message_loop_);
  // We create the worker thread as an IO thread in preparation for
  // making this use Chrome sockets.
  const base::Thread::Options options(MessageLoop::TYPE_IO, 0);
  // TODO(akalin): Make this function return a bool and remove this
  // CHECK().
  CHECK(worker_thread_.StartWithOptions(options));
}

void MediatorThreadImpl::Login(const buzz::XmppClientSettings& settings) {
  CheckOrSetValidThread();

  worker_message_loop()->PostTask(
      FROM_HERE,
      NewRunnableMethod(this, &MediatorThreadImpl::DoLogin, settings));
}

void MediatorThreadImpl::Logout() {
  CheckOrSetValidThread();
  worker_message_loop()->PostTask(
      FROM_HERE,
      NewRunnableMethod(this, &MediatorThreadImpl::DoDisconnect));
  // TODO(akalin): Decomp this into a separate stop method.
  worker_thread_.Stop();
  // worker_thread_ should have cleaned this up. It is OK to check this
  // variable in this thread because worker_thread_ is gone by now.
  CHECK(!login_.get());
}

void MediatorThreadImpl::ListenForUpdates() {
  CheckOrSetValidThread();
  worker_message_loop()->PostTask(
      FROM_HERE,
      NewRunnableMethod(this,
                        &MediatorThreadImpl::ListenForPushNotifications));
}

void MediatorThreadImpl::SubscribeForUpdates(
    const SubscriptionList& subscriptions) {
  CheckOrSetValidThread();
  worker_message_loop()->PostTask(
      FROM_HERE,
      NewRunnableMethod(
          this,
          &MediatorThreadImpl::SubscribeForPushNotifications,
          subscriptions));
}

void MediatorThreadImpl::SendNotification(
    const Notification& data) {
  CheckOrSetValidThread();
  worker_message_loop()->PostTask(
      FROM_HERE,
      NewRunnableMethod(this, &MediatorThreadImpl::DoSendNotification,
                        data));
}

void MediatorThreadImpl::UpdateXmppSettings(
    const buzz::XmppClientSettings& settings) {
  CheckOrSetValidThread();
  worker_message_loop()->PostTask(
      FROM_HERE,
      NewRunnableMethod(this,
                        &MediatorThreadImpl::DoUpdateXmppSettings,
                        settings));
}

MessageLoop* MediatorThreadImpl::worker_message_loop() {
  MessageLoop* current_message_loop = MessageLoop::current();
  DCHECK(current_message_loop);
  MessageLoop* worker_message_loop = worker_thread_.message_loop();
  DCHECK(worker_message_loop);
  DCHECK(current_message_loop == method_message_loop_ ||
         current_message_loop == worker_message_loop);
  return worker_message_loop;
}


void MediatorThreadImpl::DoLogin(
    const buzz::XmppClientSettings& settings) {
  DCHECK_EQ(MessageLoop::current(), worker_message_loop());
  VLOG(1) << "P2P: Thread logging into talk network.";

  base_task_.reset();

  host_resolver_.reset(
      net::CreateSystemHostResolver(net::HostResolver::kDefaultParallelism,
                                    NULL, NULL));
  cert_verifier_.reset(new net::CertVerifier);
  login_.reset(new notifier::Login(this,
                                   settings,
                                   notifier::ConnectionOptions(),
                                   host_resolver_.get(),
                                   cert_verifier_.get(),
                                   GetServerList(notifier_options_),
                                   notifier_options_.try_ssltcp_first,
                                   notifier_options_.auth_mechanism));
  login_->StartConnection();
}

void MediatorThreadImpl::DoDisconnect() {
  DCHECK_EQ(MessageLoop::current(), worker_message_loop());
  VLOG(1) << "P2P: Thread logging out of talk network.";
  login_.reset();
  cert_verifier_.reset();
  host_resolver_.reset();
  base_task_.reset();
}

void MediatorThreadImpl::ListenForPushNotifications() {
  DCHECK_EQ(MessageLoop::current(), worker_message_loop());
  if (!base_task_.get())
    return;
  PushNotificationsListenTask* listener =
      new PushNotificationsListenTask(base_task_, this);
  listener->Start();
}

void MediatorThreadImpl::SubscribeForPushNotifications(
    const SubscriptionList& subscriptions) {
  DCHECK_EQ(MessageLoop::current(), worker_message_loop());
  if (!base_task_.get())
    return;
  DCHECK_EQ(MessageLoop::current(), worker_message_loop());
  PushNotificationsSubscribeTask* subscribe_task =
      new PushNotificationsSubscribeTask(base_task_, subscriptions, this);
  subscribe_task->Start();
}

void MediatorThreadImpl::OnSubscribed() {
  DCHECK_EQ(MessageLoop::current(), worker_message_loop());
  observers_->Notify(&Observer::OnSubscriptionStateChange, true);
}

void MediatorThreadImpl::OnSubscriptionError() {
  DCHECK_EQ(MessageLoop::current(), worker_message_loop());
  observers_->Notify(&Observer::OnSubscriptionStateChange, false);
}

void MediatorThreadImpl::OnNotificationReceived(
    const Notification& notification) {
  DCHECK_EQ(MessageLoop::current(), worker_message_loop());
  observers_->Notify(&Observer::OnIncomingNotification, notification);
}

void MediatorThreadImpl::DoSendNotification(
    const Notification& data) {
  DCHECK_EQ(MessageLoop::current(), worker_message_loop());
  if (!base_task_.get()) {
    return;
  }
  // Owned by |base_task_|.
  PushNotificationsSendUpdateTask* task =
      new PushNotificationsSendUpdateTask(base_task_, data);
  task->Start();
  observers_->Notify(&Observer::OnOutgoingNotification);
}

void MediatorThreadImpl::DoUpdateXmppSettings(
    const buzz::XmppClientSettings& settings) {
  DCHECK_EQ(MessageLoop::current(), worker_message_loop());
  VLOG(1) << "P2P: Thread Updating login settings.";
  // The caller should only call UpdateXmppSettings after a Login call.
  if (login_.get())
    login_->UpdateXmppSettings(settings);
  else
    NOTREACHED() <<
        "P2P: Thread UpdateXmppSettings called when login_ was NULL";
}


void MediatorThreadImpl::OnConnect(base::WeakPtr<talk_base::Task> base_task) {
  DCHECK_EQ(MessageLoop::current(), worker_message_loop());
  base_task_ = base_task;
  observers_->Notify(&Observer::OnConnectionStateChange, true);
}

void MediatorThreadImpl::OnDisconnect() {
  DCHECK_EQ(MessageLoop::current(), worker_message_loop());
  base_task_.reset();
  observers_->Notify(&Observer::OnConnectionStateChange, false);
}

void MediatorThreadImpl::CheckOrSetValidThread() {
  if (method_message_loop_) {
    DCHECK_EQ(MessageLoop::current(), method_message_loop_);
  } else {
    method_message_loop_ = MessageLoop::current();
  }
}

}  // namespace notifier
