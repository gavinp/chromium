// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_DOM_STORAGE_DOM_STORAGE_TASK_RUNNER_
#define WEBKIT_DOM_STORAGE_DOM_STORAGE_TASK_RUNNER_
#pragma once

#include "base/memory/ref_counted.h"
#include "base/sequenced_task_runner.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/time.h"

namespace base {
class MessageLoopProxy;
}

namespace dom_storage {

// DomStorage uses two task sequences (primary vs commit) to avoid
// primary access from queuing up behind commits to disk.
// * Initialization, shutdown, and administrative tasks are performed as
//   shutdown-blocking primary sequence tasks.
// * Methods that return values to the javascript'able interface are performed
//   as non-shutdown-blocking primary sequence tasks.
// * Internal tasks related to committing changes to disk are performed as
//   shutdown-blocking commit sequence tasks.
class DomStorageTaskRunner : public base::TaskRunner {
 public:
  enum SequenceID {
    PRIMARY_SEQUENCE,
    COMMIT_SEQUENCE
  };

  // The PostTask() and PostDelayedTask() methods defined by TaskRunner
  // post non-shutdown-blocking tasks on the primary sequence.
  virtual bool PostDelayedTask(
      const tracked_objects::Location& from_here,
      const base::Closure& task,
      base::TimeDelta delay) = 0;

  // Posts a shutdown blocking task to |sequence_id|.
  virtual bool PostShutdownBlockingTask(
      const tracked_objects::Location& from_here,
      SequenceID sequence_id,
      const base::Closure& task) = 0;

  // Only here because base::TaskRunner requires it, the return
  // value is hard coded to true, do not rely on this method.
  virtual bool RunsTasksOnCurrentThread() const OVERRIDE;

  // DEPRECATED: Only here because base::TaskRunner requires it, implemented
  // by calling the virtual PostDelayedTask(..., TimeDelta) variant.
  virtual bool PostDelayedTask(
      const tracked_objects::Location& from_here,
      const base::Closure& task,
      int64 delay_ms) OVERRIDE;
};

// A derived class used in chromium that utilizes a SequenceWorkerPool
// under dom_storage specific SequenceTokens. The |delayed_task_loop|
// is used to delay scheduling on the worker pool.
class DomStorageWorkerPoolTaskRunner : public DomStorageTaskRunner {
 public:
  DomStorageWorkerPoolTaskRunner(
      base::SequencedWorkerPool* sequenced_worker_pool,
      base::SequencedWorkerPool::SequenceToken primary_sequence_token,
      base::SequencedWorkerPool::SequenceToken commit_sequence_token,
      base::MessageLoopProxy* delayed_task_loop);

  virtual bool PostDelayedTask(
      const tracked_objects::Location& from_here,
      const base::Closure& task,
      base::TimeDelta delay) OVERRIDE;

  virtual bool PostShutdownBlockingTask(
      const tracked_objects::Location& from_here,
      SequenceID sequence_id,
      const base::Closure& task) OVERRIDE;

 private:
  virtual ~DomStorageWorkerPoolTaskRunner();
  const scoped_refptr<base::MessageLoopProxy> message_loop_;
  const scoped_refptr<base::SequencedWorkerPool> sequenced_worker_pool_;
  base::SequencedWorkerPool::SequenceToken primary_sequence_token_;
  base::SequencedWorkerPool::SequenceToken commit_sequence_token_;
};

// A derived class used in unit tests that ignores all delays so
// we don't block in unit tests waiting for timeouts to expire.
// There is no distinction between [non]-shutdown-blocking or
// the primary sequence vs the commit sequence in the mock,
// all tasks are scheduled on |message_loop| with zero delay.
class MockDomStorageTaskRunner : public DomStorageTaskRunner {
 public:
  explicit MockDomStorageTaskRunner(base::MessageLoopProxy* message_loop);

  virtual bool PostDelayedTask(
      const tracked_objects::Location& from_here,
      const base::Closure& task,
      base::TimeDelta delay) OVERRIDE;

  virtual bool PostShutdownBlockingTask(
      const tracked_objects::Location& from_here,
      SequenceID sequence_id,
      const base::Closure& task) OVERRIDE;

 private:
  virtual ~MockDomStorageTaskRunner();
  const scoped_refptr<base::MessageLoopProxy> message_loop_;
};

}  // namespace dom_storage

#endif  // WEBKIT_DOM_STORAGE_DOM_STORAGE_TASK_RUNNER_
