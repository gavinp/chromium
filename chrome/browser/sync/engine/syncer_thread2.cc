// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/engine/syncer_thread2.h"

#include <algorithm>

#include "base/rand_util.h"
#include "chrome/browser/sync/engine/syncer.h"

using base::TimeDelta;
using base::TimeTicks;

namespace browser_sync {

using sessions::SyncSession;
using sessions::SyncSessionSnapshot;
using sessions::SyncSourceInfo;
using syncable::ModelTypePayloadMap;
using syncable::ModelTypeBitSet;
using sync_pb::GetUpdatesCallerInfo;

namespace s3 {

struct SyncerThread::WaitInterval {
  enum Mode {
    // A wait interval whose duration has been affected by exponential
    // backoff.
    // EXPONENTIAL_BACKOFF intervals are nudge-rate limited to 1 per interval.
    EXPONENTIAL_BACKOFF,
    // A server-initiated throttled interval.  We do not allow any syncing
    // during such an interval.
    THROTTLED,
  };
  Mode mode;

  // This bool is set to true if we have observed a nudge during this
  // interval and mode == EXPONENTIAL_BACKOFF.
  bool had_nudge;
  base::TimeDelta length;
  base::OneShotTimer<SyncerThread> timer;
  WaitInterval(Mode mode, base::TimeDelta length);
};

struct SyncerThread::SyncSessionJob {
  SyncSessionJobPurpose purpose;
  base::TimeTicks scheduled_start;
  linked_ptr<sessions::SyncSession> session;

  // This is the location the nudge came from. used for debugging purpose.
  // In case of multiple nudges getting coalesced this stores the first nudge
  // that came in.
  tracked_objects::Location nudge_location;
};

SyncerThread::DelayProvider::DelayProvider() {}
SyncerThread::DelayProvider::~DelayProvider() {}

TimeDelta SyncerThread::DelayProvider::GetDelay(
    const base::TimeDelta& last_delay) {
  return SyncerThread::GetRecommendedDelay(last_delay);
}

SyncerThread::WaitInterval::WaitInterval(Mode mode, TimeDelta length)
    : mode(mode), had_nudge(false), length(length) { }

SyncerThread::SyncerThread(sessions::SyncSessionContext* context,
                           Syncer* syncer)
    : thread_("SyncEngine_SyncerThread"),
      syncer_short_poll_interval_seconds_(
          TimeDelta::FromSeconds(kDefaultShortPollIntervalSeconds)),
      syncer_long_poll_interval_seconds_(
          TimeDelta::FromSeconds(kDefaultLongPollIntervalSeconds)),
      mode_(NORMAL_MODE),
      server_connection_ok_(false),
      delay_provider_(new DelayProvider()),
      syncer_(syncer),
      session_context_(context) {
}

SyncerThread::~SyncerThread() {
  DCHECK(!thread_.IsRunning());
}

void SyncerThread::CheckServerConnectionManagerStatus(
    HttpResponse::ServerConnectionCode code) {
  // Note, be careful when adding cases here because if the SyncerThread
  // thinks there is no valid connection as determined by this method, it
  // will drop out of *all* forward progress sync loops (it won't poll and it
  // will queue up Talk notifications but not actually call SyncShare) until
  // some external action causes a ServerConnectionManager to broadcast that
  // a valid connection has been re-established.
  if (HttpResponse::CONNECTION_UNAVAILABLE == code ||
      HttpResponse::SYNC_AUTH_ERROR == code) {
    server_connection_ok_ = false;
  } else if (HttpResponse::SERVER_CONNECTION_OK == code) {
    server_connection_ok_ = true;
  }
}

void SyncerThread::Start(Mode mode, ModeChangeCallback* callback) {
  if (!thread_.IsRunning()) {
    if (!thread_.Start()) {
      NOTREACHED() << "Unable to start SyncerThread.";
      return;
    }
    WatchConnectionManager();
    thread_.message_loop()->PostTask(FROM_HERE, NewRunnableMethod(
        this, &SyncerThread::SendInitialSnapshot));
  }

  thread_.message_loop()->PostTask(FROM_HERE, NewRunnableMethod(
      this, &SyncerThread::StartImpl, mode, make_linked_ptr(callback)));
}

void SyncerThread::SendInitialSnapshot() {
  DCHECK_EQ(MessageLoop::current(), thread_.message_loop());
  scoped_ptr<SyncSession> dummy(new SyncSession(session_context_.get(), this,
      SyncSourceInfo(), ModelSafeRoutingInfo(),
      std::vector<ModelSafeWorker*>()));
  SyncEngineEvent event(SyncEngineEvent::STATUS_CHANGED);
  sessions::SyncSessionSnapshot snapshot(dummy->TakeSnapshot());
  event.snapshot = &snapshot;
  session_context_->NotifyListeners(event);
}

void SyncerThread::WatchConnectionManager() {
  ServerConnectionManager* scm = session_context_->connection_manager();
  CheckServerConnectionManagerStatus(scm->server_status());
  scm->AddListener(this);
}

void SyncerThread::StartImpl(Mode mode,
                             linked_ptr<ModeChangeCallback> callback) {
  DCHECK_EQ(MessageLoop::current(), thread_.message_loop());
  DCHECK(!session_context_->account_name().empty());
  DCHECK(syncer_.get());
  mode_ = mode;
  AdjustPolling(NULL);  // Will kick start poll timer if needed.
  if (callback.get())
    callback->Run();
}

bool SyncerThread::ShouldRunJob(SyncSessionJobPurpose purpose,
    const TimeTicks& scheduled_start) {
  DCHECK_EQ(MessageLoop::current(), thread_.message_loop());

  // Check wait interval.
  if (wait_interval_.get()) {
    // TODO(tim): Consider different handling for CLEAR_USER_DATA (i.e. permit
    // when throttled).
    if (wait_interval_->mode == WaitInterval::THROTTLED)
      return false;

    DCHECK_EQ(wait_interval_->mode, WaitInterval::EXPONENTIAL_BACKOFF);
    if ((purpose != NUDGE) || wait_interval_->had_nudge)
      return false;
  }

  // Mode / purpose contract (See 'Mode' enum in header). Don't run jobs that
  // were intended for a normal sync if we are in configuration mode, and vice
  // versa.
  switch (mode_) {
    case CONFIGURATION_MODE:
      if (purpose != CONFIGURATION)
        return false;
      break;
    case NORMAL_MODE:
      if (purpose == CONFIGURATION)
        return false;
      break;
    default:
      NOTREACHED() << "Unknown SyncerThread Mode: " << mode_;
      return false;
  }

  // Continuation NUDGE tasks have priority over POLLs because they are the
  // only tasks that trigger exponential backoff, so this prevents them from
  // being starved from running (e.g. due to a very, very low poll interval,
  // such as 0ms). It's rare that this would ever matter in practice.
  if (purpose == POLL && (pending_nudge_.get() &&
      pending_nudge_->session->source().updates_source ==
          GetUpdatesCallerInfo::SYNC_CYCLE_CONTINUATION)) {
    return false;
  }

  // Freshness condition.
  if (purpose == NUDGE &&
      (scheduled_start < last_sync_session_end_time_)) {
    return false;
  }

  return server_connection_ok_;
}

GetUpdatesCallerInfo::GetUpdatesSource GetUpdatesFromNudgeSource(
    NudgeSource source) {
  switch (source) {
    case NUDGE_SOURCE_NOTIFICATION:
      return GetUpdatesCallerInfo::NOTIFICATION;
    case NUDGE_SOURCE_LOCAL:
      return GetUpdatesCallerInfo::LOCAL;
    case NUDGE_SOURCE_CONTINUATION:
      return GetUpdatesCallerInfo::SYNC_CYCLE_CONTINUATION;
    case NUDGE_SOURCE_UNKNOWN:
      return GetUpdatesCallerInfo::UNKNOWN;
    default:
      NOTREACHED();
      return GetUpdatesCallerInfo::UNKNOWN;
  }
}

// Functor for std::find_if to search by ModelSafeGroup.
struct ModelSafeWorkerGroupIs {
  explicit ModelSafeWorkerGroupIs(ModelSafeGroup group) : group(group) {}
  bool operator()(ModelSafeWorker* w) {
    return group == w->GetModelSafeGroup();
  }
  ModelSafeGroup group;
};

void SyncerThread::ScheduleClearUserData() {
  if (!thread_.IsRunning()) {
    NOTREACHED();
    return;
  }
  thread_.message_loop()->PostTask(FROM_HERE, NewRunnableMethod(
      this, &SyncerThread::ScheduleClearUserDataImpl));
}

void SyncerThread::ScheduleNudge(const TimeDelta& delay,
    NudgeSource source, const ModelTypeBitSet& types,
    const tracked_objects::Location& nudge_location) {
  if (!thread_.IsRunning()) {
    NOTREACHED();
    return;
  }

  ModelTypePayloadMap types_with_payloads =
      syncable::ModelTypePayloadMapFromBitSet(types, std::string());
  thread_.message_loop()->PostTask(FROM_HERE, NewRunnableMethod(
      this, &SyncerThread::ScheduleNudgeImpl, delay, source,
      types_with_payloads, nudge_location));
}

void SyncerThread::ScheduleNudgeWithPayloads(const TimeDelta& delay,
    NudgeSource source, const ModelTypePayloadMap& types_with_payloads,
    const tracked_objects::Location& nudge_location) {
  if (!thread_.IsRunning()) {
    NOTREACHED();
    return;
  }

  thread_.message_loop()->PostTask(FROM_HERE, NewRunnableMethod(
      this, &SyncerThread::ScheduleNudgeImpl, delay, source,
      types_with_payloads, nudge_location));
}

void SyncerThread::ScheduleClearUserDataImpl() {
  DCHECK_EQ(MessageLoop::current(), thread_.message_loop());
  SyncSession* session = new SyncSession(session_context_.get(), this,
      SyncSourceInfo(), ModelSafeRoutingInfo(),
      std::vector<ModelSafeWorker*>());
  ScheduleSyncSessionJob(TimeDelta::FromSeconds(0), CLEAR_USER_DATA, session,
                         FROM_HERE);
}

void SyncerThread::ScheduleNudgeImpl(const TimeDelta& delay,
    NudgeSource source, const ModelTypePayloadMap& types_with_payloads,
    const tracked_objects::Location& nudge_location) {
  DCHECK_EQ(MessageLoop::current(), thread_.message_loop());
  TimeTicks rough_start = TimeTicks::Now() + delay;
  if (!ShouldRunJob(NUDGE, rough_start)) {
    LOG(WARNING) << "Dropping nudge at scheduling time, source = "
                 << source;
    return;
  }

  // Note we currently nudge for all types regardless of the ones incurring
  // the nudge.  Doing different would throw off some syncer commands like
  // CleanupDisabledTypes.  We may want to change this in the future.
  ModelSafeRoutingInfo routes;
  std::vector<ModelSafeWorker*> workers;
  session_context_->registrar()->GetModelSafeRoutingInfo(&routes);
  session_context_->registrar()->GetWorkers(&workers);
  SyncSourceInfo info(GetUpdatesFromNudgeSource(source),
                      types_with_payloads);

  scoped_ptr<SyncSession> session(new SyncSession(
      session_context_.get(), this, info, routes, workers));

  if (pending_nudge_.get()) {
    if (IsBackingOff() && delay > TimeDelta::FromSeconds(1))
      return;

    pending_nudge_->session->Coalesce(*session.get());

    if (!IsBackingOff()) {
      return;
    } else {
      // Re-schedule the current pending nudge.
      SyncSession* s = pending_nudge_->session.get();
      session.reset(new SyncSession(s->context(), s->delegate(), s->source(),
          s->routing_info(), s->workers()));
      pending_nudge_.reset();
    }
  }
  ScheduleSyncSessionJob(delay, NUDGE, session.release(), nudge_location);
}

// Helper to extract the routing info and workers corresponding to types in
// |types| from |registrar|.
void GetModelSafeParamsForTypes(const ModelTypeBitSet& types,
    ModelSafeWorkerRegistrar* registrar, ModelSafeRoutingInfo* routes,
    std::vector<ModelSafeWorker*>* workers) {
  ModelSafeRoutingInfo r_tmp;
  std::vector<ModelSafeWorker*> w_tmp;
  registrar->GetModelSafeRoutingInfo(&r_tmp);
  registrar->GetWorkers(&w_tmp);

  typedef std::vector<ModelSafeWorker*>::const_iterator iter;
  for (size_t i = syncable::FIRST_REAL_MODEL_TYPE; i < types.size(); ++i) {
    if (!types.test(i))
      continue;
    syncable::ModelType t = syncable::ModelTypeFromInt(i);
    DCHECK_EQ(1U, r_tmp.count(t));
    (*routes)[t] = r_tmp[t];
    iter it = std::find_if(w_tmp.begin(), w_tmp.end(),
                           ModelSafeWorkerGroupIs(r_tmp[t]));
    if (it != w_tmp.end())
      workers->push_back(*it);
    else
      NOTREACHED();
  }

  iter it = std::find_if(w_tmp.begin(), w_tmp.end(),
                         ModelSafeWorkerGroupIs(GROUP_PASSIVE));
  if (it != w_tmp.end())
    workers->push_back(*it);
  else
    NOTREACHED();
}

void SyncerThread::ScheduleConfig(const ModelTypeBitSet& types) {
  if (!thread_.IsRunning()) {
    NOTREACHED();
    return;
  }

  ModelSafeRoutingInfo routes;
  std::vector<ModelSafeWorker*> workers;
  GetModelSafeParamsForTypes(types, session_context_->registrar(),
                             &routes, &workers);

  thread_.message_loop()->PostTask(FROM_HERE, NewRunnableMethod(
      this, &SyncerThread::ScheduleConfigImpl, routes, workers));
}

void SyncerThread::ScheduleConfigImpl(const ModelSafeRoutingInfo& routing_info,
    const std::vector<ModelSafeWorker*>& workers) {
  DCHECK_EQ(MessageLoop::current(), thread_.message_loop());

  // TODO(tim): config-specific GetUpdatesCallerInfo value?
  SyncSession* session = new SyncSession(session_context_.get(), this,
      SyncSourceInfo(GetUpdatesCallerInfo::FIRST_UPDATE,
          syncable::ModelTypePayloadMapFromRoutingInfo(
              routing_info, std::string())),
      routing_info, workers);
  ScheduleSyncSessionJob(TimeDelta::FromSeconds(0), CONFIGURATION, session,
                         FROM_HERE);
}

void SyncerThread::ScheduleSyncSessionJob(const base::TimeDelta& delay,
    SyncSessionJobPurpose purpose, sessions::SyncSession* session,
    const tracked_objects::Location& nudge_location) {
  DCHECK_EQ(MessageLoop::current(), thread_.message_loop());

  SyncSessionJob job = {purpose, TimeTicks::Now() + delay,
                        make_linked_ptr(session), nudge_location};
  if (purpose == NUDGE) {
    DCHECK(!pending_nudge_.get() || pending_nudge_->session.get() == session);
    pending_nudge_.reset(new SyncSessionJob(job));
  }
  MessageLoop::current()->PostDelayedTask(FROM_HERE, NewRunnableMethod(this,
      &SyncerThread::DoSyncSessionJob, job),
      delay.InMilliseconds());
}

void SyncerThread::SetSyncerStepsForPurpose(SyncSessionJobPurpose purpose,
    SyncerStep* start, SyncerStep* end) {
  *end = SYNCER_END;
  switch (purpose) {
    case CONFIGURATION:
      *start = DOWNLOAD_UPDATES;
      *end = APPLY_UPDATES;
      return;
    case CLEAR_USER_DATA:
      *start = CLEAR_PRIVATE_DATA;
       return;
    case NUDGE:
    case POLL:
      *start = SYNCER_BEGIN;
      return;
    default:
      NOTREACHED();
  }
}

void SyncerThread::DoSyncSessionJob(const SyncSessionJob& job) {
  DCHECK_EQ(MessageLoop::current(), thread_.message_loop());
  if (!ShouldRunJob(job.purpose, job.scheduled_start)) {
    LOG(WARNING) << "Dropping nudge at DoSyncSessionJob, source = "
        << job.session->source().updates_source;
    return;
  }

  if (job.purpose == NUDGE) {
    DCHECK(pending_nudge_.get());
    if (pending_nudge_->session != job.session)
      return;  // Another nudge must have been scheduled in in the meantime.
    pending_nudge_.reset();
  }

  SyncerStep begin(SYNCER_BEGIN);
  SyncerStep end(SYNCER_END);
  SetSyncerStepsForPurpose(job.purpose, &begin, &end);

  bool has_more_to_sync = true;
  while (ShouldRunJob(job.purpose, job.scheduled_start) && has_more_to_sync) {
    VLOG(1) << "SyncerThread: Calling SyncShare.";
    // Synchronously perform the sync session from this thread.
    syncer_->SyncShare(job.session.get(), begin, end);
    has_more_to_sync = job.session->HasMoreToSync();
    if (has_more_to_sync)
      job.session->ResetTransientState();
  }
  VLOG(1) << "SyncerThread: Done SyncShare looping.";
  FinishSyncSessionJob(job);
}

void SyncerThread::UpdateCarryoverSessionState(const SyncSessionJob& old_job) {
  if (old_job.purpose == CONFIGURATION) {
    // Whatever types were part of a configuration task will have had updates
    // downloaded.  For that reason, we make sure they get recorded in the
    // event that they get disabled at a later time.
    ModelSafeRoutingInfo r(session_context_->previous_session_routing_info());
    if (!r.empty()) {
      ModelSafeRoutingInfo temp_r;
      ModelSafeRoutingInfo old_info(old_job.session->routing_info());
      std::set_union(r.begin(), r.end(), old_info.begin(), old_info.end(),
          std::insert_iterator<ModelSafeRoutingInfo>(temp_r, temp_r.begin()));
      session_context_->set_previous_session_routing_info(temp_r);
    }
  } else {
    session_context_->set_previous_session_routing_info(
        old_job.session->routing_info());
  }
}

void SyncerThread::FinishSyncSessionJob(const SyncSessionJob& job) {
  DCHECK_EQ(MessageLoop::current(), thread_.message_loop());
  // Update timing information for how often datatypes are triggering nudges.
  base::TimeTicks now = TimeTicks::Now();
  if (!last_sync_session_end_time_.is_null()) {
    ModelTypePayloadMap::const_iterator iter;
    for (iter = job.session->source().types.begin();
         iter != job.session->source().types.end();
         ++iter) {
      syncable::PostTimeToTypeHistogram(iter->first,
                                        now - last_sync_session_end_time_);
    }
  }
  last_sync_session_end_time_ = now;
  UpdateCarryoverSessionState(job);
  if (IsSyncingCurrentlySilenced())
    return;  // Nothing to do.

  VLOG(1) << "Updating the next polling time after SyncMain";
  ScheduleNextSync(job);
}

void SyncerThread::ScheduleNextSync(const SyncSessionJob& old_job) {
  DCHECK_EQ(MessageLoop::current(), thread_.message_loop());
  DCHECK(!old_job.session->HasMoreToSync());
  // Note: |num_server_changes_remaining| > 0 here implies that we received a
  // broken response while trying to download all updates, because the Syncer
  // will loop until this value is exhausted. Also, if unsynced_handles exist
  // but HasMoreToSync is false, this implies that the Syncer determined no
  // forward progress was possible at this time (an error, such as an HTTP
  // 500, is likely to have occurred during commit).
  const bool work_to_do =
     old_job.session->status_controller()->num_server_changes_remaining() > 0
     || old_job.session->status_controller()->unsynced_handles().size() > 0;
  VLOG(1) << "syncer has work to do: " << work_to_do;

  AdjustPolling(&old_job);

  // TODO(tim): Old impl had special code if notifications disabled. Needed?
  if (!work_to_do) {
    // Success implies backoff relief.  Note that if this was a "one-off" job
    // (i.e. purpose == CLEAR_USER_DATA), if there was work_to_do before it
    // ran this wont have changed, as jobs like this don't run a full sync
    // cycle.  So we don't need special code here.
    wait_interval_.reset();
    return;
  }

  if (old_job.session->source().updates_source ==
      GetUpdatesCallerInfo::SYNC_CYCLE_CONTINUATION) {
    // We don't seem to have made forward progress. Start or extend backoff.
    HandleConsecutiveContinuationError(old_job);
  } else if (IsBackingOff()) {
    // We weren't continuing but we're in backoff; must have been a nudge.
    DCHECK_EQ(NUDGE, old_job.purpose);
    DCHECK(!wait_interval_->had_nudge);
    wait_interval_->had_nudge = true;
    wait_interval_->timer.Reset();
  } else {
    // We weren't continuing and we aren't in backoff.  Schedule a normal
    // continuation.
    ScheduleNudgeImpl(TimeDelta::FromSeconds(0), NUDGE_SOURCE_CONTINUATION,
                      old_job.session->source().types, FROM_HERE);
  }
}

void SyncerThread::AdjustPolling(const SyncSessionJob* old_job) {
  DCHECK(thread_.IsRunning());
  DCHECK_EQ(MessageLoop::current(), thread_.message_loop());

  TimeDelta poll  = (!session_context_->notifications_enabled()) ?
      syncer_short_poll_interval_seconds_ :
      syncer_long_poll_interval_seconds_;
  bool rate_changed = !poll_timer_.IsRunning() ||
                       poll != poll_timer_.GetCurrentDelay();

  if (old_job && old_job->purpose != POLL && !rate_changed)
    poll_timer_.Reset();

  if (!rate_changed)
    return;

  // Adjust poll rate.
  poll_timer_.Stop();
  poll_timer_.Start(poll, this, &SyncerThread::PollTimerCallback);
}

void SyncerThread::HandleConsecutiveContinuationError(
    const SyncSessionJob& old_job) {
  DCHECK_EQ(MessageLoop::current(), thread_.message_loop());
  DCHECK(!IsBackingOff() || !wait_interval_->timer.IsRunning());
  SyncSession* old = old_job.session.get();
  SyncSession* s(new SyncSession(session_context_.get(), this,
      old->source(), old->routing_info(), old->workers()));
  TimeDelta length = delay_provider_->GetDelay(
      IsBackingOff() ? wait_interval_->length : TimeDelta::FromSeconds(1));
  wait_interval_.reset(new WaitInterval(WaitInterval::EXPONENTIAL_BACKOFF,
                                        length));
  SyncSessionJob job = {NUDGE, TimeTicks::Now() + length,
                        make_linked_ptr(s), FROM_HERE};
  pending_nudge_.reset(new SyncSessionJob(job));
  wait_interval_->timer.Start(length, this, &SyncerThread::DoCanaryJob);
}

// static
TimeDelta SyncerThread::GetRecommendedDelay(const TimeDelta& last_delay) {
  if (last_delay.InSeconds() >= kMaxBackoffSeconds)
    return TimeDelta::FromSeconds(kMaxBackoffSeconds);

  // This calculates approx. base_delay_seconds * 2 +/- base_delay_seconds / 2
  int64 backoff_s =
      std::max(static_cast<int64>(1),
               last_delay.InSeconds() * kBackoffRandomizationFactor);

  // Flip a coin to randomize backoff interval by +/- 50%.
  int rand_sign = base::RandInt(0, 1) * 2 - 1;

  // Truncation is adequate for rounding here.
  backoff_s = backoff_s +
      (rand_sign * (last_delay.InSeconds() / kBackoffRandomizationFactor));

  // Cap the backoff interval.
  backoff_s = std::max(static_cast<int64>(1),
                       std::min(backoff_s, kMaxBackoffSeconds));

  return TimeDelta::FromSeconds(backoff_s);
}

void SyncerThread::Stop() {
  syncer_->RequestEarlyExit();  // Safe to call from any thread.
  session_context_->connection_manager()->RemoveListener(this);
  thread_.Stop();
}

void SyncerThread::DoCanaryJob() {
  DCHECK(pending_nudge_.get());
  wait_interval_->had_nudge = false;
  SyncSessionJob copy = *pending_nudge_;
  DoSyncSessionJob(copy);
}

void SyncerThread::PollTimerCallback() {
  DCHECK_EQ(MessageLoop::current(), thread_.message_loop());
  ModelSafeRoutingInfo r;
  std::vector<ModelSafeWorker*> w;
  session_context_->registrar()->GetModelSafeRoutingInfo(&r);
  session_context_->registrar()->GetWorkers(&w);
  ModelTypePayloadMap types_with_payloads =
      syncable::ModelTypePayloadMapFromRoutingInfo(r, std::string());
  SyncSourceInfo info(GetUpdatesCallerInfo::PERIODIC, types_with_payloads);
  SyncSession* s = new SyncSession(session_context_.get(), this, info, r, w);
  ScheduleSyncSessionJob(TimeDelta::FromSeconds(0), POLL, s, FROM_HERE);
}

void SyncerThread::Unthrottle() {
  DCHECK_EQ(WaitInterval::THROTTLED, wait_interval_->mode);
  wait_interval_.reset();
}

void SyncerThread::Notify(SyncEngineEvent::EventCause cause) {
  DCHECK_EQ(MessageLoop::current(), thread_.message_loop());
  session_context_->NotifyListeners(SyncEngineEvent(cause));
}

bool SyncerThread::IsBackingOff() const {
  return wait_interval_.get() && wait_interval_->mode ==
      WaitInterval::EXPONENTIAL_BACKOFF;
}

void SyncerThread::OnSilencedUntil(const base::TimeTicks& silenced_until) {
  wait_interval_.reset(new WaitInterval(WaitInterval::THROTTLED,
                                        silenced_until - TimeTicks::Now()));
  wait_interval_->timer.Start(wait_interval_->length, this,
      &SyncerThread::Unthrottle);
}

bool SyncerThread::IsSyncingCurrentlySilenced() {
  return wait_interval_.get() && wait_interval_->mode ==
      WaitInterval::THROTTLED;
}

void SyncerThread::OnReceivedShortPollIntervalUpdate(
    const base::TimeDelta& new_interval) {
  DCHECK_EQ(MessageLoop::current(), thread_.message_loop());
  syncer_short_poll_interval_seconds_ = new_interval;
}

void SyncerThread::OnReceivedLongPollIntervalUpdate(
    const base::TimeDelta& new_interval) {
  DCHECK_EQ(MessageLoop::current(), thread_.message_loop());
  syncer_long_poll_interval_seconds_ = new_interval;
}

void SyncerThread::OnShouldStopSyncingPermanently() {
  syncer_->RequestEarlyExit();  // Thread-safe.
  Notify(SyncEngineEvent::STOP_SYNCING_PERMANENTLY);
}

void SyncerThread::OnServerConnectionEvent(
    const ServerConnectionEvent2& event) {
  thread_.message_loop()->PostTask(FROM_HERE, NewRunnableMethod(this,
      &SyncerThread::CheckServerConnectionManagerStatus,
      event.connection_code));
}

void SyncerThread::set_notifications_enabled(bool notifications_enabled) {
  session_context_->set_notifications_enabled(notifications_enabled);
}

}  // s3
}  // browser_sync
