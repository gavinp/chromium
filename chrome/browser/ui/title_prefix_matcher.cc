// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/title_prefix_matcher.h"

#include "base/hash_tables.h"
#include "base/i18n/break_iterator.h"
#include "base/logging.h"
#include "base/utf_string_conversions.h"

namespace {
// We use this value to identify that we have already seen the title associated
// to this value in the duplicate_titles hash_set, ans marked it as a duplicate.
const size_t kPreviouslySeenIndex = 0xFFFFFFFF;
}

// static
const int TitlePrefixMatcher::kCommonCharsToShow = 4;
const size_t TitlePrefixMatcher::kMinElidingLength =
    TitlePrefixMatcher::kCommonCharsToShow + 3;

TitlePrefixMatcher::TitleInfo::TitleInfo(
    const string16* title, const GURL& url, int caller_value)
    : title(title),
      url(url),
      prefix_length(0),
      caller_value(caller_value) {
  DCHECK(title != NULL);
}

TitlePrefixMatcher::TitleInfo::~TitleInfo() {
}

// static
void TitlePrefixMatcher::CalculatePrefixLengths(
    std::vector<TitleInfo>* title_infos) {
  DCHECK(title_infos != NULL);
  // This set will contain the indexes of the TitleInfo objects in the vector
  // that have a duplicate.
  base::hash_set<size_t> duplicate_titles;
  // This map is used to identify duplicates by remembering the vector indexes
  // we have seen with a given title string. The vector index is set to
  // kPreviouslySeenIndex once we identified duplicates and placed their
  // indices in duplicate_titles.
  base::hash_map<string16, size_t> existing_title;
  // We identify if there are identical titles upfront,
  // because we don't want to remove prefixes for those at all.
  // We do it as a separate pass so that we don't need to remove
  // previously parsed titles when we find a duplicate title later on.
  for (size_t i = 0; i < title_infos->size(); ++i) {
    // We use pairs to test existence and insert in one shot.
    std::pair<base::hash_map<string16, size_t>::iterator, bool> insert_result =
        existing_title.insert(std::make_pair(*(*title_infos)[i].title, i));
    if (!insert_result.second) {
      // insert_result.second is false when we insert a duplicate in the set.
      // insert_result.first is a map iterator and thus
      // insert_result.first->first is the string title key of the map.
      DCHECK_EQ(*(*title_infos)[i].title, insert_result.first->first);
      duplicate_titles.insert(i);
      // insert_result.first->second is the value of the title index and if it's
      // not kPreviouslySeenIndex yet, we must remember it as a duplicate too.
      if (insert_result.first->second != kPreviouslySeenIndex) {
        duplicate_titles.insert(insert_result.first->second);
        insert_result.first->second = kPreviouslySeenIndex;
      }
    }
  }

  // This next loop accumulates all the potential prefixes,
  // and remember on which titles we saw them.
  base::hash_map<string16, std::vector<size_t> > prefixes;
  for (size_t i = 0; i < title_infos->size(); ++i) {
    // Duplicate titles are not to be included in this process.
    if (duplicate_titles.find(i) != duplicate_titles.end())
      continue;
    const TitleInfo& title_info = (*title_infos)[i];
    const string16* title = title_info.title;
    // We prefix the hostname at the beginning, so that we only group
    // titles that are from the same hostname.
    string16 hostname = ASCIIToUTF16(title_info.url.host());
    // We only create prefixes at word boundaries.
    base::BreakIterator iter(title, base::BreakIterator::BREAK_WORD);
    // We ignore this title if we can't break it into words, or if it only
    // contains a single word.
    if (!iter.Init() || !iter.Advance())
      continue;
    // We continue advancing even though we already advanced to the first
    // word above, so that we can use iter.prev() to identify the end of the
    // previous word and more easily ignore the last word while iterating.
    while (iter.Advance()) {
      if (iter.IsWord())
        prefixes[hostname + title->substr(0, iter.prev())].push_back(i);
    }
  }

  // Now we parse the map to find common prefixes
  // and keep the largest per title.
  for (base::hash_map<string16, std::vector<size_t> >::iterator iter =
       prefixes.begin(); iter != prefixes.end(); ++iter) {
    // iter->first is the prefix string, iter->second is a vector of indices.
    if (iter->second.size() > 1) {
      // We need to subtract the hostname size since we added it to the prefix.
      const TitleInfo& first_title_info = (*title_infos)[iter->second[0]];
      DCHECK_GE(iter->first.size(), first_title_info.url.host().size());
      size_t prefix_length = iter->first.size() -
          first_title_info.url.host().size();
      for (size_t i = 0; i < iter->second.size(); ++i){
        TitleInfo& title_info = (*title_infos)[iter->second[i]];
        DCHECK_EQ(first_title_info.url.host(), title_info.url.host());
        if (title_info.prefix_length < prefix_length)
          title_info.prefix_length = prefix_length;
      }
    }
  }
}
