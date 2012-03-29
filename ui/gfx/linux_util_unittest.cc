// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/linux_util.h"

#include "base/basictypes.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace gfx {

TEST(LinuxUtilTest, ConvertAcceleratorsFromWindowsStyle) {
  static const struct {
    const char* input;
    const char* output;
  } cases[] = {
    { "", "" },
    { "nothing", "nothing" },
    { "foo &bar", "foo _bar" },
    { "foo &&bar", "foo &bar" },
    { "foo &&&bar", "foo &_bar" },
    { "&foo &&bar", "_foo &bar" },
    { "&foo &bar", "_foo _bar" },
  };
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(cases); ++i) {
    std::string result = ConvertAcceleratorsFromWindowsStyle(cases[i].input);
    EXPECT_EQ(cases[i].output, result);
  }
}

TEST(LinuxUtilTest, RemoveWindowsStyleAccelerators) {
  static const struct {
    const char* input;
    const char* output;
  } cases[] = {
    { "", "" },
    { "nothing", "nothing" },
    { "foo &bar", "foo bar" },
    { "foo &&bar", "foo &bar" },
    { "foo &&&bar", "foo &bar" },
    { "&foo &&bar", "foo &bar" },
    { "&foo &bar", "foo bar" },
  };
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(cases); ++i) {
    std::string result = RemoveWindowsStyleAccelerators(cases[i].input);
    EXPECT_EQ(cases[i].output, result);
  }
}

TEST(LinuxUtilTest, EscapeWindowsStyleAccelerators) {
  static const struct {
    const char* input;
    const char* output;
  } cases[] = {
    { "nothing", "nothing" },
    { "foo &bar", "foo &&bar" },
    { "foo &&bar", "foo &&&&bar" },
    { "foo &&&bar", "foo &&&&&&bar" },
    { "&foo bar", "&&foo bar" },
    { "&&foo bar", "&&&&foo bar" },
    { "&&&foo bar", "&&&&&&foo bar" },
    { "&foo &bar", "&&foo &&bar" },
    { "&&foo &&bar", "&&&&foo &&&&bar" },
    { "f&o&o ba&r", "f&&o&&o ba&&r" },
    { "foo_&_bar", "foo_&&_bar" },
    { "&_foo_bar_&", "&&_foo_bar_&&" },
  };

  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(cases); ++i) {
    std::string result = EscapeWindowsStyleAccelerators(cases[i].input);
    EXPECT_EQ(cases[i].output, result);
  }
}

}  // namespace gfx
