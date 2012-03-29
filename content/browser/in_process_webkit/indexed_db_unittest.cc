// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_util.h"
#include "base/scoped_temp_dir.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/browser/browser_thread_impl.h"
#include "content/browser/in_process_webkit/indexed_db_context_impl.h"
#include "content/public/common/url_constants.h"
#include "content/test/test_browser_context.h"
#include "webkit/database/database_util.h"
#include "webkit/quota/mock_special_storage_policy.h"
#include "webkit/quota/special_storage_policy.h"

using content::BrowserContext;
using content::BrowserThread;
using content::BrowserThreadImpl;
using webkit_database::DatabaseUtil;

class IndexedDBTest : public testing::Test {
 public:
  IndexedDBTest()
      : message_loop_(MessageLoop::TYPE_IO),
        webkit_thread_(BrowserThread::WEBKIT_DEPRECATED, &message_loop_),
        file_thread_(BrowserThread::FILE_USER_BLOCKING, &message_loop_),
        io_thread_(BrowserThread::IO, &message_loop_) {
  }

 protected:
  MessageLoop message_loop_;

 private:
  BrowserThreadImpl webkit_thread_;
  BrowserThreadImpl file_thread_;
  BrowserThreadImpl io_thread_;
};

TEST_F(IndexedDBTest, ClearLocalState) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  FilePath protected_path;
  FilePath unprotected_path;

  // Create the scope which will ensure we run the destructor of the webkit
  // context which should trigger the clean up.
  {
    TestBrowserContext browser_context;

    // Test our assumptions about what is protected and what is not.
    const GURL kProtectedOrigin("https://foo/");
    const GURL kUnprotectedOrigin("http://foo/");
    scoped_refptr<quota::MockSpecialStoragePolicy> special_storage_policy =
        new quota::MockSpecialStoragePolicy;
    special_storage_policy->AddProtected(kProtectedOrigin);
    browser_context.SetSpecialStoragePolicy(special_storage_policy);
    quota::SpecialStoragePolicy* policy =
        browser_context.GetSpecialStoragePolicy();
    ASSERT_TRUE(policy->IsStorageProtected(kProtectedOrigin));
    ASSERT_FALSE(policy->IsStorageProtected(kUnprotectedOrigin));

    // Create some indexedDB paths.
    // With the levelDB backend, these are directories.
    IndexedDBContextImpl* idb_context =
        static_cast<IndexedDBContextImpl*>(
            BrowserContext::GetIndexedDBContext(&browser_context));
    idb_context->set_data_path_for_testing(temp_dir.path());
    protected_path = idb_context->GetFilePathForTesting(
        DatabaseUtil::GetOriginIdentifier(kProtectedOrigin));
    unprotected_path = idb_context->GetFilePathForTesting(
        DatabaseUtil::GetOriginIdentifier(kUnprotectedOrigin));
    ASSERT_TRUE(file_util::CreateDirectory(protected_path));
    ASSERT_TRUE(file_util::CreateDirectory(unprotected_path));

    // Setup to clear all unprotected origins on exit.
    idb_context->set_clear_local_state_on_exit(true);
    message_loop_.RunAllPending();
  }

  // Make sure we wait until the destructor has run.
  message_loop_.RunAllPending();

  ASSERT_TRUE(file_util::DirectoryExists(protected_path));
  ASSERT_FALSE(file_util::DirectoryExists(unprotected_path));
}

TEST_F(IndexedDBTest, ClearSessionOnlyDatabases) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  FilePath normal_path;
  FilePath session_only_path;

  // Create the scope which will ensure we run the destructor of the webkit
  // context which should trigger the clean up.
  {
    TestBrowserContext browser_context;

    const GURL kNormalOrigin("http://normal/");
    const GURL kSessionOnlyOrigin("http://session-only/");
    scoped_refptr<quota::MockSpecialStoragePolicy> special_storage_policy =
        new quota::MockSpecialStoragePolicy;
    special_storage_policy->AddSessionOnly(kSessionOnlyOrigin);

    // Create some indexedDB paths.
    // With the levelDB backend, these are directories.
    IndexedDBContextImpl* idb_context =
        static_cast<IndexedDBContextImpl*>(
            BrowserContext::GetIndexedDBContext(&browser_context));

    // Override the storage policy with our own.
    idb_context->special_storage_policy_ = special_storage_policy;
    idb_context->set_data_path_for_testing(temp_dir.path());

    normal_path = idb_context->GetFilePathForTesting(
        DatabaseUtil::GetOriginIdentifier(kNormalOrigin));
    session_only_path = idb_context->GetFilePathForTesting(
        DatabaseUtil::GetOriginIdentifier(kSessionOnlyOrigin));
    ASSERT_TRUE(file_util::CreateDirectory(normal_path));
    ASSERT_TRUE(file_util::CreateDirectory(session_only_path));
    message_loop_.RunAllPending();
  }

  message_loop_.RunAllPending();

  EXPECT_TRUE(file_util::DirectoryExists(normal_path));
  EXPECT_FALSE(file_util::DirectoryExists(session_only_path));
}

TEST_F(IndexedDBTest, SaveSessionState) {
  ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  FilePath normal_path;
  FilePath session_only_path;

  // Create the scope which will ensure we run the destructor of the webkit
  // context.
  {
    TestBrowserContext browser_context;

    const GURL kNormalOrigin("http://normal/");
    const GURL kSessionOnlyOrigin("http://session-only/");
    scoped_refptr<quota::MockSpecialStoragePolicy> special_storage_policy =
        new quota::MockSpecialStoragePolicy;
    special_storage_policy->AddSessionOnly(kSessionOnlyOrigin);

    // Create some indexedDB paths.
    // With the levelDB backend, these are directories.
    IndexedDBContextImpl* idb_context =
        static_cast<IndexedDBContextImpl*>(
            BrowserContext::GetIndexedDBContext(&browser_context));

    // Override the storage policy with our own.
    idb_context->special_storage_policy_ = special_storage_policy;
    idb_context->set_clear_local_state_on_exit(true);
    idb_context->set_data_path_for_testing(temp_dir.path());

    // Save session state. This should bypass the destruction-time deletion.
    idb_context->SaveSessionState();

    normal_path = idb_context->GetFilePathForTesting(
        DatabaseUtil::GetOriginIdentifier(kNormalOrigin));
    session_only_path = idb_context->GetFilePathForTesting(
        DatabaseUtil::GetOriginIdentifier(kSessionOnlyOrigin));
    ASSERT_TRUE(file_util::CreateDirectory(normal_path));
    ASSERT_TRUE(file_util::CreateDirectory(session_only_path));
    message_loop_.RunAllPending();
  }

  // Make sure we wait until the destructor has run.
  message_loop_.RunAllPending();

  // No data was cleared because of SaveSessionState.
  EXPECT_TRUE(file_util::DirectoryExists(normal_path));
  EXPECT_TRUE(file_util::DirectoryExists(session_only_path));
}
