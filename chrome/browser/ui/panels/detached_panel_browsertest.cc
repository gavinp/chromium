// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop.h"
#include "chrome/browser/ui/panels/base_panel_browser_test.h"
#include "chrome/browser/ui/panels/detached_panel_strip.h"
#include "chrome/browser/ui/panels/native_panel.h"
#include "chrome/browser/ui/panels/panel.h"
#include "chrome/browser/ui/panels/panel_manager.h"

class DetachedPanelBrowserTest : public BasePanelBrowserTest {
};

IN_PROC_BROWSER_TEST_F(DetachedPanelBrowserTest, CheckDetachedPanelProperties) {
  PanelManager* panel_manager = PanelManager::GetInstance();
  DetachedPanelStrip* detached_strip = panel_manager->detached_strip();

  // Create 2 panels.
  Panel* panel1 = CreateDetachedPanel("1", gfx::Rect(300, 200, 250, 200));
  Panel* panel2 = CreateDetachedPanel("2", gfx::Rect(350, 180, 300, 200));

  EXPECT_EQ(2, panel_manager->num_panels());
  EXPECT_EQ(2, detached_strip->num_panels());

  EXPECT_TRUE(detached_strip->HasPanel(panel1));
  EXPECT_TRUE(detached_strip->HasPanel(panel2));

  EXPECT_TRUE(panel1->draggable());
  EXPECT_TRUE(panel2->draggable());

  EXPECT_TRUE(panel1->CanResizeByMouse());
  EXPECT_TRUE(panel2->CanResizeByMouse());

  Panel::AttentionMode expected_attention_mode =
      static_cast<Panel::AttentionMode>(Panel::USE_PANEL_ATTENTION |
                                         Panel::USE_SYSTEM_ATTENTION);
  EXPECT_EQ(expected_attention_mode, panel1->attention_mode());
  EXPECT_EQ(expected_attention_mode, panel2->attention_mode());

  panel_manager->CloseAll();
}

IN_PROC_BROWSER_TEST_F(DetachedPanelBrowserTest, DrawAttentionOnActive) {
  // Create a detached panel that is initially active.
  Panel* panel = CreateDetachedPanel("1", gfx::Rect(300, 200, 250, 200));
  scoped_ptr<NativePanelTesting> native_panel_testing(
      NativePanelTesting::Create(panel->native_panel()));

  // Test that the attention should not be drawn if the detached panel is in
  // focus.
  EXPECT_TRUE(panel->IsActive());
  EXPECT_FALSE(panel->IsDrawingAttention());
  panel->FlashFrame(true);
  EXPECT_FALSE(panel->IsDrawingAttention());
  MessageLoop::current()->RunAllPending();
  EXPECT_FALSE(native_panel_testing->VerifyDrawingAttention());

  panel->Close();
}

IN_PROC_BROWSER_TEST_F(DetachedPanelBrowserTest, DrawAttentionOnInactive) {
  // Create an inactive detached panel.
  Panel* panel = CreateDetachedPanel("1", gfx::Rect(300, 200, 250, 200));
  panel->Deactivate();
  WaitForPanelActiveState(panel, SHOW_AS_INACTIVE);

  scoped_ptr<NativePanelTesting> native_panel_testing(
      NativePanelTesting::Create(panel->native_panel()));

  // Test that the attention is drawn when the detached panel is not in focus.
  EXPECT_FALSE(panel->IsActive());
  EXPECT_FALSE(panel->IsDrawingAttention());
  panel->FlashFrame(true);
  EXPECT_TRUE(panel->IsDrawingAttention());
  MessageLoop::current()->RunAllPending();
  EXPECT_TRUE(native_panel_testing->VerifyDrawingAttention());

  // Stop drawing attention.
  panel->FlashFrame(false);
  EXPECT_FALSE(panel->IsDrawingAttention());
  MessageLoop::current()->RunAllPending();
  EXPECT_FALSE(native_panel_testing->VerifyDrawingAttention());

  panel->Close();
}

IN_PROC_BROWSER_TEST_F(DetachedPanelBrowserTest, DrawAttentionResetOnActivate) {
  // Create an inactive detached panel.
  Panel* panel = CreateDetachedPanel("1", gfx::Rect(300, 200, 250, 200));
  panel->Deactivate();
  WaitForPanelActiveState(panel, SHOW_AS_INACTIVE);

  scoped_ptr<NativePanelTesting> native_panel_testing(
      NativePanelTesting::Create(panel->native_panel()));

  // Test that the attention is drawn when the detached panel is not in focus.
  panel->FlashFrame(true);
  EXPECT_TRUE(panel->IsDrawingAttention());
  MessageLoop::current()->RunAllPending();
  EXPECT_TRUE(native_panel_testing->VerifyDrawingAttention());

  // Test that the attention is cleared when panel gets focus.
  panel->Activate();
  WaitForPanelActiveState(panel, SHOW_AS_ACTIVE);
  EXPECT_FALSE(panel->IsDrawingAttention());
  EXPECT_FALSE(native_panel_testing->VerifyDrawingAttention());

  panel->Close();
}

IN_PROC_BROWSER_TEST_F(DetachedPanelBrowserTest, ClickTitlebar) {
  PanelManager* panel_manager = PanelManager::GetInstance();

  Panel* panel = CreateDetachedPanel("1", gfx::Rect(300, 200, 250, 200));
  EXPECT_TRUE(panel->IsActive());
  EXPECT_FALSE(panel->IsMinimized());

  // Clicking on an active detached panel's titlebar has no effect, regardless
  // of modifier.
  scoped_ptr<NativePanelTesting> test_panel(
      NativePanelTesting::Create(panel->native_panel()));
  test_panel->PressLeftMouseButtonTitlebar(panel->GetBounds().origin());
  test_panel->ReleaseMouseButtonTitlebar();
  EXPECT_TRUE(panel->IsActive());
  EXPECT_FALSE(panel->IsMinimized());

  test_panel->PressLeftMouseButtonTitlebar(panel->GetBounds().origin(),
                                           panel::APPLY_TO_ALL);
  test_panel->ReleaseMouseButtonTitlebar(panel::APPLY_TO_ALL);
  EXPECT_TRUE(panel->IsActive());
  EXPECT_FALSE(panel->IsMinimized());

  // Create a second panel to cause the first to become inactive.
  CreateDetachedPanel("2", gfx::Rect(100, 200, 230, 345));
  EXPECT_FALSE(panel->IsActive());

  // Clicking on an inactive detached panel's titlebar activates it.
  test_panel->PressLeftMouseButtonTitlebar(panel->GetBounds().origin());
  test_panel->ReleaseMouseButtonTitlebar();
  WaitForPanelActiveState(panel, SHOW_AS_ACTIVE);
  EXPECT_FALSE(panel->IsMinimized());

  panel_manager->CloseAll();
}
