// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_IN_PROCESS_WEBKIT_INDEXED_DB_CONTEXT_IMPL_H_
#define CONTENT_BROWSER_IN_PROCESS_WEBKIT_INDEXED_DB_CONTEXT_IMPL_H_
#pragma once

#include <map>
#include <set>

#include "base/compiler_specific.h"
#include "base/file_path.h"
#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "content/public/browser/indexed_db_context.h"
#include "googleurl/src/gurl.h"
#include "webkit/quota/quota_types.h"

class GURL;
class FilePath;

namespace WebKit {
class WebIDBFactory;
}

namespace base {
class MessageLoopProxy;
}

namespace quota {
class QuotaManagerProxy;
class SpecialStoragePolicy;
}

class CONTENT_EXPORT IndexedDBContextImpl
    : NON_EXPORTED_BASE(public content::IndexedDBContext) {
 public:
  // If |data_path| is empty, nothing will be saved to disk.
  IndexedDBContextImpl(const FilePath& data_path,
                       quota::SpecialStoragePolicy* special_storage_policy,
                       quota::QuotaManagerProxy* quota_manager_proxy,
                       base::MessageLoopProxy* webkit_thread_loop);

  virtual ~IndexedDBContextImpl();

  WebKit::WebIDBFactory* GetIDBFactory();

  // The indexed db directory.
  static const FilePath::CharType kIndexedDBDirectory[];

  // The indexed db file extension.
  static const FilePath::CharType kIndexedDBExtension[];

  void set_clear_local_state_on_exit(bool clear_local_state) {
    clear_local_state_on_exit_ = clear_local_state;
  }

  // Disables the exit-time deletion for all data (also session-only data).
  void SaveSessionState() {
    save_session_state_ = true;
  }

  // IndexedDBContext implementation:
  virtual std::vector<GURL> GetAllOrigins() OVERRIDE;
  virtual int64 GetOriginDiskUsage(const GURL& origin_url) OVERRIDE;
  virtual base::Time GetOriginLastModified(const GURL& origin_url) OVERRIDE;
  virtual void DeleteForOrigin(const GURL& origin_url) OVERRIDE;
  virtual FilePath GetFilePathForTesting(
      const string16& origin_id) const OVERRIDE;

  // Methods called by IndexedDBDispatcherHost for quota support.
  void ConnectionOpened(const GURL& origin_url);
  void ConnectionClosed(const GURL& origin_url);
  void TransactionComplete(const GURL& origin_url);
  bool WouldBeOverQuota(const GURL& origin_url, int64 additional_bytes);
  bool IsOverQuota(const GURL& origin_url);

  quota::QuotaManagerProxy* quota_manager_proxy();

  FilePath data_path() const { return data_path_; }

  // For unit tests allow to override the |data_path_|.
  void set_data_path_for_testing(const FilePath& data_path) {
    data_path_ = data_path;
  }

 private:
  FRIEND_TEST_ALL_PREFIXES(IndexedDBTest, ClearLocalState);
  FRIEND_TEST_ALL_PREFIXES(IndexedDBTest, ClearSessionOnlyDatabases);
  FRIEND_TEST_ALL_PREFIXES(IndexedDBTest, SaveSessionState);
  friend class IndexedDBQuotaClientTest;

  typedef std::map<GURL, int64> OriginToSizeMap;
  class IndexedDBGetUsageAndQuotaCallback;

  FilePath GetIndexedDBFilePath(const string16& origin_id) const;
  int64 ReadUsageFromDisk(const GURL& origin_url) const;
  void EnsureDiskUsageCacheInitialized(const GURL& origin_url);
  void QueryDiskAndUpdateQuotaUsage(const GURL& origin_url);
  void GotUsageAndQuota(const GURL& origin_url, quota::QuotaStatusCode,
                        int64 usage, int64 quota);
  void GotUpdatedQuota(const GURL& origin_url, int64 usage, int64 quota);
  void QueryAvailableQuota(const GURL& origin_url);

  std::set<GURL>* GetOriginSet();
  bool AddToOriginSet(const GURL& origin_url) {
    return GetOriginSet()->insert(origin_url).second;
  }
  void RemoveFromOriginSet(const GURL& origin_url) {
    GetOriginSet()->erase(origin_url);
  }
  bool IsInOriginSet(const GURL& origin_url) {
    std::set<GURL>* set = GetOriginSet();
    return set->find(origin_url) != set->end();
  }

  // Only for testing.
  void ResetCaches();

  scoped_ptr<WebKit::WebIDBFactory> idb_factory_;
  FilePath data_path_;
  bool clear_local_state_on_exit_;
  // If true, nothing (not even session-only data) should be deleted on exit.
  bool save_session_state_;
  scoped_refptr<quota::SpecialStoragePolicy> special_storage_policy_;
  scoped_refptr<quota::QuotaManagerProxy> quota_manager_proxy_;
  scoped_ptr<std::set<GURL> > origin_set_;
  OriginToSizeMap origin_size_map_;
  OriginToSizeMap space_available_map_;
  std::map<GURL, unsigned int> connection_count_;

  DISALLOW_COPY_AND_ASSIGN(IndexedDBContextImpl);
};

#endif  // CONTENT_BROWSER_IN_PROCESS_WEBKIT_INDEXED_DB_CONTEXT_IMPL_H_
