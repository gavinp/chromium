// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options2/options_ui2_uitest.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/automation/automation_proxy.h"
#include "chrome/test/automation/browser_proxy.h"
#include "chrome/test/automation/tab_proxy.h"
#include "chrome/test/ui/ui_test.h"

namespace {

// Same as OptionsUITest but launches with Guest mode command line switches.
class GuestModeOptionsUITest : public options2::OptionsUITest {
 public:
  GuestModeOptionsUITest() : OptionsUITest() {
    launch_arguments_.AppendSwitch(switches::kGuestSession);
    launch_arguments_.AppendSwitch(switches::kIncognito);
  }
};

TEST_F(GuestModeOptionsUITest, LoadOptionsByURL) {
  scoped_refptr<BrowserProxy> browser(automation()->GetBrowserWindow(0));
  ASSERT_TRUE(browser.get());

  scoped_refptr<TabProxy> tab = browser->GetActiveTab();
  ASSERT_TRUE(tab.get());

  NavigateToSettings(tab);
  VerifyTitle(tab);
  VerifyNavbar(tab);
}

}  // namespace
