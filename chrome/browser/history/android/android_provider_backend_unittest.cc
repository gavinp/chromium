// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/history/android/android_provider_backend.h"

#include <vector>

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/memory/ref_counted.h"
#include "base/scoped_temp_dir.h"
#include "base/stringprintf.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/bookmarks/bookmark_service.h"
#include "chrome/browser/history/android/android_time.h"
#include "chrome/browser/history/history_backend.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_notification_types.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::Time;
using base::TimeDelta;

namespace history {

namespace {

struct BookmarkCacheRow {
 public:
  BookmarkCacheRow()
      : url_id_(0),
        bookmark_(false),
        favicon_id_(0) {
  }
  URLID url_id_;
  Time create_time_;
  Time last_visit_time_;
  bool bookmark_;
  FaviconID favicon_id_;
};

}  // namespace

class AndroidProviderBackendDelegate : public HistoryBackend::Delegate {
 public:
  AndroidProviderBackendDelegate() {}

  virtual void NotifyProfileError(int backend_id,
                                  sql::InitStatus init_status) OVERRIDE {}
  virtual void SetInMemoryBackend(int backend_id,
                                  InMemoryHistoryBackend* backend) OVERRIDE {}
  virtual void BroadcastNotifications(int type,
                                      HistoryDetails* details) OVERRIDE {
    switch (type) {
      case chrome::NOTIFICATION_HISTORY_URLS_DELETED:
        deleted_details_.reset(static_cast<URLsDeletedDetails*>(details));
        break;
      case chrome::NOTIFICATION_FAVICON_CHANGED:
        favicon_details_.reset(static_cast<FaviconChangeDetails*>(details));
        break;
      case chrome::NOTIFICATION_HISTORY_TYPED_URLS_MODIFIED:
        modified_details_.reset(static_cast<URLsModifiedDetails*>(details));
        break;
    }
  }
  virtual void DBLoaded(int backend_id) OVERRIDE {}
  virtual void StartTopSitesMigration(int backend_id) OVERRIDE {}

  URLsDeletedDetails* deleted_details() const {
    return deleted_details_.get();
  }

  URLsModifiedDetails* modified_details() const {
    return modified_details_.get();
  }

  FaviconChangeDetails* favicon_details() const {
    return favicon_details_.get();
  }

  void ResetDetails() {
    deleted_details_.reset();
    modified_details_.reset();
    favicon_details_.reset();
  }

 private:
  scoped_ptr<URLsDeletedDetails> deleted_details_;
  scoped_ptr<URLsModifiedDetails> modified_details_;
  scoped_ptr<FaviconChangeDetails> favicon_details_;

  DISALLOW_COPY_AND_ASSIGN(AndroidProviderBackendDelegate);
};

class AndroidProviderBackendTest : public testing::Test {
 public:
  AndroidProviderBackendTest()
      : bookmark_model_(NULL) {
  }
  ~AndroidProviderBackendTest() {
  }

 protected:
  virtual void SetUp() OVERRIDE {
    // Get a temporary directory for the test DB files.
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    history_db_name_ = temp_dir_.path().AppendASCII(chrome::kHistoryFilename);
    thumbnail_db_name_ = temp_dir_.path().AppendASCII(
        chrome::kFaviconsFilename);
    android_cache_db_name_ = temp_dir_.path().AppendASCII(
        "TestAndroidCache.db");
    bookmark_temp_ = temp_dir_.path().AppendASCII("bookmark_temp");
    ASSERT_TRUE(file_util::CreateDirectory(bookmark_temp_));
  }

  void AddBookmark(const GURL& url) {
    const BookmarkNode* mobile_node = bookmark_model_.mobile_node();
    ASSERT_TRUE(mobile_node);
    ASSERT_TRUE(bookmark_model_.AddURL(mobile_node, 0, string16(), url));
  }

  bool GetAndroidURLsRows(std::vector<AndroidURLRow>* rows,
                          AndroidProviderBackend* backend) {
    sql::Statement statement(backend->db_->GetCachedStatement(SQL_FROM_HERE,
        "SELECT id, raw_url, url_id FROM android_urls ORDER BY url_id ASC"));

    while (statement.Step()) {
      AndroidURLRow row;
      row.id = statement.ColumnInt64(0);
      row.raw_url = statement.ColumnString(1);
      row.url_id = statement.ColumnInt64(2);
      rows->push_back(row);
    }
    return true;
  }

  bool GetBookmarkCacheRows(std::vector<BookmarkCacheRow>* rows,
                            AndroidProviderBackend* backend) {
    sql::Statement statement(backend->db_->GetCachedStatement(SQL_FROM_HERE,
        "SELECT created_time, last_visit_time, url_id, bookmark, favicon_id "
        "FROM android_cache_db.bookmark_cache ORDER BY url_id ASC"));

    while (statement.Step()) {
      BookmarkCacheRow row;
      row.create_time_ = MillisecondsToTime(statement.ColumnInt64(0));
      row.last_visit_time_ = MillisecondsToTime(statement.ColumnInt64(1));
      row.url_id_ = statement.ColumnInt64(2);
      row.bookmark_ = statement.ColumnBool(3);
      row.favicon_id_ = statement.ColumnInt64(4);
      rows->push_back(row);
    }
    return true;
  }

  AndroidProviderBackendDelegate delegate_;
  scoped_refptr<HistoryBackend> history_backend_;
  HistoryDatabase history_db_;
  ThumbnailDatabase thumbnail_db_;
  ScopedTempDir temp_dir_;
  FilePath android_cache_db_name_;
  FilePath history_db_name_;
  FilePath thumbnail_db_name_;
  FilePath bookmark_temp_;
  MessageLoop message_loop_;
  BookmarkModel bookmark_model_;

  DISALLOW_COPY_AND_ASSIGN(AndroidProviderBackendTest);
};

TEST_F(AndroidProviderBackendTest, UpdateTables) {
  GURL url1("http://www.cnn.com");
  URLID url_id1 = 0;
  std::vector<VisitInfo> visits1;
  Time last_visited1 = Time::Now() - TimeDelta::FromDays(1);
  Time created1 = last_visited1 - TimeDelta::FromDays(20);
  visits1.push_back(VisitInfo(created1, content::PAGE_TRANSITION_LINK));
  visits1.push_back(VisitInfo(last_visited1 - TimeDelta::FromDays(1),
                              content::PAGE_TRANSITION_LINK));
  visits1.push_back(VisitInfo(last_visited1, content::PAGE_TRANSITION_LINK));

  GURL url2("http://www.example.com");
  URLID url_id2 = 0;
  std::vector<VisitInfo> visits2;
  Time last_visited2 = Time::Now();
  Time created2 = last_visited2 - TimeDelta::FromDays(10);
  visits2.push_back(VisitInfo(created2, content::PAGE_TRANSITION_LINK));
  visits2.push_back(VisitInfo(last_visited2 - TimeDelta::FromDays(5),
                              content::PAGE_TRANSITION_LINK));
  visits2.push_back(VisitInfo(last_visited2, content::PAGE_TRANSITION_LINK));

  // Only use the HistoryBackend to generate the test data.
  // HistoryBackend will shutdown after that.
  {
  scoped_refptr<HistoryBackend> history_backend;
  history_backend = new HistoryBackend(temp_dir_.path(), 0,
      new AndroidProviderBackendDelegate(), &bookmark_model_);
  history_backend->Init(std::string(), false);
  history_backend->AddVisits(url1, visits1, history::SOURCE_SYNCED);
  history_backend->AddVisits(url2, visits2, history::SOURCE_SYNCED);
  URLRow url_row;

  ASSERT_TRUE(history_backend->GetURL(url1, &url_row));
  url_id1 = url_row.id();
  ASSERT_TRUE(history_backend->GetURL(url2, &url_row));
  url_id2 = url_row.id();

  // Set favicon to url2.
  std::vector<unsigned char> data;
  data.push_back('1');
  history_backend->SetFavicon(url2, GURL(), new RefCountedBytes(data), FAVICON);
  history_backend->Closing();
  }

  // The history_db_name and thumbnail_db_name files should be created by
  // HistoryBackend. We need to open the same database files.
  ASSERT_TRUE(file_util::PathExists(history_db_name_));
  ASSERT_TRUE(file_util::PathExists(thumbnail_db_name_));

  ASSERT_EQ(sql::INIT_OK, history_db_.Init(history_db_name_, bookmark_temp_));
  ASSERT_EQ(sql::INIT_OK, thumbnail_db_.Init(thumbnail_db_name_, NULL,
                                             &history_db_));
  // Set url1 as bookmark.
  AddBookmark(url1);
  scoped_ptr<AndroidProviderBackend> backend(
      new AndroidProviderBackend(android_cache_db_name_, &history_db_,
                                 &thumbnail_db_, &bookmark_model_, &delegate_));

  ASSERT_TRUE(backend->EnsureInitializedAndUpdated());

  std::vector<AndroidURLRow> android_url_rows;
  ASSERT_TRUE(GetAndroidURLsRows(&android_url_rows, backend.get()));
  ASSERT_EQ(2u, android_url_rows.size());
  std::vector<AndroidURLRow>::iterator i = android_url_rows.begin();
  EXPECT_EQ(url_id1, i->url_id);
  EXPECT_EQ(url1.spec(), i->raw_url);
  i++;
  EXPECT_EQ(url_id2, i->url_id);
  EXPECT_EQ(url2.spec(), i->raw_url);

  std::vector<BookmarkCacheRow> bookmark_cache_rows;
  ASSERT_TRUE(GetBookmarkCacheRows(&bookmark_cache_rows, backend.get()));
  ASSERT_EQ(2u, bookmark_cache_rows.size());
  std::vector<BookmarkCacheRow>::const_iterator j = bookmark_cache_rows.begin();
  EXPECT_EQ(url_id1, j->url_id_);
  EXPECT_EQ(ToMilliseconds(last_visited1), ToMilliseconds(j->last_visit_time_));
  EXPECT_EQ(ToMilliseconds(created1), ToMilliseconds(j->create_time_));
  EXPECT_EQ(0, j->favicon_id_);
  EXPECT_TRUE(j->bookmark_);
  j++;
  EXPECT_EQ(url_id2, j->url_id_);
  EXPECT_EQ(ToMilliseconds(last_visited2), ToMilliseconds(j->last_visit_time_));
  EXPECT_EQ(ToMilliseconds(created2), ToMilliseconds(j->create_time_));
  EXPECT_NE(0, j->favicon_id_);
  EXPECT_FALSE(j->bookmark_);

  // Delete url2 from database.
  ASSERT_TRUE(history_db_.DeleteURLRow(url_id2));
  VisitVector visit_rows;
  ASSERT_TRUE(history_db_.GetMostRecentVisitsForURL(url_id2, 10,
                                                    &visit_rows));
  ASSERT_EQ(3u, visit_rows.size());
  for (VisitVector::const_iterator v = visit_rows.begin();
       v != visit_rows.end(); v++)
    history_db_.DeleteVisit(*v);

  backend->UpdateTables();

  android_url_rows.clear();
  ASSERT_TRUE(GetAndroidURLsRows(&android_url_rows, backend.get()));
  ASSERT_EQ(1u, android_url_rows.size());
  i = android_url_rows.begin();
  EXPECT_EQ(url_id1, i->url_id);
  EXPECT_EQ(url1.spec(), i->raw_url);

  bookmark_cache_rows.clear();
  ASSERT_TRUE(GetBookmarkCacheRows(&bookmark_cache_rows, backend.get()));
  ASSERT_EQ(1u, bookmark_cache_rows.size());
  j = bookmark_cache_rows.begin();
  EXPECT_EQ(url_id1, j->url_id_);
  EXPECT_EQ(ToMilliseconds(last_visited1), ToMilliseconds(j->last_visit_time_));
  EXPECT_EQ(ToMilliseconds(created1), ToMilliseconds(j->create_time_));
  EXPECT_EQ(0, j->favicon_id_);
  EXPECT_TRUE(j->bookmark_);
}

TEST_F(AndroidProviderBackendTest, QueryBookmarks) {
  GURL url1("http://www.cnn.com");
  URLID url_id1 = 0;
  const string16 title1(UTF8ToUTF16("cnn"));
  std::vector<VisitInfo> visits1;
  Time last_visited1 = Time::Now() - TimeDelta::FromDays(1);
  Time created1 = last_visited1 - TimeDelta::FromDays(20);
  visits1.push_back(VisitInfo(created1, content::PAGE_TRANSITION_LINK));
  visits1.push_back(VisitInfo(last_visited1 - TimeDelta::FromDays(1),
                              content::PAGE_TRANSITION_LINK));
  visits1.push_back(VisitInfo(last_visited1, content::PAGE_TRANSITION_LINK));

  GURL url2("http://www.example.com");
  URLID url_id2 = 0;
  std::vector<VisitInfo> visits2;
  const string16 title2(UTF8ToUTF16("example"));
  Time last_visited2 = Time::Now();
  Time created2 = last_visited2 - TimeDelta::FromDays(10);
  visits2.push_back(VisitInfo(created2, content::PAGE_TRANSITION_LINK));
  visits2.push_back(VisitInfo(last_visited2 - TimeDelta::FromDays(5),
                              content::PAGE_TRANSITION_LINK));
  visits2.push_back(VisitInfo(last_visited2, content::PAGE_TRANSITION_LINK));

  // Only use the HistoryBackend to generate the test data.
  // HistoryBackend will shutdown after that.
  {
  scoped_refptr<HistoryBackend> history_backend;
  history_backend = new HistoryBackend(temp_dir_.path(), 0,
      new AndroidProviderBackendDelegate(), &bookmark_model_);
  history_backend->Init(std::string(), false);
  history_backend->AddVisits(url1, visits1, history::SOURCE_SYNCED);
  history_backend->AddVisits(url2, visits2, history::SOURCE_SYNCED);
  URLRow url_row;

  ASSERT_TRUE(history_backend->GetURL(url1, &url_row));
  url_id1 = url_row.id();
  url_row.set_title(title1);
  ASSERT_TRUE(history_backend->UpdateURL(url_id1, url_row));

  ASSERT_TRUE(history_backend->GetURL(url2, &url_row));
  url_id2 = url_row.id();
  url_row.set_title(title2);
  ASSERT_TRUE(history_backend->UpdateURL(url_id2, url_row));

  // Set favicon to url2.
  std::vector<unsigned char> data;
  data.push_back('1');
  history_backend->SetFavicon(url2, GURL(), new RefCountedBytes(data), FAVICON);
  history_backend->Closing();
  }

  // The history_db_name and thumbnail_db_name files should be created by
  // HistoryBackend. We need to open the same database files.
  ASSERT_TRUE(file_util::PathExists(history_db_name_));
  ASSERT_TRUE(file_util::PathExists(thumbnail_db_name_));

  ASSERT_EQ(sql::INIT_OK, history_db_.Init(history_db_name_, bookmark_temp_));
  ASSERT_EQ(sql::INIT_OK, thumbnail_db_.Init(thumbnail_db_name_, NULL,
                                             &history_db_));
  // Set url1 as bookmark.
  AddBookmark(url1);

  scoped_ptr<AndroidProviderBackend> backend(
      new AndroidProviderBackend(android_cache_db_name_, &history_db_,
                                 &thumbnail_db_, &bookmark_model_, &delegate_));

  std::vector<BookmarkRow::BookmarkColumnID> projections;

  projections.push_back(BookmarkRow::ID);
  projections.push_back(BookmarkRow::URL);
  projections.push_back(BookmarkRow::TITLE);
  projections.push_back(BookmarkRow::CREATED);
  projections.push_back(BookmarkRow::LAST_VISIT_TIME);
  projections.push_back(BookmarkRow::VISIT_COUNT);
  projections.push_back(BookmarkRow::FAVICON);
  projections.push_back(BookmarkRow::BOOKMARK);

  scoped_ptr<AndroidStatement> statement(backend->QueryBookmarks(projections,
      std::string(), std::vector<string16>(), std::string("url ASC")));
  ASSERT_TRUE(statement->statement()->Step());
  ASSERT_EQ(url1, GURL(statement->statement()->ColumnString(1)));
  EXPECT_EQ(title1, statement->statement()->ColumnString16(2));
  EXPECT_EQ(ToMilliseconds(created1),
            statement->statement()->ColumnInt64(3));
  EXPECT_EQ(ToMilliseconds(last_visited1),
            statement->statement()->ColumnInt64(4));
  EXPECT_EQ(3, statement->statement()->ColumnInt(5));
  EXPECT_EQ(6, statement->favicon_index());
  // No favicon.
  EXPECT_EQ(0, statement->statement()->ColumnByteLength(6));
  EXPECT_TRUE(statement->statement()->ColumnBool(7));

  ASSERT_TRUE(statement->statement()->Step());
  EXPECT_EQ(title2, statement->statement()->ColumnString16(2));
  ASSERT_EQ(url2, GURL(statement->statement()->ColumnString(1)));
  EXPECT_EQ(ToMilliseconds(created2),
            statement->statement()->ColumnInt64(3));
  EXPECT_EQ(ToMilliseconds(last_visited2),
            statement->statement()->ColumnInt64(4));
  EXPECT_EQ(3, statement->statement()->ColumnInt(5));
  std::vector<unsigned char> favicon2;
  EXPECT_EQ(6, statement->favicon_index());
  // Has favicon.
  EXPECT_NE(0, statement->statement()->ColumnByteLength(6));
  EXPECT_FALSE(statement->statement()->ColumnBool(7));

  // No more row.
  EXPECT_FALSE(statement->statement()->Step());
}

TEST_F(AndroidProviderBackendTest, InsertBookmark) {
  BookmarkRow row1;
  row1.set_raw_url("cnn.com");
  row1.set_url(GURL("http://cnn.com"));
  row1.set_last_visit_time(Time::Now() - TimeDelta::FromDays(1));
  row1.set_created(Time::Now() - TimeDelta::FromDays(20));
  row1.set_visit_count(10);
  row1.set_is_bookmark(true);
  row1.set_title(UTF8ToUTF16("cnn"));

  BookmarkRow row2;
  row2.set_raw_url("http://www.example.com");
  row2.set_url(GURL("http://www.example.com"));
  row2.set_last_visit_time(Time::Now() - TimeDelta::FromDays(10));
  row2.set_is_bookmark(false);
  row2.set_title(UTF8ToUTF16("example"));
  std::vector<unsigned char> data;
  data.push_back('1');
  row2.set_favicon(data);

  ASSERT_EQ(sql::INIT_OK, history_db_.Init(history_db_name_, bookmark_temp_));
  ASSERT_EQ(sql::INIT_OK, thumbnail_db_.Init(thumbnail_db_name_, NULL,
                                             &history_db_));
  scoped_ptr<AndroidProviderBackend> backend(
      new AndroidProviderBackend(android_cache_db_name_, &history_db_,
                                 &thumbnail_db_, &bookmark_model_, &delegate_));

  ASSERT_TRUE(backend->InsertBookmark(row1));
  EXPECT_FALSE(delegate_.deleted_details());
  ASSERT_TRUE(delegate_.modified_details());
  ASSERT_EQ(1u, delegate_.modified_details()->changed_urls.size());
  EXPECT_EQ(row1.url(), delegate_.modified_details()->changed_urls[0].url());
  EXPECT_EQ(row1.last_visit_time(),
            delegate_.modified_details()->changed_urls[0].last_visit());
  EXPECT_EQ(row1.visit_count(),
            delegate_.modified_details()->changed_urls[0].visit_count());
  EXPECT_EQ(row1.title(),
            delegate_.modified_details()->changed_urls[0].title());
  EXPECT_FALSE(delegate_.favicon_details());

  delegate_.ResetDetails();
  ASSERT_TRUE(backend->InsertBookmark(row2));
  EXPECT_FALSE(delegate_.deleted_details());
  ASSERT_TRUE(delegate_.modified_details());
  ASSERT_EQ(1u, delegate_.modified_details()->changed_urls.size());
  EXPECT_EQ(row2.url(), delegate_.modified_details()->changed_urls[0].url());
  EXPECT_EQ(row2.last_visit_time(),
            delegate_.modified_details()->changed_urls[0].last_visit());
  EXPECT_EQ(row2.title(),
            delegate_.modified_details()->changed_urls[0].title());
  ASSERT_TRUE(delegate_.favicon_details());
  ASSERT_EQ(1u, delegate_.favicon_details()->urls.size());
  ASSERT_NE(delegate_.favicon_details()->urls.end(),
            delegate_.favicon_details()->urls.find(row2.url()));

  // Set url1 as bookmark.
  AddBookmark(row1.url());

  std::vector<BookmarkRow::BookmarkColumnID> projections;
  projections.push_back(BookmarkRow::ID);
  projections.push_back(BookmarkRow::URL);
  projections.push_back(BookmarkRow::TITLE);
  projections.push_back(BookmarkRow::CREATED);
  projections.push_back(BookmarkRow::LAST_VISIT_TIME);
  projections.push_back(BookmarkRow::VISIT_COUNT);
  projections.push_back(BookmarkRow::FAVICON);
  projections.push_back(BookmarkRow::BOOKMARK);

  scoped_ptr<AndroidStatement> statement(backend->QueryBookmarks(projections,
      std::string(), std::vector<string16>(), std::string("url ASC")));
  ASSERT_TRUE(statement->statement()->Step());
  ASSERT_EQ(row1.raw_url(), statement->statement()->ColumnString(1));
  EXPECT_EQ(row1.title(), statement->statement()->ColumnString16(2));
  EXPECT_EQ(ToMilliseconds(row1.created()),
            statement->statement()->ColumnInt64(3));
  EXPECT_EQ(ToMilliseconds(row1.last_visit_time()),
            statement->statement()->ColumnInt64(4));
  EXPECT_EQ(row1.visit_count(), statement->statement()->ColumnInt(5));
  EXPECT_EQ(6, statement->favicon_index());
  // No favicon.
  EXPECT_EQ(0, statement->statement()->ColumnByteLength(6));

  // TODO: Find a way to test the bookmark was added in BookmarkModel.
  // The bookmark was added in UI thread, there is no good way to test it.
  EXPECT_TRUE(statement->statement()->ColumnBool(7));

  ASSERT_TRUE(statement->statement()->Step());
  EXPECT_EQ(row2.title(), statement->statement()->ColumnString16(2));
  EXPECT_EQ(row2.url(), GURL(statement->statement()->ColumnString(1)));
  EXPECT_EQ(ToMilliseconds(row2.last_visit_time()),
            statement->statement()->ColumnInt64(3));
  EXPECT_EQ(ToMilliseconds(row2.last_visit_time()),
            statement->statement()->ColumnInt64(4));
  EXPECT_EQ(1, statement->statement()->ColumnInt(5));
  EXPECT_EQ(6, statement->favicon_index());
  // Has favicon.
  EXPECT_NE(0, statement->statement()->ColumnByteLength(6));
  // TODO: Find a way to test the bookmark was added in BookmarkModel.
  // The bookmark was added in UI thread, there is no good way to test it.
  EXPECT_FALSE(statement->statement()->ColumnBool(7));

  // No more row.
  EXPECT_FALSE(statement->statement()->Step());
}

TEST_F(AndroidProviderBackendTest, DeleteBookmarks) {
  BookmarkRow row1;
  row1.set_raw_url("cnn.com");
  row1.set_url(GURL("http://cnn.com"));
  row1.set_last_visit_time(Time::Now() - TimeDelta::FromDays(1));
  row1.set_created(Time::Now() - TimeDelta::FromDays(20));
  row1.set_visit_count(10);
  row1.set_is_bookmark(true);
  row1.set_title(UTF8ToUTF16("cnn"));

  BookmarkRow row2;
  row2.set_raw_url("http://www.example.com");
  row2.set_url(GURL("http://www.example.com"));
  row2.set_last_visit_time(Time::Now() - TimeDelta::FromDays(10));
  row2.set_is_bookmark(false);
  row2.set_title(UTF8ToUTF16("example"));
  std::vector<unsigned char> data;
  data.push_back('1');
  row2.set_favicon(data);

  ASSERT_EQ(sql::INIT_OK, history_db_.Init(history_db_name_, bookmark_temp_));
  ASSERT_EQ(sql::INIT_OK, thumbnail_db_.Init(thumbnail_db_name_, NULL,
                                             &history_db_));

  scoped_ptr<AndroidProviderBackend> backend(
      new AndroidProviderBackend(android_cache_db_name_, &history_db_,
                                 &thumbnail_db_, &bookmark_model_, &delegate_));

  ASSERT_TRUE(backend->InsertBookmark(row1));
  ASSERT_TRUE(backend->InsertBookmark(row2));
  // Set url1 as bookmark.
  AddBookmark(row1.url());

  // Delete the row1.
  std::vector<string16> args;
  int deleted_count = 0;
  delegate_.ResetDetails();
  ASSERT_TRUE(backend->DeleteBookmarks("Favicon IS NULL", args,
                                       &deleted_count));
  EXPECT_EQ(1, deleted_count);
  // Verify notifications
  ASSERT_TRUE(delegate_.deleted_details());
  EXPECT_FALSE(delegate_.modified_details());
  EXPECT_EQ(1u, delegate_.deleted_details()->rows.size());
  EXPECT_EQ(1u, delegate_.deleted_details()->urls.size());
  EXPECT_NE(delegate_.deleted_details()->urls.end(),
            delegate_.deleted_details()->urls.find(row1.url()));
  EXPECT_EQ(row1.url(), delegate_.deleted_details()->rows[0].url());
  EXPECT_EQ(row1.last_visit_time(),
            delegate_.deleted_details()->rows[0].last_visit());
  EXPECT_EQ(row1.title(),
            delegate_.deleted_details()->rows[0].title());
  EXPECT_FALSE(delegate_.favicon_details());

  std::vector<BookmarkRow::BookmarkColumnID> projections;
  projections.push_back(BookmarkRow::ID);
  projections.push_back(BookmarkRow::URL);
  projections.push_back(BookmarkRow::TITLE);
  projections.push_back(BookmarkRow::CREATED);
  projections.push_back(BookmarkRow::LAST_VISIT_TIME);
  projections.push_back(BookmarkRow::VISIT_COUNT);
  projections.push_back(BookmarkRow::FAVICON);
  projections.push_back(BookmarkRow::BOOKMARK);

  scoped_ptr<AndroidStatement> statement(backend->QueryBookmarks(projections,
      std::string(), std::vector<string16>(), std::string("url ASC")));
  ASSERT_TRUE(statement->statement()->Step());

  EXPECT_EQ(row2.title(), statement->statement()->ColumnString16(2));
  EXPECT_EQ(row2.url(), GURL(statement->statement()->ColumnString(1)));
  EXPECT_EQ(ToMilliseconds(row2.last_visit_time()),
            statement->statement()->ColumnInt64(3));
  EXPECT_EQ(ToMilliseconds(row2.last_visit_time()),
            statement->statement()->ColumnInt64(4));
  EXPECT_EQ(1, statement->statement()->ColumnInt(5));
  EXPECT_EQ(6, statement->favicon_index());
  // Has favicon.
  EXPECT_NE(0, statement->statement()->ColumnByteLength(6));
  // TODO: Find a way to test the bookmark was added in BookmarkModel.
  // The bookmark was added in UI thread, there is no good way to test it.
  EXPECT_FALSE(statement->statement()->ColumnBool(7));
  // No more row.
  EXPECT_FALSE(statement->statement()->Step());

  deleted_count = 0;
  // Delete row2.
  delegate_.ResetDetails();
  ASSERT_TRUE(backend->DeleteBookmarks("bookmark = 0", std::vector<string16>(),
                                       &deleted_count));
  // Verify notifications
  ASSERT_TRUE(delegate_.deleted_details());
  EXPECT_FALSE(delegate_.modified_details());
  EXPECT_EQ(1u, delegate_.deleted_details()->rows.size());
  EXPECT_EQ(1u, delegate_.deleted_details()->urls.size());
  EXPECT_NE(delegate_.deleted_details()->urls.end(),
            delegate_.deleted_details()->urls.find(row2.url()));
  EXPECT_EQ(row2.url(), delegate_.deleted_details()->rows[0].url());
  EXPECT_EQ(row2.last_visit_time(),
            delegate_.deleted_details()->rows[0].last_visit());
  EXPECT_EQ(row2.title(),
            delegate_.deleted_details()->rows[0].title());
  ASSERT_TRUE(delegate_.favicon_details());
  ASSERT_EQ(1u, delegate_.favicon_details()->urls.size());
  ASSERT_NE(delegate_.favicon_details()->urls.end(),
            delegate_.favicon_details()->urls.find(row2.url()));

  ASSERT_EQ(1, deleted_count);
  scoped_ptr<AndroidStatement> statement1(backend->QueryBookmarks(projections,
      std::string(), std::vector<string16>(), std::string("url ASC")));
  ASSERT_FALSE(statement1->statement()->Step());
}

TEST_F(AndroidProviderBackendTest, IsValidBookmarkRow) {
  ASSERT_EQ(sql::INIT_OK, history_db_.Init(history_db_name_, bookmark_temp_));
  ASSERT_EQ(sql::INIT_OK, thumbnail_db_.Init(thumbnail_db_name_, NULL,
                                             &history_db_));
  scoped_ptr<AndroidProviderBackend> backend(
      new AndroidProviderBackend(android_cache_db_name_, &history_db_,
                                 &thumbnail_db_, &bookmark_model_, &delegate_));

  // The created time and last visit time are too close to have required visit
  // count.
  BookmarkRow row1;
  row1.set_raw_url("cnn.com");
  row1.set_url(GURL("http://cnn.com"));
  row1.set_last_visit_time(Time::Now() - TimeDelta::FromDays(1));
  row1.set_created(Time::FromInternalValue(
      row1.last_visit_time().ToInternalValue() - 1));
  row1.set_visit_count(10);
  row1.set_is_bookmark(true);
  row1.set_title(UTF8ToUTF16("cnn"));
  EXPECT_FALSE(backend->InsertBookmark(row1));

  // Have different created time and last visit time, but only have 1 visit
  // count.
  BookmarkRow row2;
  row2.set_raw_url("http://www.example.com");
  row2.set_url(GURL("http://www.example.com"));
  row2.set_last_visit_time(Time::Now() - TimeDelta::FromDays(10));
  row2.set_created(Time::Now() - TimeDelta::FromDays(11));
  row2.set_visit_count(1);
  EXPECT_FALSE(backend->InsertBookmark(row2));

  // Have created time in the future.
  BookmarkRow row3;
  row3.set_raw_url("http://www.example.com");
  row3.set_url(GURL("http://www.example.com"));
  row3.set_created(Time::Now() + TimeDelta::FromDays(11));
  EXPECT_FALSE(backend->InsertBookmark(row3));

  // Have last vist time in the future.
  BookmarkRow row4;
  row4.set_raw_url("http://www.example.com");
  row4.set_url(GURL("http://www.example.com"));
  row4.set_last_visit_time(Time::Now() + TimeDelta::FromDays(11));
  EXPECT_FALSE(backend->InsertBookmark(row4));

  // Created time is larger than last visit time.
  BookmarkRow row5;
  row5.set_raw_url("http://www.example.com");
  row5.set_url(GURL("http://www.example.com"));
  row5.set_last_visit_time(Time::Now());
  row5.set_created(Time::Now() + TimeDelta::FromDays(11));
  EXPECT_FALSE(backend->InsertBookmark(row5));

  // Visit count is zero, and last visit time is not zero.
  BookmarkRow row6;
  row6.set_raw_url("http://www.example.com");
  row6.set_url(GURL("http://www.example.com"));
  row6.set_visit_count(0);
  row6.set_last_visit_time(Time::Now());
  EXPECT_FALSE(backend->InsertBookmark(row6));

  // Visit count is zero, and create time is not zero.
  BookmarkRow row7;
  row7.set_raw_url("http://www.example.com");
  row7.set_url(GURL("http://www.example.com"));
  row7.set_visit_count(0);
  row7.set_created(Time::Now());
  EXPECT_FALSE(backend->InsertBookmark(row7));
}

TEST_F(AndroidProviderBackendTest, UpdateURL) {
  BookmarkRow row1;
  row1.set_raw_url("cnn.com");
  row1.set_url(GURL("http://cnn.com"));
  row1.set_last_visit_time(Time::Now() - TimeDelta::FromDays(1));
  row1.set_created(Time::Now() - TimeDelta::FromDays(20));
  row1.set_visit_count(10);
  row1.set_is_bookmark(true);
  row1.set_title(UTF8ToUTF16("cnn"));

  BookmarkRow row2;
  row2.set_raw_url("http://www.example.com");
  row2.set_url(GURL("http://www.example.com"));
  row2.set_last_visit_time(Time::Now() - TimeDelta::FromDays(10));
  row2.set_is_bookmark(false);
  row2.set_title(UTF8ToUTF16("example"));
  std::vector<unsigned char> data;
  data.push_back('1');
  row2.set_favicon(data);

  ASSERT_EQ(sql::INIT_OK, history_db_.Init(history_db_name_, bookmark_temp_));
  ASSERT_EQ(sql::INIT_OK, thumbnail_db_.Init(thumbnail_db_name_, NULL,
                                             &history_db_));
  scoped_ptr<AndroidProviderBackend> backend(
      new AndroidProviderBackend(android_cache_db_name_, &history_db_,
                                 &thumbnail_db_, &bookmark_model_, &delegate_));

  AndroidURLID id1 = backend->InsertBookmark(row1);
  ASSERT_TRUE(id1);
  AndroidURLID id2 = backend->InsertBookmark(row2);
  ASSERT_TRUE(id2);

  // Set url1 as bookmark.
  AddBookmark(row1.url());

  // Make sure the url has correctly insertted.
  URLID url_id1 = history_db_.GetRowForURL(row1.url(), NULL);
  ASSERT_TRUE(url_id1);
  URLID url_id2 = history_db_.GetRowForURL(row2.url(), NULL);
  ASSERT_TRUE(url_id2);

  // Make sure we have the correct visit rows in visit table.
  VisitVector visits;
  ASSERT_TRUE(history_db_.GetVisitsForURL(url_id1, &visits));
  ASSERT_EQ(10u, visits.size());
  visits.clear();
  ASSERT_TRUE(history_db_.GetVisitsForURL(url_id2, &visits));
  ASSERT_EQ(1u, visits.size());

  int update_count;
  std::vector<string16> update_args;
  // Try to update the mutiple rows with the same URL, this should failed.
  BookmarkRow update_row1;
  update_row1.set_raw_url("newwebiste.com");
  update_row1.set_url(GURL("http://newwebsite.com"));
  update_args.clear();
  ASSERT_FALSE(backend->UpdateBookmarks(update_row1, std::string(),
                                        update_args, &update_count));

  // Only update one URL.
  update_args.clear();
  update_args.push_back(UTF8ToUTF16(row1.raw_url()));
  delegate_.ResetDetails();
  ASSERT_TRUE(backend->UpdateBookmarks(update_row1, "url = ?", update_args,
                                       &update_count));
  // Verify notifications, Update involves insert and delete URLS.
  ASSERT_TRUE(delegate_.deleted_details());
  EXPECT_EQ(1u, delegate_.deleted_details()->rows.size());
  EXPECT_EQ(1u, delegate_.deleted_details()->urls.size());
  EXPECT_NE(delegate_.deleted_details()->urls.end(),
            delegate_.deleted_details()->urls.find(row1.url()));
  EXPECT_EQ(row1.url(), delegate_.deleted_details()->rows[0].url());
  EXPECT_EQ(row1.last_visit_time(),
            delegate_.deleted_details()->rows[0].last_visit());
  EXPECT_EQ(row1.title(),
            delegate_.deleted_details()->rows[0].title());
  ASSERT_TRUE(delegate_.modified_details());
  ASSERT_EQ(1u, delegate_.modified_details()->changed_urls.size());
  EXPECT_EQ(update_row1.url(),
            delegate_.modified_details()->changed_urls[0].url());
  EXPECT_EQ(ToMilliseconds(row1.last_visit_time()),
            ToMilliseconds(
                delegate_.modified_details()->changed_urls[0].last_visit()));
  EXPECT_EQ(row1.title(),
            delegate_.modified_details()->changed_urls[0].title());
  EXPECT_FALSE(delegate_.favicon_details());

  EXPECT_EQ(1, update_count);
  // We shouldn't find orignal url anymore.
  EXPECT_FALSE(history_db_.GetRowForURL(row1.url(), NULL));
  visits.clear();
  EXPECT_TRUE(history_db_.GetVisitsForURL(url_id1, &visits));
  EXPECT_EQ(0u, visits.size());
  // Verify new URL.
  URLRow new_row;
  EXPECT_TRUE(history_db_.GetRowForURL(update_row1.url(), &new_row));
  EXPECT_EQ(10, new_row.visit_count());
  EXPECT_EQ(ToMilliseconds(row1.last_visit_time()),
            ToMilliseconds(new_row.last_visit()));
  visits.clear();
  EXPECT_TRUE(history_db_.GetVisitsForURL(new_row.id(), &visits));
  EXPECT_EQ(10u, visits.size());
  AndroidURLRow android_url_row1;
  ASSERT_TRUE(history_db_.GetAndroidURLRow(new_row.id(), &android_url_row1));
  // Android URL ID shouldn't change.
  EXPECT_EQ(id1, android_url_row1.id);

  // Update the URL with visit count, created time, and last visit time.
  BookmarkRow update_row2;
  update_row2.set_raw_url("somethingelse.com");
  update_row2.set_url(GURL("http://somethingelse.com"));
  update_row2.set_last_visit_time(Time::Now());
  update_row2.set_created(Time::Now() - TimeDelta::FromDays(20));
  update_row2.set_visit_count(10);

  update_args.clear();
  update_args.push_back(UTF8ToUTF16(row2.raw_url()));
  delegate_.ResetDetails();
  ASSERT_TRUE(backend->UpdateBookmarks(update_row2, "url = ?", update_args,
                                       &update_count));
  // Verify notifications, Update involves insert and delete URLS.
  ASSERT_TRUE(delegate_.deleted_details());
  EXPECT_EQ(1u, delegate_.deleted_details()->rows.size());
  EXPECT_EQ(1u, delegate_.deleted_details()->urls.size());
  EXPECT_NE(delegate_.deleted_details()->urls.end(),
            delegate_.deleted_details()->urls.find(row2.url()));
  EXPECT_EQ(row2.url(), delegate_.deleted_details()->rows[0].url());
  EXPECT_EQ(row2.last_visit_time(),
            delegate_.deleted_details()->rows[0].last_visit());
  EXPECT_EQ(row2.title(),
            delegate_.deleted_details()->rows[0].title());
  ASSERT_TRUE(delegate_.modified_details());
  ASSERT_EQ(1u, delegate_.modified_details()->changed_urls.size());
  EXPECT_EQ(update_row2.url(),
            delegate_.modified_details()->changed_urls[0].url());
  EXPECT_EQ(ToMilliseconds(update_row2.last_visit_time()),
            ToMilliseconds(
                delegate_.modified_details()->changed_urls[0].last_visit()));
  EXPECT_EQ(update_row2.visit_count(),
            delegate_.modified_details()->changed_urls[0].visit_count());
  ASSERT_TRUE(delegate_.favicon_details());
  ASSERT_EQ(2u, delegate_.favicon_details()->urls.size());
  ASSERT_NE(delegate_.favicon_details()->urls.end(),
            delegate_.favicon_details()->urls.find(row2.url()));
  ASSERT_NE(delegate_.favicon_details()->urls.end(),
            delegate_.favicon_details()->urls.find(update_row2.url()));

  EXPECT_EQ(1, update_count);
  // We shouldn't find orignal url anymore.
  EXPECT_FALSE(history_db_.GetRowForURL(row2.url(), NULL));
  visits.clear();
  EXPECT_TRUE(history_db_.GetVisitsForURL(url_id2, &visits));
  EXPECT_EQ(0u, visits.size());

  // Verify new URL.
  URLRow new_row2;
  ASSERT_TRUE(history_db_.GetRowForURL(update_row2.url(), &new_row2));
  EXPECT_EQ(10, new_row2.visit_count());
  EXPECT_EQ(update_row2.last_visit_time(), new_row2.last_visit());
  visits.clear();
  EXPECT_TRUE(history_db_.GetVisitsForURL(new_row2.id(), &visits));
  EXPECT_EQ(10u, visits.size());
  AndroidURLRow android_url_row2;
  ASSERT_TRUE(history_db_.GetAndroidURLRow(new_row2.id(), &android_url_row2));
  // Android URL ID shouldn't change.
  EXPECT_EQ(id2, android_url_row2.id);

  ASSERT_TRUE(history_db_.GetVisitsForURL(new_row2.id(), &visits));
  ASSERT_EQ(10u, visits.size());
  EXPECT_EQ(update_row2.created(), visits[0].visit_time);
  EXPECT_EQ(update_row2.last_visit_time(), visits[9].visit_time);
}

TEST_F(AndroidProviderBackendTest, UpdateVisitCount) {
  BookmarkRow row1;
  row1.set_raw_url("cnn.com");
  row1.set_url(GURL("http://cnn.com"));
  row1.set_last_visit_time(Time::Now() - TimeDelta::FromDays(1));
  row1.set_created(Time::Now() - TimeDelta::FromDays(20));
  row1.set_visit_count(10);
  row1.set_is_bookmark(true);
  row1.set_title(UTF8ToUTF16("cnn"));

  BookmarkRow row2;
  row2.set_raw_url("http://www.example.com");
  row2.set_url(GURL("http://www.example.com"));
  row2.set_last_visit_time(Time::Now() - TimeDelta::FromDays(10));
  row2.set_is_bookmark(false);
  row2.set_title(UTF8ToUTF16("example"));
  std::vector<unsigned char> data;
  data.push_back('1');
  row2.set_favicon(data);

  ASSERT_EQ(sql::INIT_OK, history_db_.Init(history_db_name_, bookmark_temp_));
  ASSERT_EQ(sql::INIT_OK, thumbnail_db_.Init(thumbnail_db_name_, NULL,
                                             &history_db_));
  scoped_ptr<AndroidProviderBackend> backend(
      new AndroidProviderBackend(android_cache_db_name_, &history_db_,
                                 &thumbnail_db_, &bookmark_model_, &delegate_));

  AndroidURLID id1 = backend->InsertBookmark(row1);
  ASSERT_TRUE(id1);
  AndroidURLID id2 = backend->InsertBookmark(row2);
  ASSERT_TRUE(id2);

  int update_count;
  std::vector<string16> update_args;
  // Update the visit_count to a value less than current one.
  BookmarkRow update_row1;
  update_row1.set_visit_count(5);
  update_args.push_back(UTF8ToUTF16(row1.raw_url()));
  delegate_.ResetDetails();
  ASSERT_TRUE(backend->UpdateBookmarks(update_row1, "url = ?", update_args,
                                       &update_count));
  // Verify notifications, Update involves modified URL.
  EXPECT_FALSE(delegate_.deleted_details());
  ASSERT_TRUE(delegate_.modified_details());
  ASSERT_EQ(1u, delegate_.modified_details()->changed_urls.size());
  EXPECT_EQ(row1.url(),
            delegate_.modified_details()->changed_urls[0].url());
  EXPECT_EQ(ToMilliseconds(row1.last_visit_time()),
            ToMilliseconds(
                delegate_.modified_details()->changed_urls[0].last_visit()));
  EXPECT_EQ(update_row1.visit_count(),
            delegate_.modified_details()->changed_urls[0].visit_count());
  EXPECT_FALSE(delegate_.favicon_details());

  // All visits should be removed, and 5 new visit insertted.
  URLRow new_row1;
  ASSERT_TRUE(history_db_.GetRowForURL(row1.url(), &new_row1));
  EXPECT_EQ(5, new_row1.visit_count());
  VisitVector visits;
  ASSERT_TRUE(history_db_.GetVisitsForURL(new_row1.id(), &visits));
  ASSERT_EQ(5u, visits.size());
  EXPECT_EQ(row1.last_visit_time(), visits[4].visit_time);
  EXPECT_GT(row1.last_visit_time(), visits[0].visit_time);

  // Update the visit_count to a value equal to current one.
  BookmarkRow update_row2;
  update_row2.set_visit_count(1);
  update_args.clear();
  update_args.push_back(UTF8ToUTF16(row2.raw_url()));
  ASSERT_TRUE(backend->UpdateBookmarks(update_row2, "url = ?", update_args,
                                       &update_count));
  // All shouldn't have any change.
  URLRow new_row2;
  ASSERT_TRUE(history_db_.GetRowForURL(row2.url(), &new_row2));
  EXPECT_EQ(1, new_row2.visit_count());

  ASSERT_TRUE(history_db_.GetVisitsForURL(new_row2.id(), &visits));
  ASSERT_EQ(1u, visits.size());
  EXPECT_EQ(row2.last_visit_time(), visits[0].visit_time);
}

TEST_F(AndroidProviderBackendTest, UpdateLastVisitTime) {
  BookmarkRow row1;
  row1.set_raw_url("cnn.com");
  row1.set_url(GURL("http://cnn.com"));
  row1.set_last_visit_time(Time::Now() - TimeDelta::FromDays(1));
  row1.set_created(Time::Now() - TimeDelta::FromDays(20));
  row1.set_visit_count(10);
  row1.set_is_bookmark(true);
  row1.set_title(UTF8ToUTF16("cnn"));

  BookmarkRow row2;
  row2.set_raw_url("http://www.example.com");
  row2.set_url(GURL("http://www.example.com"));
  row2.set_last_visit_time(Time::Now() - TimeDelta::FromDays(10));
  row2.set_is_bookmark(false);
  row2.set_title(UTF8ToUTF16("example"));
  std::vector<unsigned char> data;
  data.push_back('1');
  row2.set_favicon(data);

  ASSERT_EQ(sql::INIT_OK, history_db_.Init(history_db_name_, bookmark_temp_));
  ASSERT_EQ(sql::INIT_OK, thumbnail_db_.Init(thumbnail_db_name_, NULL,
                                             &history_db_));
  scoped_ptr<AndroidProviderBackend> backend(
      new AndroidProviderBackend(android_cache_db_name_, &history_db_,
                                 &thumbnail_db_, &bookmark_model_, &delegate_));

  AndroidURLID id1 = backend->InsertBookmark(row1);
  ASSERT_TRUE(id1);
  AndroidURLID id2 = backend->InsertBookmark(row2);
  ASSERT_TRUE(id2);

  int update_count;
  std::vector<string16> update_args;
  // Update the last visit time to a value greater than current one.
  BookmarkRow update_row1;
  update_row1.set_last_visit_time(Time::Now());
  update_args.push_back(UTF8ToUTF16(row1.raw_url()));
  delegate_.ResetDetails();
  ASSERT_TRUE(backend->UpdateBookmarks(update_row1, "url = ?", update_args,
                                       &update_count));
  // Verify notifications, Update involves modified URL.
  EXPECT_FALSE(delegate_.deleted_details());
  ASSERT_TRUE(delegate_.modified_details());
  ASSERT_EQ(1u, delegate_.modified_details()->changed_urls.size());
  EXPECT_EQ(row1.url(),
            delegate_.modified_details()->changed_urls[0].url());
  EXPECT_EQ(ToMilliseconds(update_row1.last_visit_time()),
            ToMilliseconds(
                delegate_.modified_details()->changed_urls[0].last_visit()));
  EXPECT_FALSE(delegate_.favicon_details());

  URLRow new_row1;
  ASSERT_TRUE(history_db_.GetRowForURL(row1.url(), &new_row1));
  EXPECT_EQ(11, new_row1.visit_count());
  EXPECT_EQ(update_row1.last_visit_time(), new_row1.last_visit());
  VisitVector visits;
  ASSERT_TRUE(history_db_.GetVisitsForURL(new_row1.id(), &visits));
  // 1 new visit insertted.
  ASSERT_EQ(11u, visits.size());
  EXPECT_EQ(update_row1.last_visit_time(), visits[10].visit_time);
  EXPECT_EQ(row1.last_visit_time(), visits[9].visit_time);

  // Update the visit_tim to a value less than to current one.
  BookmarkRow update_row2;
  update_row2.set_last_visit_time(Time::Now() - TimeDelta::FromDays(1));
  update_args.clear();
  update_args.push_back(UTF8ToUTF16(row1.raw_url()));
  ASSERT_FALSE(backend->UpdateBookmarks(update_row2, "url = ?", update_args,
                                        &update_count));
}

TEST_F(AndroidProviderBackendTest, UpdateFavicon) {
  BookmarkRow row1;
  row1.set_raw_url("cnn.com");
  row1.set_url(GURL("http://cnn.com"));
  row1.set_last_visit_time(Time::Now() - TimeDelta::FromDays(1));
  row1.set_created(Time::Now() - TimeDelta::FromDays(20));
  row1.set_visit_count(10);
  row1.set_is_bookmark(true);
  row1.set_title(UTF8ToUTF16("cnn"));

  ASSERT_EQ(sql::INIT_OK, history_db_.Init(history_db_name_, bookmark_temp_));
  ASSERT_EQ(sql::INIT_OK, thumbnail_db_.Init(thumbnail_db_name_, NULL,
                                             &history_db_));
  scoped_ptr<AndroidProviderBackend> backend(
      new AndroidProviderBackend(android_cache_db_name_, &history_db_,
                                 &thumbnail_db_, &bookmark_model_, &delegate_));

  AndroidURLID id1 = backend->InsertBookmark(row1);
  ASSERT_TRUE(id1);

  int update_count;
  std::vector<string16> update_args;
  // Update the last visit time to a value greater than current one.
  BookmarkRow update_row1;

  // Set favicon.
  std::vector<unsigned char> data;
  data.push_back('1');
  update_row1.set_favicon(data);
  update_args.push_back(UTF8ToUTF16(row1.raw_url()));
  delegate_.ResetDetails();
  ASSERT_TRUE(backend->UpdateBookmarks(update_row1, "url = ?", update_args,
                                       &update_count));
  // Verify notifications.
  EXPECT_FALSE(delegate_.deleted_details());
  EXPECT_FALSE(delegate_.modified_details());
  ASSERT_TRUE(delegate_.favicon_details());
  ASSERT_EQ(1u, delegate_.favicon_details()->urls.size());
  ASSERT_NE(delegate_.favicon_details()->urls.end(),
            delegate_.favicon_details()->urls.find(row1.url()));

  IconMapping icon_mapping;
  EXPECT_TRUE(thumbnail_db_.GetIconMappingForPageURL(row1.url(), FAVICON,
                                                     &icon_mapping));
  Time last_updated;
  std::vector<unsigned char> png_icon_data;
  EXPECT_TRUE(thumbnail_db_.GetFavicon(icon_mapping.icon_id, &last_updated,
                                       &png_icon_data, NULL));
  EXPECT_EQ(data, png_icon_data);

  // Remove favicon.
  BookmarkRow update_row2;

  // Set favicon.
  update_row1.set_favicon(std::vector<unsigned char>());
  update_args.clear();
  update_args.push_back(UTF8ToUTF16(row1.raw_url()));
  delegate_.ResetDetails();
  ASSERT_TRUE(backend->UpdateBookmarks(update_row1, "url = ?", update_args,
                                       &update_count));
  // Verify notifications.
  EXPECT_FALSE(delegate_.deleted_details());
  EXPECT_FALSE(delegate_.modified_details());
  ASSERT_TRUE(delegate_.favicon_details());
  ASSERT_EQ(1u, delegate_.favicon_details()->urls.size());
  ASSERT_NE(delegate_.favicon_details()->urls.end(),
            delegate_.favicon_details()->urls.find(row1.url()));

  EXPECT_FALSE(thumbnail_db_.GetIconMappingForPageURL(row1.url(), FAVICON,
                                                      NULL));
}

}  // namespace history
