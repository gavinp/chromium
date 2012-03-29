// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_path.h"
#include "content/test/layout_browsertest.h"

class IndexedDBLayoutTest : public InProcessBrowserLayoutTest {
 public:
  IndexedDBLayoutTest() : InProcessBrowserLayoutTest(
      FilePath().AppendASCII("storage").AppendASCII("indexeddb")) {
  }

  virtual void SetUpInProcessBrowserTestFixture() OVERRIDE {
    InProcessBrowserLayoutTest::SetUpInProcessBrowserTestFixture();
    AddResourceForLayoutTest(
        FilePath().AppendASCII("fast").AppendASCII("js"),
        FilePath().AppendASCII("resources"));
  }

  void RunLayoutTests(const char* file_names[]) {
    for (size_t i = 0; file_names[i]; i++)
      RunLayoutTest(file_names[i]);
  }
};

namespace {

static const char* kBasicTests[] = {
  "basics.html",
  "basics-shared-workers.html",
  "basics-workers.html",
  "database-basics.html",
  "factory-basics.html",
  "index-basics.html",
  "objectstore-basics.html",
  NULL
};

static const char* kComplexTests[] = {
  "prefetch-bugfix-108071.html",
  NULL
};

static const char* kIndexTests[] = {
  "deleteIndex.html",
  "index-basics-workers.html",
  "index-count.html",
  "index-cursor.html",  // Locally takes ~6s compared to <1 for the others.
  "index-get-key-argument-required.html",
  "index-multientry.html",
  "index-population.html",
  "index-unique.html",
  NULL
};

static const char* kKeyTests[] = {
  "key-generator.html",
  "keypath-basics.html",
  "keypath-edges.html",
  "keypath-fetch-key.html",
  "keyrange.html",
  "keyrange-required-arguments.html",
  "key-sort-order-across-types.html",
  "key-sort-order-date.html",
  "key-type-array.html",
  "key-type-infinity.html",
  "invalid-keys.html",
  NULL
};

static const char* kTransactionTests[] = {
//  "transaction-abort.html", // Flaky, http://crbug.com/83226
  "transaction-abort-with-js-recursion-cross-frame.html",
  "transaction-abort-with-js-recursion.html",
  "transaction-abort-workers.html",
  "transaction-after-close.html",
  "transaction-and-objectstore-calls.html",
  "transaction-basics.html",
  "transaction-crash-on-abort.html",
  "transaction-event-propagation.html",
  "transaction-read-only.html",
  "transaction-rollback.html",
  "transaction-storeNames-required.html",
  NULL
};

}

IN_PROC_BROWSER_TEST_F(IndexedDBLayoutTest, BasicTests) {
  RunLayoutTests(kBasicTests);
}

IN_PROC_BROWSER_TEST_F(IndexedDBLayoutTest, ComplexTests) {
  RunLayoutTests(kComplexTests);
}

IN_PROC_BROWSER_TEST_F(IndexedDBLayoutTest, IndexTests) {
  RunLayoutTests(kIndexTests);
}

IN_PROC_BROWSER_TEST_F(IndexedDBLayoutTest, KeyTests) {
  RunLayoutTests(kKeyTests);
}

IN_PROC_BROWSER_TEST_F(IndexedDBLayoutTest, TransactionTests) {
  RunLayoutTests(kTransactionTests);
}
