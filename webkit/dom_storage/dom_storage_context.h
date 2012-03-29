// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_DOM_STORAGE_DOM_STORAGE_CONTEXT_H_
#define WEBKIT_DOM_STORAGE_DOM_STORAGE_CONTEXT_H_
#pragma once

#include <map>
#include <vector>

#include "base/atomic_sequence_num.h"
#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/gtest_prod_util.h"
#include "base/memory/ref_counted.h"
#include "base/observer_list.h"
#include "base/time.h"
#include "googleurl/src/gurl.h"

class FilePath;
class NullableString16;

namespace base {
class Time;
}

namespace quota {
class SpecialStoragePolicy;
}

namespace dom_storage {

class DomStorageArea;
class DomStorageNamespace;
class DomStorageSession;
class DomStorageTaskRunner;

// The Context is the root of an object containment hierachy for
// Namespaces and Areas related to the owning profile.
// One instance is allocated in the main process for each profile,
// instance methods should be called serially in the background as
// determined by the task_runner. Specifcally not on chrome's non-blocking
// IO thread since these methods can result in blocking file io.
//
// In general terms, the DomStorage object relationships are...
//   Contexts (per-profile) own Namespaces which own Areas which share Maps.
//   Hosts(per-renderer) refer to Namespaces and Areas open in its renderer.
//   Sessions (per-tab) cause the creation and deletion of session Namespaces.
//
// Session Namespaces are cloned by initially making a shallow copy of
// all contained Areas, the shallow copies refer to the same refcounted Map,
// and does a deep copy-on-write if needed.
//
// Classes intended to be used by an embedder are DomStorageContext,
// DomStorageHost, and DomStorageSession. The other classes are for
// internal consumption.
class DomStorageContext
    : public base::RefCountedThreadSafe<DomStorageContext> {
 public:
  struct UsageInfo {
    GURL origin;
    size_t data_size;
    base::Time last_modified;

    UsageInfo();
    ~UsageInfo();
  };

  // An interface for observing LocalStorage events on the
  // background thread.
  class EventObserver {
   public:
    virtual void OnDomStorageItemSet(
        const DomStorageArea* area,
        const string16& key,
        const string16& new_value,
        const NullableString16& old_value,  // may be null on initial insert
        const GURL& page_url) = 0;
    virtual void OnDomStorageItemRemoved(
        const DomStorageArea* area,
        const string16& key,
        const string16& old_value,
        const GURL& page_url) = 0;
    virtual void OnDomStorageAreaCleared(
        const DomStorageArea* area,
        const GURL& page_url) = 0;
    virtual ~EventObserver() {}
  };

  DomStorageContext(const FilePath& directory,  // empty for incognito profiles
                    quota::SpecialStoragePolicy* special_storage_policy,
                    DomStorageTaskRunner* task_runner);
  const FilePath& directory() const { return directory_; }
  DomStorageTaskRunner* task_runner() const { return task_runner_; }
  DomStorageNamespace* GetStorageNamespace(int64 namespace_id);

  void GetUsageInfo(std::vector<UsageInfo>* infos, bool include_file_info);
  void DeleteOrigin(const GURL& origin);
  void DeleteDataModifiedSince(const base::Time& cutoff);
  void PurgeMemory();

  // Used by content settings to alter the behavior around
  // what data to keep and what data to discard at shutdown.
  // The policy is not so straight forward to describe, see
  // the implementation for details.
  void SetClearLocalState(bool clear_local_state) {
    clear_local_state_ = clear_local_state;
  }
  void SaveSessionState() {
    save_session_state_ = true;
  }

  // Called when the owning BrowserContext is ending.
  // Schedules the commit of any unsaved changes and will delete
  // and keep data on disk per the content settings and special storage
  // policies. Contained areas and namespaces will stop functioning after
  // this method has been called.
  void Shutdown();

  // Methods to add, remove, and notify EventObservers.
  void AddEventObserver(EventObserver* observer);
  void RemoveEventObserver(EventObserver* observer);
  void NotifyItemSet(
      const DomStorageArea* area,
      const string16& key,
      const string16& new_value,
      const NullableString16& old_value,
      const GURL& page_url);
  void NotifyItemRemoved(
      const DomStorageArea* area,
      const string16& key,
      const string16& old_value,
      const GURL& page_url);
  void NotifyAreaCleared(
      const DomStorageArea* area,
      const GURL& page_url);

  // May be called on any thread.
  int64 AllocateSessionId() {
    return session_id_sequence_.GetNext();
  }

  // Must be called on the background thread.
  void CreateSessionNamespace(int64 namespace_id);
  void DeleteSessionNamespace(int64 namespace_id);
  void CloneSessionNamespace(int64 existing_id, int64 new_id);

 private:
  friend class DomStorageContextTest;
  FRIEND_TEST_ALL_PREFIXES(DomStorageContextTest, Basics);
  friend class base::RefCountedThreadSafe<DomStorageContext>;
  typedef std::map<int64, scoped_refptr<DomStorageNamespace> >
      StorageNamespaceMap;

  ~DomStorageContext();

  void ClearLocalStateInCommitSequence();

  // Collection of namespaces keyed by id.
  StorageNamespaceMap namespaces_;

  // Where localstorage data is stored, maybe empty for the incognito use case.
  FilePath directory_;

  // Used to schedule sequenced background tasks.
  scoped_refptr<DomStorageTaskRunner> task_runner_;

  // List of objects observing local storage events.
  ObserverList<EventObserver> event_observers_;

  // We use a 32 bit identifier for per tab storage sessions.
  // At a tab per second, this range is large enough for 68 years.
  base::AtomicSequenceNumber session_id_sequence_;

  bool is_shutdown_;
  bool clear_local_state_;
  bool save_session_state_;
  scoped_refptr<quota::SpecialStoragePolicy> special_storage_policy_;
};

}  // namespace dom_storage

#endif  // WEBKIT_DOM_STORAGE_DOM_STORAGE_CONTEXT_H_
