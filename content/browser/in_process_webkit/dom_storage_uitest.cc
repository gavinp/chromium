// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/path_service.h"
#include "base/test/test_timeouts.h"
#include "chrome/test/automation/tab_proxy.h"
#include "chrome/test/ui/ui_layout_test.h"
#include "content/public/common/content_paths.h"
#include "content/public/common/content_switches.h"
#include "net/base/net_util.h"
#include "webkit/dom_storage/dom_storage_types.h"

#ifdef ENABLE_NEW_DOM_STORAGE_BACKEND
// No longer applicable.
#else

static const char* kRootFiles[] = {
  "clear.html",
//  "complex-keys.html",  // Output too big for a cookie.  crbug.com/33472
//  "complex-values.html",  // crbug.com/33472
  "quota.html",
  "remove-item.html",
  "window-attributes-exist.html",
  NULL
};

static const char* kEventsFiles[] = {
//  "basic-body-attribute.html",  // crbug.com/33472
//  "basic.html",  // crbug.com/33472
//  "basic-setattribute.html",  // crbug.com/33472
  "case-sensitive.html",
  "documentURI.html",
  NULL
};

static const char* kStorageFiles[] = {
  "delete-removal.html",
  "enumerate-storage.html",
  "enumerate-with-length-and-key.html",
  "index-get-and-set.html",
  "simple-usage.html",
  "string-conversion.html",
//  "window-open.html", // TODO(jorlow): Fix
  NULL
};

class DOMStorageTest : public UILayoutTest {
 protected:
  DOMStorageTest()
      : UILayoutTest(),
        test_dir_(FilePath().
                  AppendASCII("storage").AppendASCII("domstorage")) {
  }

  virtual ~DOMStorageTest() { }

  virtual void SetUp() {
    launch_arguments_.AppendSwitch(switches::kDisablePopupBlocking);
    UILayoutTest::SetUp();
  }

  // We require fast/js/resources for most of the DOM Storage layout tests.
  // Add those to the list to be copied.
  void AddJSTestResources() {
    // Add other paths our tests require.
    FilePath js_dir = FilePath().
                      AppendASCII("fast").AppendASCII("js");
    AddResourceForLayoutTest(js_dir, FilePath().AppendASCII("resources"));
  }

  // This is somewhat of a hack because we're running a real browser that
  // actually persists the LocalStorage state vs. DRT and TestShell which don't.
  // The correct fix is to fix the LayoutTests, but similar patches have been
  // rejected in the past.
  void ClearDOMStorage() {
    scoped_refptr<TabProxy> tab(GetActiveTab());
    ASSERT_TRUE(tab.get());

    FilePath dir;
    PathService::Get(content::DIR_TEST_DATA, &dir);
    GURL url = net::FilePathToFileURL(
        dir.AppendASCII("layout_tests").AppendASCII("clear_dom_storage.html"));

    ASSERT_TRUE(tab->SetCookie(url, ""));
    ASSERT_TRUE(tab->NavigateToURL(url));

    WaitUntilCookieNonEmpty(tab.get(), url, "cleared",
                            TestTimeouts::action_max_timeout_ms());
  }

  // Runs each test in an array of strings until it hits a NULL.
  void RunTests(const char** files) {
    while (*files) {
      ClearDOMStorage();
      RunLayoutTest(*files, kNoHttpPort);
      ++files;
    }
  }

  FilePath test_dir_;
};


// http://crbug.com/113611
TEST_F(DOMStorageTest, FAILS_RootLayoutTests) {
  InitializeForLayoutTest(test_dir_, FilePath(), kNoHttpPort);
  AddJSTestResources();
  AddResourceForLayoutTest(test_dir_, FilePath().AppendASCII("script-tests"));
  RunTests(kRootFiles);
}

// Flakily fails on all platforms.  http://crbug.com/102641
TEST_F(DOMStorageTest, DISABLED_EventLayoutTests) {
  InitializeForLayoutTest(test_dir_, FilePath().AppendASCII("events"),
                          kNoHttpPort);
  AddJSTestResources();
  AddResourceForLayoutTest(test_dir_, FilePath().AppendASCII("events").
                                      AppendASCII("resources"));
  AddResourceForLayoutTest(test_dir_, FilePath().AppendASCII("events").
                                      AppendASCII("script-tests"));
  RunTests(kEventsFiles);
}

#if defined(OS_LINUX)
// http://crbug.com/104872
#define MAYBE_LocalStorageLayoutTests FAILS_LocalStorageLayoutTests
#else
#define MAYBE_LocalStorageLayoutTests LocalStorageLayoutTests
#endif

TEST_F(DOMStorageTest, MAYBE_LocalStorageLayoutTests) {
  InitializeForLayoutTest(test_dir_, FilePath().AppendASCII("localstorage"),
                          kNoHttpPort);
  AddJSTestResources();
  AddResourceForLayoutTest(test_dir_, FilePath().AppendASCII("localstorage").
                                      AppendASCII("resources"));
  RunTests(kStorageFiles);
}

#if defined(OS_LINUX)
// http://crbug.com/104872
#define MAYBE_SessionStorageLayoutTests FAILS_SessionStorageLayoutTests
#else
#define MAYBE_SessionStorageLayoutTests SessionStorageLayoutTests
#endif

TEST_F(DOMStorageTest, MAYBE_SessionStorageLayoutTests) {
  InitializeForLayoutTest(test_dir_, FilePath().AppendASCII("sessionstorage"),
                          kNoHttpPort);
  AddJSTestResources();
  AddResourceForLayoutTest(test_dir_, FilePath().AppendASCII("sessionstorage").
                                      AppendASCII("resources"));
  RunTests(kStorageFiles);
}

class DomStorageEmptyDatabaseTest : public UITest {
 protected:
  FilePath StorageDir() const {
    FilePath storage_dir = user_data_dir();
    storage_dir = storage_dir.AppendASCII("Default");
    storage_dir = storage_dir.AppendASCII("Local Storage");
    return storage_dir;
  }

  bool StorageDirIsEmpty() const {
    FilePath storage_dir = StorageDir();
    if (!file_util::DirectoryExists(storage_dir))
      return true;
    return file_util::IsDirectoryEmpty(storage_dir);
  }

  GURL TestUrl() const {
    FilePath test_dir = test_data_directory_;
    FilePath test_file = test_dir.AppendASCII("dom_storage_empty_db.html");
    return net::FilePathToFileURL(test_file);
  }
};

TEST_F(DomStorageEmptyDatabaseTest, EmptyDirAfterClear) {
  NavigateToURL(TestUrl());
  ASSERT_TRUE(StorageDirIsEmpty());

  NavigateToURL(GURL("javascript:set()"));
  NavigateToURL(GURL("javascript:clear()"));
  QuitBrowser();
  EXPECT_TRUE(StorageDirIsEmpty());
}

TEST_F(DomStorageEmptyDatabaseTest, EmptyDirAfterGet) {
  NavigateToURL(TestUrl());
  ASSERT_TRUE(StorageDirIsEmpty());

  NavigateToURL(GURL("javascript:get()"));
  QuitBrowser();
  EXPECT_TRUE(StorageDirIsEmpty());
}

#if defined(OS_WIN)
// Flaky, see http://crbug.com/73776
#define MAYBE_NonEmptyDirAfterSet DISABLED_NonEmptyDirAfterSet
#else
#define MAYBE_NonEmptyDirAfterSet NonEmptyDirAfterSet
#endif
TEST_F(DomStorageEmptyDatabaseTest, MAYBE_NonEmptyDirAfterSet) {
  NavigateToURL(TestUrl());
  ASSERT_TRUE(StorageDirIsEmpty());

  NavigateToURL(GURL("javascript:set()"));
  QuitBrowser();
  EXPECT_FALSE(StorageDirIsEmpty());

  LaunchBrowserAndServer();
  NavigateToURL(TestUrl());
  NavigateToURL(GURL("javascript:clear()"));
  QuitBrowser();
  EXPECT_TRUE(StorageDirIsEmpty());
}

#endif  // ENABLE_NEW_DOM_STORAGE_BACKEND
