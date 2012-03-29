// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/history/android/android_cache_database.h"

#include "base/file_util.h"
#include "chrome/browser/history/android/android_time.h"
#include "sql/statement.h"

using base::Time;
using base::TimeDelta;

namespace history {

AndroidCacheDatabase::AndroidCacheDatabase() {
}

AndroidCacheDatabase::~AndroidCacheDatabase() {
}

sql::InitStatus AndroidCacheDatabase::InitAndroidCacheDatabase(
    const FilePath& db_name) {
  if (!CreateDatabase(db_name))
    return sql::INIT_FAILURE;

  if (!Attach())
    return sql::INIT_FAILURE;

  if (!CreateBookmarkCacheTable())
    return sql::INIT_FAILURE;

  return sql::INIT_OK;
}

bool AndroidCacheDatabase::AddBookmarkCacheRow(const Time& created_time,
                                               const Time& last_visit_time,
                                               URLID url_id) {
  sql::Statement statement(GetDB().GetCachedStatement(SQL_FROM_HERE,
      "INSERT INTO android_cache_db.bookmark_cache (created_time, "
      "last_visit_time, url_id) VALUES (?, ?, ?)"));

  statement.BindInt64(0, ToMilliseconds(created_time));
  statement.BindInt64(1, ToMilliseconds(last_visit_time));
  statement.BindInt64(2, url_id);

  if (!statement.Run()) {
    LOG(ERROR) << GetDB().GetErrorMessage();
    return false;
  }

  return true;
}

bool AndroidCacheDatabase::ClearAllBookmarkCache() {
  sql::Statement statement(GetDB().GetCachedStatement(SQL_FROM_HERE,
      "DELETE FROM android_cache_db.bookmark_cache"));
  if (!statement.Run()) {
    LOG(ERROR) << GetDB().GetErrorMessage();
    return false;
  }
  return true;
}

bool AndroidCacheDatabase::MarkURLsAsBookmarked(
    const std::vector<URLID>& url_ids) {
  bool has_id = false;
  std::ostringstream oss;
  for (std::vector<URLID>::const_iterator i = url_ids.begin();
      i != url_ids.end(); ++i) {
    if (has_id)
      oss << ", ";
    else
      has_id = true;
    oss << *i;
  }

  if (!has_id)
    return true;

  std::string sql("UPDATE android_cache_db.bookmark_cache "
                  "SET bookmark = 1 WHERE url_id in (");
  sql.append(oss.str());
  sql.append(")");
  if (!GetDB().Execute(sql.c_str())) {
    LOG(ERROR) << GetDB().GetErrorMessage();
    return false;
  }
  return true;
}

bool AndroidCacheDatabase::SetFaviconID(URLID url_id, FaviconID favicon_id) {
  sql::Statement update_statement(GetDB().GetCachedStatement(SQL_FROM_HERE,
      "UPDATE android_cache_db.bookmark_cache "
      "SET favicon_id = ? WHERE url_id = ? "));

  update_statement.BindInt64(0, favicon_id);
  update_statement.BindInt64(1, url_id);
  if (!update_statement.Run()) {
    LOG(ERROR) << GetDB().GetErrorMessage();
    return false;
  }
  return true;
}

bool AndroidCacheDatabase::CreateDatabase(const FilePath& db_name) {
  db_name_ = db_name;
  if (file_util::PathExists(db_name_))
    file_util::Delete(db_name_, false);

  // Using a new connection, otherwise we can not create the database.
  sql::Connection connection;

  // The db doesn't store too much data, so we don't need that big a page
  // size or cache.
  connection.set_page_size(2048);
  connection.set_cache_size(32);

  // Run the database in exclusive mode. Nobody else should be accessing the
  // database while we're running, and this will give somewhat improved perf.
  connection.set_exclusive_locking();

  if (!connection.Open(db_name_)) {
    LOG(ERROR) << connection.GetErrorMessage();
    return false;
  }
  connection.Close();
  return true;
}

bool AndroidCacheDatabase::CreateBookmarkCacheTable() {
  const char* name = "android_cache_db.bookmark_cache";
  DCHECK(!GetDB().DoesTableExist(name));

  std::string sql;
  sql.append("CREATE TABLE ");
  sql.append(name);
  sql.append("("
             "id INTEGER PRIMARY KEY,"
             "created_time INTEGER NOT NULL,"     // Time in millisecond.
             "last_visit_time INTEGER NOT NULL,"  // Time in millisecond.
             "url_id INTEGER NOT NULL,"           // url id in urls table.
             "favicon_id INTEGER DEFAULT NULL,"   // favicon id.
             "bookmark INTEGER DEFAULT 0"         // whether is bookmark.
             ")");
  if (!GetDB().Execute(sql.c_str())) {
    LOG(ERROR) << GetDB().GetErrorMessage();
    return false;
  }

  sql.assign("CREATE INDEX ");
  sql.append("android_cache_db.bookmark_cache_url_id_idx ON "
             "bookmark_cache(url_id)");
  if (!GetDB().Execute(sql.c_str())) {
    LOG(ERROR) << GetDB().GetErrorMessage();
    return false;
  }
  return true;
}

bool AndroidCacheDatabase::Attach() {
  // Commit all open transactions to make attach succeed.
  if (GetDB().transaction_nesting())
    GetDB().CommitTransaction();

  bool result = DoAttach();

  // No matter the attach succeed or not, we need to begin a new transaction.
  GetDB().BeginTransaction();
  return result;
}

bool AndroidCacheDatabase::DoAttach() {
  std::string sql("ATTACH ? AS android_cache_db");
  sql::Statement attach(GetDB().GetUniqueStatement(sql.c_str()));
  if (!attach.is_valid())
    // Keep the transaction open, even though we failed.
    return false;

  attach.BindString(0, db_name_.value());
  if (!attach.Run())
    return false;

  return true;
}

}  // namespace history
