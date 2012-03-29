// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/declarative/url_matcher.h"

#include "base/string_util.h"
#include "googleurl/src/gurl.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace extensions {

//
// URLMatcherCondition
//

TEST(URLMatcherConditionTest, Constructors) {
  SubstringPattern pattern("example.com", 1);
  URLMatcherCondition m1(URLMatcherCondition::HOST_SUFFIX, &pattern);
  EXPECT_EQ(URLMatcherCondition::HOST_SUFFIX, m1.criterion());
  EXPECT_EQ(&pattern, m1.substring_pattern());

  URLMatcherCondition m2;
  m2 = m1;
  EXPECT_EQ(URLMatcherCondition::HOST_SUFFIX, m2.criterion());
  EXPECT_EQ(&pattern, m2.substring_pattern());

  URLMatcherCondition m3(m1);
  EXPECT_EQ(URLMatcherCondition::HOST_SUFFIX, m3.criterion());
  EXPECT_EQ(&pattern, m3.substring_pattern());
}

TEST(URLMatcherConditionTest, IsFullURLCondition) {
  SubstringPattern pattern("example.com", 1);
  EXPECT_FALSE(URLMatcherCondition(URLMatcherCondition::HOST_SUFFIX,
      &pattern).IsFullURLCondition());

  EXPECT_TRUE(URLMatcherCondition(URLMatcherCondition::HOST_CONTAINS,
      &pattern).IsFullURLCondition());
  EXPECT_TRUE(URLMatcherCondition(URLMatcherCondition::PATH_CONTAINS,
      &pattern).IsFullURLCondition());
  EXPECT_TRUE(URLMatcherCondition(URLMatcherCondition::QUERY_CONTAINS,
      &pattern).IsFullURLCondition());

  EXPECT_TRUE(URLMatcherCondition(URLMatcherCondition::URL_PREFIX,
      &pattern).IsFullURLCondition());
  EXPECT_TRUE(URLMatcherCondition(URLMatcherCondition::URL_SUFFIX,
      &pattern).IsFullURLCondition());
  EXPECT_TRUE(URLMatcherCondition(URLMatcherCondition::URL_CONTAINS,
      &pattern).IsFullURLCondition());
  EXPECT_TRUE(URLMatcherCondition(URLMatcherCondition::URL_EQUALS,
      &pattern).IsFullURLCondition());
}

TEST(URLMatcherConditionTest, IsMatch) {
  GURL url1("http://www.example.com/www.foobar.com/index.html");
  GURL url2("http://www.foobar.com/example.com/index.html");

  SubstringPattern pattern("example.com", 1);
  URLMatcherCondition m1(URLMatcherCondition::HOST_SUFFIX, &pattern);

  std::set<SubstringPattern::ID> matching_substring_patterns;

  // matches = {0} --> matcher did not indicate that m1 was a match.
  matching_substring_patterns.insert(0);
  EXPECT_FALSE(m1.IsMatch(matching_substring_patterns, url1));

  // matches = {0, 1} --> matcher did indicate that m1 was a match.
  matching_substring_patterns.insert(1);
  EXPECT_TRUE(m1.IsMatch(matching_substring_patterns, url1));

  // For m2 we use a HOST_CONTAINS test, which requires a post-validation
  // whether the match reported by the SubstringSetMatcher occurs really
  // in the correct url component.
  URLMatcherCondition m2(URLMatcherCondition::HOST_CONTAINS, &pattern);
  EXPECT_TRUE(m2.IsMatch(matching_substring_patterns, url1));
  EXPECT_FALSE(m2.IsMatch(matching_substring_patterns, url2));
}

TEST(URLMatcherConditionTest, Comparison) {
  SubstringPattern p1("foobar.com", 1);
  SubstringPattern p2("foobar.com", 2);
  // The first component of each test is expected to be < than the second.
  URLMatcherCondition test_smaller[][2] = {
      {URLMatcherCondition(URLMatcherCondition::HOST_PREFIX, &p1),
       URLMatcherCondition(URLMatcherCondition::HOST_SUFFIX, &p1)},
      {URLMatcherCondition(URLMatcherCondition::HOST_PREFIX, &p1),
       URLMatcherCondition(URLMatcherCondition::HOST_PREFIX, &p2)},
      {URLMatcherCondition(URLMatcherCondition::HOST_PREFIX, NULL),
       URLMatcherCondition(URLMatcherCondition::HOST_PREFIX, &p2)},
      {URLMatcherCondition(URLMatcherCondition::HOST_PREFIX, &p1),
       URLMatcherCondition(URLMatcherCondition::HOST_SUFFIX, NULL)},
  };
  for (size_t i = 0; i < arraysize(test_smaller); ++i) {
    EXPECT_TRUE(test_smaller[i][0] < test_smaller[i][1])
        << "Test " << i << " of test_smaller failed";
    EXPECT_FALSE(test_smaller[i][1] < test_smaller[i][0])
        << "Test " << i << " of test_smaller failed";
  }
  URLMatcherCondition test_equal[][2] = {
      {URLMatcherCondition(URLMatcherCondition::HOST_PREFIX, &p1),
       URLMatcherCondition(URLMatcherCondition::HOST_PREFIX, &p1)},
      {URLMatcherCondition(URLMatcherCondition::HOST_PREFIX, NULL),
       URLMatcherCondition(URLMatcherCondition::HOST_PREFIX, NULL)},
  };
  for (size_t i = 0; i < arraysize(test_equal); ++i) {
    EXPECT_FALSE(test_equal[i][0] < test_equal[i][1])
        << "Test " << i << " of test_equal failed";
    EXPECT_FALSE(test_equal[i][1] < test_equal[i][0])
        << "Test " << i << " of test_equal failed";
  }
}

//
// URLMatcherConditionFactory
//

namespace {

bool Matches(const URLMatcherCondition& condition, std::string text) {
  return text.find(condition.substring_pattern()->pattern()) !=
      std::string::npos;
}

}  // namespace

TEST(URLMatcherConditionFactoryTest, GURLCharacterSet) {
  // GURL guarantees that neither domain, nor path, nor query may contain
  // non ASCII-7 characters. We test this here, because a change to this
  // guarantee breaks this implementation horribly.
  GURL url("http://www.föö.com/föö?föö#föö");
  EXPECT_TRUE(IsStringASCII(url.host()));
  EXPECT_TRUE(IsStringASCII(url.path()));
  EXPECT_TRUE(IsStringASCII(url.query()));
  EXPECT_FALSE(IsStringASCII(url.ref()));
}

TEST(URLMatcherConditionFactoryTest, TestSingletonProperty) {
  URLMatcherConditionFactory factory;
  URLMatcherCondition c1 = factory.CreateHostEqualsCondition("www.google.com");
  URLMatcherCondition c2 = factory.CreateHostEqualsCondition("www.google.com");
  EXPECT_EQ(c1.criterion(), c2.criterion());
  EXPECT_EQ(c1.substring_pattern(), c2.substring_pattern());
  URLMatcherCondition c3 = factory.CreateHostEqualsCondition("www.google.de");
  EXPECT_EQ(c2.criterion(), c3.criterion());
  EXPECT_NE(c2.substring_pattern(), c3.substring_pattern());
  EXPECT_NE(c2.substring_pattern()->id(), c3.substring_pattern()->id());
  EXPECT_NE(c2.substring_pattern()->pattern(),
            c3.substring_pattern()->pattern());

  // Check that all SubstringPattern singletons are freed if we call
  // ForgetUnusedPatterns.
  SubstringPattern::ID old_id_1 = c1.substring_pattern()->id();
  factory.ForgetUnusedPatterns(std::set<SubstringPattern::ID>());
  EXPECT_TRUE(factory.IsEmpty());
  URLMatcherCondition c4 = factory.CreateHostEqualsCondition("www.google.com");
  EXPECT_NE(old_id_1, c4.substring_pattern()->id());
}

TEST(URLMatcherConditionFactoryTest, TestComponentSearches) {
  GURL gurl("https://www.google.com/webhp?sourceid=chrome-instant&ie=UTF-8"
      "&ion=1#hl=en&output=search&sclient=psy-ab&q=chrome%20is%20awesome");
  URLMatcherConditionFactory factory;
  std::string url = factory.CanonicalizeURLForComponentSearches(gurl);

  // Test host component.
  EXPECT_TRUE(Matches(factory.CreateHostPrefixCondition(""), url));
  EXPECT_TRUE(Matches(factory.CreateHostPrefixCondition("www.goog"), url));
  EXPECT_TRUE(
      Matches(factory.CreateHostPrefixCondition("www.google.com"), url));
  EXPECT_TRUE(
      Matches(factory.CreateHostPrefixCondition(".www.google.com"), url));
  EXPECT_FALSE(Matches(factory.CreateHostPrefixCondition("google.com"), url));
  EXPECT_FALSE(
      Matches(factory.CreateHostPrefixCondition("www.google.com/"), url));
  EXPECT_FALSE(Matches(factory.CreateHostPrefixCondition("webhp"), url));

  EXPECT_TRUE(Matches(factory.CreateHostSuffixCondition(""), url));
  EXPECT_TRUE(Matches(factory.CreateHostSuffixCondition("com"), url));
  EXPECT_TRUE(Matches(factory.CreateHostSuffixCondition(".com"), url));
  EXPECT_TRUE(
      Matches(factory.CreateHostSuffixCondition("www.google.com"), url));
  EXPECT_TRUE(
      Matches(factory.CreateHostSuffixCondition(".www.google.com"), url));
  EXPECT_FALSE(Matches(factory.CreateHostSuffixCondition("www"), url));
  EXPECT_FALSE(
      Matches(factory.CreateHostSuffixCondition("www.google.com/"), url));
  EXPECT_FALSE(Matches(factory.CreateHostSuffixCondition("webhp"), url));

  EXPECT_FALSE(Matches(factory.CreateHostEqualsCondition(""), url));
  EXPECT_FALSE(Matches(factory.CreateHostEqualsCondition("www"), url));
  EXPECT_TRUE(
      Matches(factory.CreateHostEqualsCondition("www.google.com"), url));
  EXPECT_FALSE(
      Matches(factory.CreateHostEqualsCondition("www.google.com/"), url));


  // Test path component.
  EXPECT_TRUE(Matches(factory.CreatePathPrefixCondition(""), url));
  EXPECT_TRUE(Matches(factory.CreatePathPrefixCondition("/web"), url));
  EXPECT_TRUE(Matches(factory.CreatePathPrefixCondition("/webhp"), url));
  EXPECT_FALSE(Matches(factory.CreatePathPrefixCondition("webhp"), url));
  EXPECT_FALSE(Matches(factory.CreatePathPrefixCondition("/webhp?"), url));

  EXPECT_TRUE(Matches(factory.CreatePathSuffixCondition(""), url));
  EXPECT_TRUE(Matches(factory.CreatePathSuffixCondition("webhp"), url));
  EXPECT_TRUE(Matches(factory.CreatePathSuffixCondition("/webhp"), url));
  EXPECT_FALSE(Matches(factory.CreatePathSuffixCondition("/web"), url));
  EXPECT_FALSE(Matches(factory.CreatePathSuffixCondition("/webhp?"), url));

  EXPECT_TRUE(Matches(factory.CreatePathEqualsCondition("/webhp"), url));
  EXPECT_FALSE(Matches(factory.CreatePathEqualsCondition("webhp"), url));
  EXPECT_FALSE(Matches(factory.CreatePathEqualsCondition("/webhp?"), url));
  EXPECT_FALSE(
      Matches(factory.CreatePathEqualsCondition("www.google.com"), url));


  // Test query component.
  EXPECT_TRUE(Matches(factory.CreateQueryPrefixCondition(""), url));
  EXPECT_TRUE(Matches(factory.CreateQueryPrefixCondition("?sourceid"), url));
  EXPECT_FALSE(Matches(factory.CreatePathPrefixCondition("sourceid"), url));

  EXPECT_TRUE(Matches(factory.CreateQuerySuffixCondition(""), url));
  EXPECT_TRUE(Matches(factory.CreateQuerySuffixCondition("ion=1"), url));
  EXPECT_FALSE(Matches(factory.CreatePathPrefixCondition("?sourceid"), url));
  EXPECT_FALSE(Matches(factory.CreateQuerySuffixCondition("www"), url));

  EXPECT_TRUE(Matches(factory.CreateQueryEqualsCondition(
      "?sourceid=chrome-instant&ie=UTF-8&ion=1"), url));
  EXPECT_FALSE(Matches(factory.CreateQueryEqualsCondition(
        "sourceid=chrome-instant&ie=UTF-8&ion="), url));
  EXPECT_FALSE(
      Matches(factory.CreateQueryEqualsCondition("www.google.com"), url));


  // Test adjacent components
  EXPECT_TRUE(Matches(factory.CreateHostSuffixPathPrefixCondition(
      "google.com", "/webhp"), url));
  EXPECT_TRUE(Matches(factory.CreateHostSuffixPathPrefixCondition(
        "", "/webhp"), url));
  EXPECT_TRUE(Matches(factory.CreateHostSuffixPathPrefixCondition(
        "google.com", ""), url));
  EXPECT_FALSE(Matches(factory.CreateHostSuffixPathPrefixCondition(
        "www", ""), url));
}

TEST(URLMatcherConditionFactoryTest, TestFullSearches) {
  GURL gurl("https://www.google.com/webhp?sourceid=chrome-instant&ie=UTF-8"
      "&ion=1#hl=en&output=search&sclient=psy-ab&q=chrome%20is%20awesome");
  URLMatcherConditionFactory factory;
  std::string url = factory.CanonicalizeURLForFullSearches(gurl);

  EXPECT_TRUE(Matches(factory.CreateURLPrefixCondition(""), url));
  EXPECT_TRUE(Matches(factory.CreateURLPrefixCondition("www.goog"), url));
  EXPECT_TRUE(Matches(factory.CreateURLPrefixCondition("www.google.com"), url));
  EXPECT_TRUE(
      Matches(factory.CreateURLPrefixCondition(".www.google.com"), url));
  EXPECT_TRUE(
      Matches(factory.CreateURLPrefixCondition("www.google.com/"), url));
  EXPECT_FALSE(Matches(factory.CreateURLPrefixCondition("webhp"), url));

  EXPECT_TRUE(Matches(factory.CreateURLSuffixCondition(""), url));
  EXPECT_TRUE(Matches(factory.CreateURLSuffixCondition("ion=1"), url));
  EXPECT_FALSE(Matches(factory.CreateURLSuffixCondition("www"), url));

  EXPECT_TRUE(Matches(factory.CreateURLContainsCondition(""), url));
  EXPECT_TRUE(Matches(factory.CreateURLContainsCondition("www.goog"), url));
  EXPECT_TRUE(Matches(factory.CreateURLContainsCondition(".www.goog"), url));
  EXPECT_TRUE(Matches(factory.CreateURLContainsCondition("webhp"), url));
  EXPECT_TRUE(Matches(factory.CreateURLContainsCondition("?"), url));
  EXPECT_TRUE(Matches(factory.CreateURLContainsCondition("sourceid"), url));
  EXPECT_TRUE(Matches(factory.CreateURLContainsCondition("ion=1"), url));
  EXPECT_FALSE(Matches(factory.CreateURLContainsCondition("foobar"), url));
  EXPECT_FALSE(Matches(factory.CreateURLContainsCondition("search"), url));

  EXPECT_TRUE(Matches(factory.CreateURLEqualsCondition(
      "www.google.com/webhp?sourceid=chrome-instant&ie=UTF-8&ion=1"), url));
  EXPECT_FALSE(
      Matches(factory.CreateURLEqualsCondition("www.google.com"), url));
}


//
// URLMatcherConditionSet
//

TEST(URLMatcherConditionSetTest, Constructors) {
  URLMatcherConditionFactory factory;
  URLMatcherCondition m1 = factory.CreateHostSuffixCondition("example.com");
  URLMatcherCondition m2 = factory.CreatePathContainsCondition("foo");

  std::set<URLMatcherCondition> conditions;
  conditions.insert(m1);
  conditions.insert(m2);

  URLMatcherConditionSet condition_set(1, conditions);
  EXPECT_EQ(1, condition_set.id());
  EXPECT_EQ(2u, condition_set.conditions().size());

  std::set<URLMatcherCondition> other_conditions;
  other_conditions.insert(m1);
  URLMatcherConditionSet condition_set2(2, other_conditions);
  condition_set2 = condition_set;
  EXPECT_EQ(1, condition_set2.id());
  EXPECT_EQ(2u, condition_set2.conditions().size());

  URLMatcherConditionSet condition_set3(condition_set);
  EXPECT_EQ(1, condition_set2.id());
  EXPECT_EQ(2u, condition_set2.conditions().size());
}

TEST(URLMatcherConditionSetTest, Matching) {
  GURL url1("http://www.example.com/foo?bar=1");
  GURL url2("http://foo.example.com/index.html");

  URLMatcherConditionFactory factory;
  URLMatcherCondition m1 = factory.CreateHostSuffixCondition("example.com");
  URLMatcherCondition m2 = factory.CreatePathContainsCondition("foo");

  std::set<URLMatcherCondition> conditions;
  conditions.insert(m1);
  conditions.insert(m2);

  URLMatcherConditionSet condition_set(1, conditions);
  EXPECT_EQ(1, condition_set.id());
  EXPECT_EQ(2u, condition_set.conditions().size());

  std::set<SubstringPattern::ID> matching_substring_patterns;
  matching_substring_patterns.insert(m1.substring_pattern()->id());
  EXPECT_FALSE(condition_set.IsMatch(matching_substring_patterns, url1));

  matching_substring_patterns.insert(m2.substring_pattern()->id());
  EXPECT_TRUE(condition_set.IsMatch(matching_substring_patterns, url1));
  EXPECT_FALSE(condition_set.IsMatch(matching_substring_patterns, url2));
}


//
// URLMatcher
//

TEST(URLMatcherTest, FullTest) {
  GURL url1("http://www.example.com/foo?bar=1");
  GURL url2("http://foo.example.com/index.html");

  URLMatcher matcher;
  URLMatcherConditionFactory* factory = matcher.condition_factory();

  // First insert.
  URLMatcherConditionSet::Conditions conditions1;
  conditions1.insert(factory->CreateHostSuffixCondition("example.com"));
  conditions1.insert(factory->CreatePathContainsCondition("foo"));

  const int kConditionSetId1 = 1;
  std::vector<URLMatcherConditionSet> insert1;
  insert1.push_back(URLMatcherConditionSet(kConditionSetId1, conditions1));
  matcher.AddConditionSets(insert1);
  EXPECT_EQ(1u, matcher.MatchURL(url1).size());
  EXPECT_EQ(0u, matcher.MatchURL(url2).size());

  // Second insert.
  URLMatcherConditionSet::Conditions conditions2;
  conditions2.insert(factory->CreateHostSuffixCondition("example.com"));

  const int kConditionSetId2 = 2;
  std::vector<URLMatcherConditionSet> insert2;
  insert2.push_back(URLMatcherConditionSet(kConditionSetId2, conditions2));
  matcher.AddConditionSets(insert2);
  EXPECT_EQ(2u, matcher.MatchURL(url1).size());
  EXPECT_EQ(1u, matcher.MatchURL(url2).size());

  // This should be the cached singleton.
  int patternId1 = factory->CreateHostSuffixCondition(
      "example.com").substring_pattern()->id();

  // Removal of last insert.
  std::vector<URLMatcherConditionSet::ID> remove2;
  remove2.push_back(kConditionSetId2);
  matcher.RemoveConditionSets(remove2);
  EXPECT_EQ(1u, matcher.MatchURL(url1).size());
  EXPECT_EQ(0u, matcher.MatchURL(url2).size());

  // Removal of first insert.
  std::vector<URLMatcherConditionSet::ID> remove1;
  remove1.push_back(kConditionSetId1);
  matcher.RemoveConditionSets(remove1);
  EXPECT_EQ(0u, matcher.MatchURL(url1).size());
  EXPECT_EQ(0u, matcher.MatchURL(url2).size());

  EXPECT_TRUE(matcher.IsEmpty());

  // The cached singleton in matcher.condition_factory_ should be destroyed to
  // free memory.
  int patternId2 = factory->CreateHostSuffixCondition(
      "example.com").substring_pattern()->id();
  // If patternId1 and patternId2 are different that indicates that
  // matcher.condition_factory_ does not leak memory.
  EXPECT_NE(patternId1, patternId2);
}

}  // namespace extensions
