// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/history/android/android_history_types.h"

namespace history {

namespace {
// The column name defined in android.provider.Browser.BookmarkColumns
const char* const kAndroidBookmarkColumn[] = {
    "_id",
    "url",
    "title",
    "created",
    "date",
    "visits",
    "favicon",
    "bookmark",
    "raw_url",
};

// The column name defined in android.provider.Browser.SearchColumns
const char* const kAndroidSearchColumn[] = {
    "_id",
    "search",
    "date",
};

class BookmarkIDMapping : public std::map<std::string,
                                          BookmarkRow::BookmarkColumnID> {
 public:
  BookmarkIDMapping() {
    COMPILE_ASSERT(arraysize(kAndroidBookmarkColumn) <= BookmarkRow::COLUMN_END,
                   Array_size_must_not_exceed_enum);
    for (size_t i = 0; i < arraysize(kAndroidBookmarkColumn); ++i) {
      (*this)[kAndroidBookmarkColumn[i]] =
          static_cast<BookmarkRow::BookmarkColumnID>(i);
    }
  }
};

// The mapping from Android column name to BookmarkColumnID; It is initialized
// once it used.
BookmarkIDMapping* g_bookmark_id_mapping = NULL;

class SearchIDMapping : public std::map<std::string,
                                        SearchRow::SearchColumnID> {
 public:
  SearchIDMapping() {
    COMPILE_ASSERT(arraysize(kAndroidSearchColumn) <= SearchRow::COLUMN_END,
                   Array_size_must_not_exceed_enum);
    for (size_t i = 0; i < arraysize(kAndroidSearchColumn); ++i) {
      (*this)[kAndroidSearchColumn[i]] =
              static_cast<SearchRow::SearchColumnID>(i);
    }
  }
};

// The mapping from Android column name to SearchColumnID; It is initialized
// once it used.
SearchIDMapping* g_search_id_mapping = NULL;

}  // namespace

BookmarkRow::BookmarkRow()
    : id_(0),
      created_(base::Time()),
      last_visit_time_(base::Time()),
      visit_count_(0),
      is_bookmark_(false),
      parent_id_(0),
      url_id_(0) {
}

BookmarkRow::~BookmarkRow() {
}

std::string BookmarkRow::GetAndroidName(BookmarkColumnID id) {
  return kAndroidBookmarkColumn[id];
}

BookmarkRow::BookmarkColumnID BookmarkRow::GetBookmarkColumnID(
    const std::string& name) {
  if (!g_bookmark_id_mapping)
    g_bookmark_id_mapping = new BookmarkIDMapping();

  BookmarkIDMapping::const_iterator i = g_bookmark_id_mapping->find(name);
  if (i == g_bookmark_id_mapping->end())
    return BookmarkRow::COLUMN_END;
  else
    return i->second;
}

SearchRow::SearchRow()
    : id_(0),
      template_url_id_(0) {
}

SearchRow::~SearchRow() {
}

std::string SearchRow::GetAndroidName(SearchColumnID id) {
  return kAndroidSearchColumn[id];
}

SearchRow::SearchColumnID SearchRow::GetSearchColumnID(
    const std::string& name) {
  if (!g_search_id_mapping)
    g_search_id_mapping = new SearchIDMapping();

  SearchIDMapping::const_iterator i = g_search_id_mapping->find(name);
  if (i == g_search_id_mapping->end())
    return SearchRow:: COLUMN_END;
  else
    return i->second;
}

AndroidStatement::AndroidStatement(sql::Statement* statement, int favicon_index)
    : statement_(statement),
      favicon_index_(favicon_index) {
}

AndroidStatement::~AndroidStatement() {
}

}  // namespace history.
