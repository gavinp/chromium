// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include "base/logging.h"
#include "base/path_service.h"
#include "base/string_util.h"
#include "base/string16.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/browser/accessibility/browser_accessibility.h"
#include "content/browser/accessibility/browser_accessibility_manager.h"
#include "content/browser/accessibility/dump_accessibility_tree_helper.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/port/browser/render_widget_host_view_port.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_paths.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::OpenURLParams;
using content::RenderViewHostImpl;
using content::RenderWidgetHostImpl;
using content::RenderWidgetHost;
using content::RenderWidgetHostViewPort;
using content::Referrer;

namespace {
// Required to enter html content into a url.
  static const std::string kUrlPreamble = "data:text/html,\n<!doctype html>";
  static const char kCommentToken = '#';
} // namespace

// This test takes a snapshot of the platform BrowserAccessibility tree and
// tests it against an expected baseline.
//
// The flow of the test is as outlined below.
// 1. Load an html file from chrome/test/data/accessibility.
// 2. Read the expectation.
// 3. Browse to the page and serialize the platform specific tree into a human
//    readable string.
// 4. Perform a comparison between actual and expected and fail if they do not
//    exactly match.
class DumpAccessibilityTreeTest : public InProcessBrowserTest {
 public:
  // Utility helper that does a comment aware equality check.
  bool EqualsWithComments(std::string& expected, std::string& actual) {
    std::vector<std::string> actual_lines, expected_lines;
    int actual_lines_count = Tokenize(actual, "\n", &actual_lines);
    int expected_lines_count = Tokenize(expected, "\n", &expected_lines);
    int i = actual_lines_count - 1, j = expected_lines_count - 1;
    while (i >= 0 && j >= 0) {
      if (expected_lines[j].size() > 0 &&
          expected_lines[j][0] == kCommentToken) {
        --j;
        continue;
      }

      if (actual_lines[i] != expected_lines[j])
        return false;
      --i;
      --j;
    }

    // Actual file has been fully checked.
    return i < 0;
  }

  DumpAccessibilityTreeHelper helper_;
};

IN_PROC_BROWSER_TEST_F(DumpAccessibilityTreeTest,
                       PlatformTreeDifferenceTest) {
  RenderWidgetHostViewPort* host_view = static_cast<RenderWidgetHostViewPort*>(
          browser()->GetSelectedWebContents()->GetRenderWidgetHostView());
  RenderWidgetHost* host = host_view->GetRenderWidgetHost();
  RenderViewHostImpl* view_host =
      static_cast<RenderViewHostImpl*>(RenderWidgetHostImpl::From(host));
  view_host->set_save_accessibility_tree_for_testing(true);
  view_host->EnableRendererAccessibility();

  // Setup test paths.
  FilePath dir_test_data;
  EXPECT_TRUE(PathService::Get(content::DIR_TEST_DATA, &dir_test_data));
  FilePath test_path(dir_test_data.Append(FILE_PATH_LITERAL("accessibility")));
  EXPECT_TRUE(file_util::PathExists(test_path))
      << test_path.LossyDisplayName();

  // Output the test path to help anyone who encounters a failure and needs
  // to know where to look.
  printf("Path to test files: %s\n", test_path.MaybeAsASCII().c_str());

  // Grab all HTML files.
  file_util::FileEnumerator file_enumerator(test_path,
                                            false,
                                            file_util::FileEnumerator::FILES,
                                            FILE_PATH_LITERAL("*.html"));

  // TODO(dtseng): Make each of these a gtest with script.
  FilePath html_file(file_enumerator.Next());
  ASSERT_FALSE(html_file.empty());
  do {
    printf("Testing %s\n", html_file.BaseName().MaybeAsASCII().c_str());

    std::string html_contents;
    file_util::ReadFileToString(html_file, &html_contents);

    // Read the expected file.
    std::string expected_contents_raw;
    FilePath expected_file =
        FilePath(html_file.RemoveExtension().value() +
            helper_.GetExpectedFileSuffix());
    file_util::ReadFileToString(
        expected_file,
        &expected_contents_raw);

    // Tolerate Windows-style line endings (\r\n) in the expected file:
    // normalize by deleting all \r from the file (if any) to leave only \n.
    std::string expected_contents;
    RemoveChars(expected_contents_raw, "\r", &expected_contents);

    // Load the page.
    ui_test_utils::WindowedNotificationObserver tree_updated_observer(
        content::NOTIFICATION_RENDER_VIEW_HOST_ACCESSIBILITY_TREE_UPDATED,
        content::NotificationService::AllSources());
    string16 html_contents16;
    html_contents16 = UTF8ToUTF16(html_contents);
    GURL url(UTF8ToUTF16(kUrlPreamble) + html_contents16);
    browser()->OpenURL(OpenURLParams(
        url, Referrer(), CURRENT_TAB, content::PAGE_TRANSITION_TYPED, false));

    // Wait for the tree.
    tree_updated_observer.Wait();

    // Perform a diff (or write the initial baseline).
    string16 actual_contents_utf16;
    helper_.DumpAccessibilityTree(
        host_view->GetBrowserAccessibilityManager()->GetRoot(),
        &actual_contents_utf16);
    std::string actual_contents = UTF16ToUTF8(actual_contents_utf16);
    EXPECT_TRUE(EqualsWithComments(expected_contents, actual_contents));
    if (expected_contents != actual_contents) {
      printf("*** EXPECTED: ***\n%s\n", expected_contents.c_str());
      printf("*** ACTUAL: ***\n%s\n", actual_contents.c_str());
    }

    if (!file_util::PathExists(expected_file)) {
      FilePath actual_file =
          FilePath(html_file.RemoveExtension().value() +
                   helper_.GetActualFileSuffix());

      EXPECT_TRUE(file_util::WriteFile(
          actual_file, actual_contents.c_str(), actual_contents.size()));

      ADD_FAILURE() << "No expectation found. Create it by doing:\n"
          << "mv " << actual_file.LossyDisplayName() << " "
          << expected_file.LossyDisplayName();
    }
  } while (!(html_file = file_enumerator.Next()).empty());
}

