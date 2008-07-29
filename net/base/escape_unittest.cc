// Copyright 2008, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//    * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//    * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//    * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <string>

#include "net/base/escape.h"

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/string_util.h"
#include "testing/gtest/include/gtest/gtest.h"

struct unescape_case {
  const char* input;
  const char* output;
};

TEST(Escape, EscapeTextForFormSubmission) {
  struct escape_case {
    const wchar_t* input;
    const wchar_t* output;
  } escape_cases[] = {
    {L"foo", L"foo"},
    {L"foo bar", L"foo+bar"},
    {L"foo++", L"foo%2B%2B"}
  };
  for (int i = 0; i < arraysize(escape_cases); ++i) {
    escape_case value = escape_cases[i];
    EXPECT_EQ(value.output, EscapeQueryParamValueUTF8(value.input));
  }

  // Test all the values in we're supposed to be escaping.
  const std::string no_escape(
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789"
    "!'()*-._~");
  for (int i = 0; i < 256; ++i) {
    std::string in;
    in.push_back(i);
    std::string out = EscapeQueryParamValue(in);
    if (0 == i) {
      EXPECT_EQ(out, std::string("%00"));
    } else if (32 == i) {
      // Spaces are plus escaped like web forms.
      EXPECT_EQ(out, std::string("+"));
    } else if (no_escape.find(in) == std::string::npos) {
      // Check %hex escaping
      char buf[4];
      sprintf_s(buf, 4, "%%%02X", i);
      EXPECT_EQ(std::string(buf), out);
    } else {
      // No change for things in the no_escape list.
      EXPECT_EQ(out, in);
    }
  }

  // Check to see if EscapeQueryParamValueUTF8 is the same as
  // EscapeQueryParamValue(..., kCodepageUTF8,)
  std::wstring test_str;
  test_str.reserve(5000);
  for (int i = 1; i < 5000; ++i) {
    test_str.push_back(i);
  }
  std::wstring wide;
  EXPECT_TRUE(EscapeQueryParamValue(test_str, kCodepageUTF8, &wide));
  EXPECT_EQ(wide, EscapeQueryParamValueUTF8(test_str));
}

TEST(Escape, EscapePath) {
  ASSERT_EQ(
    // Most of the character space we care about, un-escaped
    EscapePath(
      "\x02\n\x1d !\"#$%&'()*+,-./0123456789:;"
      "<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "[\\]^_`abcdefghijklmnopqrstuvwxyz"
      "{|}~\x7f\x80\xff"),
    // Escaped
    "%02%0A%1D%20!%22%23$%25&'()*+,-./0123456789%3A;"
    "%3C=%3E%3F@ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "%5B%5C%5D%5E_%60abcdefghijklmnopqrstuvwxyz"
    "%7B%7C%7D~%7F%80%FF");
}

TEST(Escape, UnescapeURLComponent) {
  struct UnescapeCase {
    const char* input;
    UnescapeRule::Type rules;
    const char* output;
  } unescape_cases[] = {
    {"", UnescapeRule::NORMAL, ""},
    {"%2", UnescapeRule::NORMAL, "%2"},
    {"%%%%%%", UnescapeRule::NORMAL, "%%%%%%"},
    {"Don't escape anything", UnescapeRule::NORMAL, "Don't escape anything"},
    {"Invalid %escape %2", UnescapeRule::NORMAL, "Invalid %escape %2"},
    {"Some%20random text %25%3bOK", UnescapeRule::NORMAL, "Some%20random text %25;OK"},
    {"Some%20random text %25%3bOK", UnescapeRule::SPACES, "Some random text %25;OK"},
    {"Some%20random text %25%3bOK", UnescapeRule::PERCENTS, "Some%20random text %;OK"},
    {"Some%20random text %25%3bOK", UnescapeRule::SPACES | UnescapeRule::PERCENTS, "Some random text %;OK"},
    {"%01%02%03%04%05%06%07%08%09", UnescapeRule::NORMAL, "\x01\x02\x03\x04\x05\x06\x07\x08\x09"},
    {"%A0%B1%C2%D3%E4%F5", UnescapeRule::NORMAL, "\xA0\xB1\xC2\xD3\xE4\xF5"},
    {"%Aa%Bb%Cc%Dd%Ee%Ff", UnescapeRule::NORMAL, "\xAa\xBb\xCc\xDd\xEe\xFf"}
  };

  for (int i = 0; i < arraysize(unescape_cases); i++) {
    std::string str(unescape_cases[i].input);
    EXPECT_EQ(std::string(unescape_cases[i].output),
              UnescapeURLComponent(str, unescape_cases[i].rules));
  }

  // test the NULL character escaping (which wouldn't work above since those
  // are just char pointers)
  std::string input("Null");
  input.push_back(0);  // Also have a NULL in the input.
  input.append("%00%39Test");

  std::string expected("Null");
  expected.push_back(0);
  expected.push_back(0);
  expected.append("9Test");

  EXPECT_EQ(expected, UnescapeURLComponent(input, UnescapeRule::NORMAL));
}

TEST(Escape, UnescapeAndDecodeURLComponent) {
  struct UnescapeCase {
    const char* encoding;
    const char* input;

    // The expected output when run through UnescapeURL.
    const char* url_unescaped;

    // The expected output when run through UnescapeQuery.
    const char* query_unescaped;

    // The expected output when run through UnescapeAndDecodeURLComponent.
    const wchar_t* decoded;
  } unescape_cases[] = {
    {"UTF8", "+", "+", " ", L"+"},
    {"UTF8", "%2+", "%2+", "%2 ", L"%2+"},
    {"UTF8", "+%%%+%%%", "+%%%+%%%", " %%% %%%", L"+%%%+%%%"},
    {"UTF8", "Don't escape anything",
             "Don't escape anything",
             "Don't escape anything",
             L"Don't escape anything"},
    {"UTF8", "+Invalid %escape %2+",
             "+Invalid %escape %2+",
             " Invalid %escape %2 ",
             L"+Invalid %escape %2+"},
    {"UTF8", "Some random text %25%3bOK",
             "Some random text %25;OK",
             "Some random text %25;OK",
             L"Some random text %25;OK"},
    {"UTF8", "%01%02%03%04%05%06%07%08%09",
             "\x01\x02\x03\x04\x05\x06\x07\x08\x09",
             "\x01\x02\x03\x04\x05\x06\x07\x08\x09",
             L"\x01\x02\x03\x04\x05\x06\x07\x08\x09"},
    {"UTF8", "%E4%BD%A0+%E5%A5%BD",
             "\xE4\xBD\xA0+\xE5\xA5\xBD",
             "\xE4\xBD\xA0 \xE5\xA5\xBD",
             L"\x4f60+\x597d"},
    {"BIG5", "%A7A%A6n",
             "\xA7\x41\xA6n",
             "\xA7\x41\xA6n",
             L"\x4f60\x597d"},
    {"UTF8", "%ED%ED",  // Invalid UTF-8.
             "\xED\xED",
             "\xED\xED",
             L"%ED%ED"},  // Invalid UTF-8 -> kept unescaped.
  };

  for (int i = 0; i < arraysize(unescape_cases); i++) {
    std::string unescaped = UnescapeURLComponent(unescape_cases[i].input,
                                                 UnescapeRule::NORMAL);
    EXPECT_EQ(std::string(unescape_cases[i].url_unescaped), unescaped);

    unescaped = UnescapeURLComponent(unescape_cases[i].input,
                                     UnescapeRule::REPLACE_PLUS_WITH_SPACE);
    EXPECT_EQ(std::string(unescape_cases[i].query_unescaped), unescaped);

    // TODO: Need to test unescape_spaces and unescape_percent.
    std::wstring decoded = UnescapeAndDecodeURLComponent(
        unescape_cases[i].input, unescape_cases[i].encoding,
        UnescapeRule::NORMAL);
    EXPECT_EQ(std::wstring(unescape_cases[i].decoded), decoded);
  }
}

TEST(Escape, EscapeForHTML) {
  static const struct {
    const char* input;
    const char* expected_output;
  } tests[] = {
    { "hello", "hello" },
    { "<hello>", "&lt;hello&gt;" },
    { "don\'t mess with me", "don&#39;t mess with me" },
  };
  for (size_t i = 0; i < arraysize(tests); ++i) {
    std::string result = EscapeForHTML(std::string(tests[i].input));
    EXPECT_EQ(std::string(tests[i].expected_output), result);
  }
}

