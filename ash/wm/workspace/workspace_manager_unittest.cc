// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/workspace/workspace_manager.h"

#include "ash/screen_ash.h"
#include "ash/shell.h"
#include "ash/shell_window_ids.h"
#include "ash/test/ash_test_base.h"
#include "ash/wm/activation_controller.h"
#include "ash/wm/property_util.h"
#include "ash/wm/shelf_layout_manager.h"
#include "ash/wm/window_util.h"
#include "ash/wm/workspace_controller.h"
#include "ash/wm/workspace/workspace.h"
#include "ash/wm/workspace/workspace_layout_manager.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/root_window.h"
#include "ui/aura/test/event_generator.h"
#include "ui/aura/window.h"
#include "ui/base/ui_base_types.h"
#include "ui/gfx/compositor/layer.h"
#include "ui/gfx/screen.h"

using aura::Window;

namespace ash {
namespace internal {

class WorkspaceManagerTest : public test::AshTestBase {
 public:
  WorkspaceManagerTest() {}
  virtual ~WorkspaceManagerTest() {}

  aura::Window* CreateTestWindowUnparented() {
    aura::Window* window = new aura::Window(NULL);
    window->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_NORMAL);
    window->SetType(aura::client::WINDOW_TYPE_NORMAL);
    window->Init(ui::LAYER_TEXTURED);
    return window;
  }

  aura::Window* CreateTestWindow() {
    aura::Window* window = new aura::Window(NULL);
    window->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_NORMAL);
    window->SetType(aura::client::WINDOW_TYPE_NORMAL);
    window->Init(ui::LAYER_TEXTURED);
    window->SetParent(GetViewport());
    return window;
  }

  aura::Window* GetViewport() {
    return Shell::GetInstance()->GetContainer(
        internal::kShellWindowId_DefaultContainer);
  }

  const std::vector<Workspace*>& workspaces() const {
    return manager_->workspaces_;
  }

  gfx::Rect GetFullscreenBounds(aura::Window* window) {
    return gfx::Screen::GetMonitorAreaNearestWindow(window);
  }

  Workspace* active_workspace() {
    return manager_->active_workspace_;
  }

  Workspace* FindBy(aura::Window* window) const {
    return manager_->FindBy(window);
  }

  // Overridden from AshTestBase:
  virtual void SetUp() OVERRIDE {
    test::AshTestBase::SetUp();
    Shell::TestApi shell_test(Shell::GetInstance());
    manager_ = shell_test.workspace_controller()->workspace_manager();
    manager_->set_grid_size(0);
  }
  virtual void TearDown() OVERRIDE {
    manager_ = NULL;
    test::AshTestBase::TearDown();
  }

 protected:
  internal::WorkspaceManager* manager_;

 private:
  scoped_ptr<internal::ActivationController> activation_controller_;

  DISALLOW_COPY_AND_ASSIGN(WorkspaceManagerTest);
};

// Assertions around adding a normal window.
TEST_F(WorkspaceManagerTest, AddNormalWindowWhenEmpty) {
  scoped_ptr<Window> w1(CreateTestWindow());
  w1->SetBounds(gfx::Rect(0, 0, 250, 251));

  ASSERT_TRUE(manager_->IsManagedWindow(w1.get()));
  EXPECT_FALSE(FindBy(w1.get()));

  EXPECT_TRUE(GetRestoreBounds(w1.get()) == NULL);

  w1->Show();

  EXPECT_TRUE(GetRestoreBounds(w1.get()) == NULL);

  ASSERT_TRUE(w1->layer() != NULL);
  EXPECT_TRUE(w1->layer()->visible());

  EXPECT_EQ(250, w1->bounds().width());
  EXPECT_EQ(251, w1->bounds().height());

  // Should be 1 workspace, TYPE_NORNMAL with w1.
  ASSERT_EQ(1u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MANAGED, workspaces()[0]->type());
  ASSERT_EQ(1u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
}

// Assertions around maximizing/unmaximizing.
TEST_F(WorkspaceManagerTest, SingleMaximizeWindow) {
  scoped_ptr<Window> w1(CreateTestWindow());
  w1->SetBounds(gfx::Rect(0, 0, 250, 251));

  ASSERT_TRUE(manager_->IsManagedWindow(w1.get()));

  w1->Show();

  ASSERT_TRUE(w1->layer() != NULL);
  EXPECT_TRUE(w1->layer()->visible());

  EXPECT_EQ(250, w1->bounds().width());
  EXPECT_EQ(251, w1->bounds().height());

  // Maximize the window.
  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);

  // Should be 2 workspaces, the second TYPE_MAXIMIZED with w1.
  ASSERT_EQ(2u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MAXIMIZED, workspaces()[1]->type());
  ASSERT_EQ(1u, workspaces()[1]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[1]->windows()[0]);
  EXPECT_EQ(ScreenAsh::GetMaximizedWindowBounds(w1.get()).width(),
            w1->bounds().width());
  EXPECT_EQ(ScreenAsh::GetMaximizedWindowBounds(w1.get()).height(),
            w1->bounds().height());

  // Restore the window.
  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_NORMAL);

  // Should be 1 workspace, TYPE_MANAGED with w1.
  ASSERT_EQ(1u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MANAGED, workspaces()[0]->type());
  ASSERT_EQ(1u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
  EXPECT_EQ(250, w1->bounds().width());
  EXPECT_EQ(251, w1->bounds().height());
}

// Assertions around closing the last window in a workspace.
TEST_F(WorkspaceManagerTest, CloseLastWindowInWorkspace) {
  scoped_ptr<Window> w1(CreateTestWindow());
  scoped_ptr<Window> w2(CreateTestWindow());
  w1->SetBounds(gfx::Rect(0, 0, 250, 251));
  w1->Show();
  w2->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  w2->Show();

  // Should be 2 workspaces, TYPE_NORMAL with w1, and TYPE_MAXIMIZED with w2.
  ASSERT_EQ(2u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MANAGED, workspaces()[0]->type());
  ASSERT_EQ(1u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
  EXPECT_EQ(Workspace::TYPE_MAXIMIZED, workspaces()[1]->type());
  ASSERT_EQ(1u, workspaces()[1]->windows().size());
  EXPECT_EQ(w2.get(), workspaces()[1]->windows()[0]);
  EXPECT_FALSE(w1->layer()->visible());
  EXPECT_TRUE(w2->layer()->visible());
  // TYPE_MAXIMIZED workspace should be active.
  EXPECT_EQ(workspaces()[1], active_workspace());

  // Close w2.
  w2.reset();

  // Should have one workspace, TYPE_NORMAL with w1.
  ASSERT_EQ(1u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MANAGED, workspaces()[0]->type());
  ASSERT_EQ(1u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
  EXPECT_TRUE(w1->layer()->visible());
  EXPECT_EQ(workspaces()[0], active_workspace());
}

// Assertions around adding a maximized window when empty.
TEST_F(WorkspaceManagerTest, AddMaximizedWindowWhenEmpty) {
  scoped_ptr<Window> w1(CreateTestWindow());
  w1->SetBounds(gfx::Rect(0, 0, 250, 251));
  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  w1->Show();

  ASSERT_TRUE(w1->layer() != NULL);
  EXPECT_TRUE(w1->layer()->visible());
  gfx::Rect work_area(ScreenAsh::GetMaximizedWindowBounds(w1.get()));
  EXPECT_EQ(work_area.width(), w1->bounds().width());
  EXPECT_EQ(work_area.height(), w1->bounds().height());

  // Should be 1 workspace, TYPE_NORNMAL with w1.
  ASSERT_EQ(1u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MAXIMIZED, workspaces()[0]->type());
  ASSERT_EQ(1u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
}

// Assertions around two windows and toggling one to be maximized.
TEST_F(WorkspaceManagerTest, MaximizeWithNormalWindow) {
  scoped_ptr<Window> w1(CreateTestWindow());
  scoped_ptr<Window> w2(CreateTestWindow());
  w1->SetBounds(gfx::Rect(0, 0, 250, 251));
  w1->Show();

  ASSERT_TRUE(w1->layer() != NULL);
  EXPECT_TRUE(w1->layer()->visible());

  w2->SetBounds(gfx::Rect(0, 0, 50, 51));
  w2->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  w2->Show();

  // Should now be two workspaces.
  ASSERT_EQ(2u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MANAGED, workspaces()[0]->type());
  ASSERT_EQ(1u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
  EXPECT_EQ(Workspace::TYPE_MAXIMIZED, workspaces()[1]->type());
  ASSERT_EQ(1u, workspaces()[1]->windows().size());
  EXPECT_EQ(w2.get(), workspaces()[1]->windows()[0]);
  ASSERT_TRUE(w1->layer() != NULL);
  EXPECT_FALSE(w1->layer()->visible());
  ASSERT_TRUE(w2->layer() != NULL);
  EXPECT_TRUE(w2->layer()->visible());

  gfx::Rect work_area(ScreenAsh::GetMaximizedWindowBounds(w1.get()));
  EXPECT_EQ(work_area.width(), w2->bounds().width());
  EXPECT_EQ(work_area.height(), w2->bounds().height());

  // Restore w2, which should then go back to one workspace.
  w2->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_NORMAL);
  ASSERT_EQ(1u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MANAGED, workspaces()[0]->type());
  ASSERT_EQ(2u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
  EXPECT_EQ(w2.get(), workspaces()[0]->windows()[1]);
  EXPECT_EQ(50, w2->bounds().width());
  EXPECT_EQ(51, w2->bounds().height());
  ASSERT_TRUE(w1->layer() != NULL);
  EXPECT_TRUE(w1->layer()->visible());
  ASSERT_TRUE(w2->layer() != NULL);
  EXPECT_TRUE(w2->layer()->visible());
}

// Assertions around two maximized windows.
TEST_F(WorkspaceManagerTest, TwoMaximized) {
  scoped_ptr<Window> w1(CreateTestWindow());
  scoped_ptr<Window> w2(CreateTestWindow());
  w1->SetBounds(gfx::Rect(0, 0, 250, 251));
  w1->Show();
  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);

  w2->SetBounds(gfx::Rect(0, 0, 50, 51));
  w2->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  w2->Show();

  // Should now be three workspaces.
  ASSERT_EQ(3u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MAXIMIZED, workspaces()[1]->type());
  ASSERT_EQ(1u, workspaces()[1]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[1]->windows()[0]);
  EXPECT_EQ(Workspace::TYPE_MAXIMIZED, workspaces()[2]->type());
  ASSERT_EQ(1u, workspaces()[2]->windows().size());
  EXPECT_EQ(w2.get(), workspaces()[2]->windows()[0]);
  ASSERT_TRUE(w1->layer() != NULL);
  EXPECT_FALSE(w1->layer()->visible());
  ASSERT_TRUE(w2->layer() != NULL);
  EXPECT_TRUE(w2->layer()->visible());
}

// Makes sure requests to change the bounds of a normal window go through.
TEST_F(WorkspaceManagerTest, ChangeBoundsOfNormalWindow) {
  scoped_ptr<Window> w1(CreateTestWindow());
  w1->Show();

  EXPECT_TRUE(manager_->IsManagedWindow(w1.get()));
  // Setting the bounds should go through since the window is in the normal
  // workspace.
  w1->SetBounds(gfx::Rect(0, 0, 200, 500));
  EXPECT_EQ(200, w1->bounds().width());
  EXPECT_EQ(500, w1->bounds().height());
}

// Assertions around grid size.
TEST_F(WorkspaceManagerTest, SnapToGrid) {
  manager_->set_grid_size(8);

  // Verify snap to grid when bounds are set before parented.
  scoped_ptr<Window> w1(CreateTestWindowUnparented());
  w1->SetBounds(gfx::Rect(1, 6, 25, 30));
  w1->SetParent(GetViewport());
  EXPECT_EQ(gfx::Rect(0, 8, 24, 32), w1->bounds());
}

// Assertions around a fullscreen window.
TEST_F(WorkspaceManagerTest, SingleFullscreenWindow) {
  scoped_ptr<Window> w1(CreateTestWindow());
  w1->SetBounds(gfx::Rect(0, 0, 250, 251));
  // Make the window fullscreen.
  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_FULLSCREEN);
  w1->Show();

  // Should be 1 workspace, TYPE_MAXIMIZED with w1.
  ASSERT_EQ(1u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MAXIMIZED, workspaces()[0]->type());
  ASSERT_EQ(1u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
  EXPECT_EQ(GetFullscreenBounds(w1.get()).width(), w1->bounds().width());
  EXPECT_EQ(GetFullscreenBounds(w1.get()).height(), w1->bounds().height());

  // Restore the window. Use SHOW_STATE_DEFAULT as that is what we'll end up
  // with when using views::Widget.
  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_DEFAULT);
  EXPECT_EQ("0,0 250x251", w1->bounds().ToString());

  // Should be 1 workspace, TYPE_MANAGED with w1.
  ASSERT_EQ(1u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MANAGED, workspaces()[0]->type());
  ASSERT_EQ(1u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
  EXPECT_EQ(250, w1->bounds().width());
  EXPECT_EQ(251, w1->bounds().height());

  // Back to fullscreen.
  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_FULLSCREEN);
  ASSERT_EQ(2u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MAXIMIZED, workspaces()[1]->type());
  ASSERT_EQ(1u, workspaces()[1]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[1]->windows()[0]);
  EXPECT_EQ(GetFullscreenBounds(w1.get()).width(), w1->bounds().width());
  EXPECT_EQ(GetFullscreenBounds(w1.get()).height(), w1->bounds().height());
  ASSERT_TRUE(GetRestoreBounds(w1.get()));
  EXPECT_EQ(gfx::Rect(0, 0, 250, 251), *GetRestoreBounds(w1.get()));
}

// Makes sure switching workspaces doesn't show transient windows.
TEST_F(WorkspaceManagerTest, DontShowTransientsOnSwitch) {
  scoped_ptr<Window> w1(CreateTestWindow());
  scoped_ptr<Window> w2(CreateTestWindow());

  w1->SetBounds(gfx::Rect(0, 0, 250, 251));
  w2->SetBounds(gfx::Rect(0, 0, 250, 251));
  w1->AddTransientChild(w2.get());

  w1->Show();

  scoped_ptr<Window> w3(CreateTestWindow());
  w3->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  w3->Show();

  EXPECT_FALSE(w1->layer()->IsDrawn());
  EXPECT_FALSE(w2->layer()->IsDrawn());
  EXPECT_TRUE(w3->layer()->IsDrawn());

  w1->Show();
  EXPECT_TRUE(w1->layer()->IsDrawn());
  EXPECT_FALSE(w2->layer()->IsDrawn());
  EXPECT_FALSE(w3->layer()->IsDrawn());
}

// Assertions around minimizing a single window.
TEST_F(WorkspaceManagerTest, MinimizeSingleWindow) {
  scoped_ptr<Window> w1(CreateTestWindow());

  w1->Show();
  ASSERT_EQ(1u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MANAGED, workspaces()[0]->type());

  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MINIMIZED);
  ASSERT_EQ(1u, workspaces().size());
  EXPECT_FALSE(w1->layer()->IsDrawn());

  // Show the window.
  w1->Show();
  EXPECT_TRUE(wm::IsWindowNormal(w1.get()));
  ASSERT_EQ(1u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MANAGED, workspaces()[0]->type());
  EXPECT_TRUE(w1->layer()->IsDrawn());
}

// Assertions around minimizing a maximized window.
TEST_F(WorkspaceManagerTest, MinimizeMaximizedWindow) {
  // Two windows, w1 normal, w2 maximized.
  scoped_ptr<Window> w1(CreateTestWindow());
  scoped_ptr<Window> w2(CreateTestWindow());
  w1->Show();
  w2->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  w2->Show();
  ASSERT_EQ(2u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MANAGED, workspaces()[0]->type());
  EXPECT_EQ(Workspace::TYPE_MAXIMIZED, workspaces()[1]->type());

  // Minimize w2.
  w2->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MINIMIZED);
  ASSERT_EQ(1u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MANAGED, workspaces()[0]->type());
  EXPECT_TRUE(w1->layer()->IsDrawn());
  EXPECT_FALSE(w2->layer()->IsDrawn());

  // Show the window, which should trigger unminimizing.
  w2->Show();
  EXPECT_TRUE(wm::IsWindowMaximized(w2.get()));
  ASSERT_EQ(2u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MANAGED, workspaces()[0]->type());
  EXPECT_EQ(Workspace::TYPE_MAXIMIZED, workspaces()[1]->type());
  EXPECT_FALSE(w1->layer()->IsDrawn());
  EXPECT_TRUE(w2->layer()->IsDrawn());

  // Make it active and minimize the window, which should hide the window and
  // activate another.
  wm::ActivateWindow(w2.get());
  EXPECT_TRUE(wm::IsActiveWindow(w2.get()));
  w2->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MINIMIZED);
  EXPECT_FALSE(wm::IsActiveWindow(w2.get()));
  EXPECT_FALSE(w2->layer()->IsDrawn());
  EXPECT_TRUE(wm::IsActiveWindow(w1.get()));

  // Make the window normal.
  w2->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_NORMAL);
  ASSERT_EQ(1u, workspaces().size());
  EXPECT_EQ(Workspace::TYPE_MANAGED, workspaces()[0]->type());
  ASSERT_EQ(2u, workspaces()[0]->windows().size());
  EXPECT_EQ(w1.get(), workspaces()[0]->windows()[0]);
  EXPECT_EQ(w2.get(), workspaces()[0]->windows()[1]);
  EXPECT_TRUE(w2->layer()->IsDrawn());
}

// Verifies ShelfLayoutManager's visibility/auto-hide state is correctly
// updated.
TEST_F(WorkspaceManagerTest, ShelfStateUpdated) {
  // Since ShelfLayoutManager queries for mouse location, move the mouse so
  // it isn't over the shelf.
  aura::test::EventGenerator generator(
      Shell::GetInstance()->GetRootWindow(), gfx::Point());
  generator.MoveMouseTo(0, 0);

  // Two windows, w1 normal, w2 maximized.
  scoped_ptr<Window> w1(CreateTestWindow());
  w1->SetBounds(gfx::Rect(0, 1, 101, 102));
  w1->Show();

  ShelfLayoutManager* shelf = Shell::GetInstance()->shelf();

  EXPECT_EQ(ShelfLayoutManager::VISIBLE, shelf->visibility_state());

  // Maximize the window.
  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE, shelf->visibility_state());
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE_HIDDEN, shelf->auto_hide_state());

  // Restore.
  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_NORMAL);
  EXPECT_EQ(ShelfLayoutManager::VISIBLE, shelf->visibility_state());
  EXPECT_EQ("0,1 101x102", w1->bounds().ToString());

  // Fullscreen.
  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_FULLSCREEN);
  EXPECT_EQ(ShelfLayoutManager::HIDDEN, shelf->visibility_state());

  // Normal.
  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_NORMAL);
  EXPECT_EQ(ShelfLayoutManager::VISIBLE, shelf->visibility_state());
  EXPECT_EQ("0,1 101x102", w1->bounds().ToString());

  // Maximize again.
  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE, shelf->visibility_state());
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE_HIDDEN, shelf->auto_hide_state());

  // Minimize.
  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MINIMIZED);
  EXPECT_EQ(ShelfLayoutManager::VISIBLE, shelf->visibility_state());

  // Restore.
  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_NORMAL);
  EXPECT_EQ(ShelfLayoutManager::VISIBLE, shelf->visibility_state());
  EXPECT_EQ("0,1 101x102", w1->bounds().ToString());

  // Create another window, maximized.
  scoped_ptr<Window> w2(CreateTestWindow());
  w2->SetBounds(gfx::Rect(10, 11, 250, 251));
  w2->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  w2->Show();
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE, shelf->visibility_state());
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE_HIDDEN, shelf->auto_hide_state());
  EXPECT_EQ("0,1 101x102", w1->bounds().ToString());

  // Switch to w1.
  w1->Show();
  EXPECT_EQ(ShelfLayoutManager::VISIBLE, shelf->visibility_state());
  EXPECT_EQ("0,1 101x102", w1->bounds().ToString());
  EXPECT_EQ(ScreenAsh::GetMaximizedWindowBounds(w2.get()).ToString(),
            w2->bounds().ToString());

  // Switch to w2.
  w2->Show();
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE, shelf->visibility_state());
  EXPECT_EQ(ShelfLayoutManager::AUTO_HIDE_HIDDEN, shelf->auto_hide_state());
  EXPECT_EQ("0,1 101x102", w1->bounds().ToString());
  EXPECT_EQ(ScreenAsh::GetMaximizedWindowBounds(w2.get()).ToString(),
            w2->bounds().ToString());
}

}  // namespace internal
}  // namespace ash
