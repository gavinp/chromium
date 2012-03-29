// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/monitor/multi_monitor_manager.h"

#include "ash/shell.h"
#include "ash/test/ash_test_base.h"
#include "base/format_macros.h"
#include "base/stl_util.h"
#include "base/string_split.h"
#include "base/stringprintf.h"
#include "ui/aura/env.h"
#include "ui/aura/monitor.h"
#include "ui/aura/root_window.h"
#include "ui/aura/window_observer.h"

namespace ash {
namespace test {

using std::vector;
using std::string;
using aura::Monitor;

namespace {

vector<const aura::Monitor*> CreateMonitorsFromString(
    const std::string specs) {
  vector<const aura::Monitor*> monitors;
  vector<string> parts;
  base::SplitString(specs, ',', &parts);
  for (vector<string>::const_iterator iter = parts.begin();
       iter != parts.end(); ++iter) {
    monitors.push_back(aura::MonitorManager::CreateMonitorFromSpec(*iter));
  }
  return monitors;
}

}  // namespace

class MultiMonitorManagerTest : public test::AshTestBase,
                                public aura::MonitorObserver,
                                public aura::WindowObserver {
 public:
  MultiMonitorManagerTest()
      : removed_count_(0U),
        root_window_destroyed_(false) {
  }
  virtual ~MultiMonitorManagerTest() {}

  virtual void SetUp() OVERRIDE {
    AshTestBase::SetUp();
    monitor_manager()->AddObserver(this);
    Shell::GetRootWindow()->AddObserver(this);
  }
  virtual void TearDown() OVERRIDE {
    Shell::GetRootWindow()->RemoveObserver(this);
    monitor_manager()->RemoveObserver(this);
    AshTestBase::TearDown();
  }

  aura::MonitorManager* monitor_manager() {
    return aura::Env::GetInstance()->monitor_manager();
  }
  const vector<const Monitor*>& changed() const { return changed_; }
  const vector<const Monitor*>& added() const { return added_; }

  string GetCountSummary() const {
    return StringPrintf("%"PRIuS" %"PRIuS" %"PRIuS,
                        changed_.size(), added_.size(), removed_count_);
  }

  void reset() {
    changed_.clear();
    added_.clear();
    removed_count_ = 0U;
    root_window_destroyed_ = false;
  }

  bool root_window_destroyed() const {
    return root_window_destroyed_;
  }

  // aura::MonitorObserver overrides:
  virtual void OnMonitorBoundsChanged(const Monitor* monitor) OVERRIDE {
    changed_.push_back(monitor);
  }
  virtual void OnMonitorAdded(Monitor* new_monitor) OVERRIDE {
    added_.push_back(new_monitor);
  }
  virtual void OnMonitorRemoved(const Monitor* old_monitor) OVERRIDE {
    ++removed_count_;
  }

  // aura::WindowObserver overrides:
  virtual void OnWindowDestroying(aura::Window* window) {
    ASSERT_EQ(Shell::GetRootWindow(), window);
    root_window_destroyed_ = true;
  }

  void UpdateMonitor(const std::string str) {
    vector<const aura::Monitor*> monitors = CreateMonitorsFromString(str);
    monitor_manager()->OnNativeMonitorsChanged(monitors);
    STLDeleteContainerPointers(monitors.begin(), monitors.end());
  }

 private:
  vector<const Monitor*> changed_;
  vector<const Monitor*> added_;
  size_t removed_count_;
  bool root_window_destroyed_;

  DISALLOW_COPY_AND_ASSIGN(MultiMonitorManagerTest);
};

TEST_F(MultiMonitorManagerTest, NativeMonitorTest) {
  aura::MonitorManager::set_use_fullscreen_host_window(true);

  EXPECT_EQ(1U, monitor_manager()->GetNumMonitors());

  // Update primary and add seconary.
  UpdateMonitor("0+0-500x500,0+501-400x400");
  EXPECT_EQ(2U, monitor_manager()->GetNumMonitors());
  EXPECT_EQ("1 1 0", GetCountSummary());
  EXPECT_EQ(monitor_manager()->GetMonitorAt(0), changed()[0]);
  EXPECT_EQ(monitor_manager()->GetMonitorAt(1), added()[0]);
  EXPECT_EQ("0,0 500x500", changed()[0]->bounds().ToString());
  EXPECT_EQ("0,501 400x400", added()[0]->bounds().ToString());
  reset();

  // Delete secondary.
  UpdateMonitor("0+0-500x500");
  EXPECT_EQ("0 0 1", GetCountSummary());
  reset();

  // Change primary.
  UpdateMonitor("0+0-1000x600");
  EXPECT_EQ("1 0 0", GetCountSummary());
  EXPECT_EQ(monitor_manager()->GetMonitorAt(0), changed()[0]);
  EXPECT_EQ("0,0 1000x600", changed()[0]->bounds().ToString());
  reset();

  // Add secondary.
  UpdateMonitor("0+0-1000x600,1001+0-600x400");
  EXPECT_EQ(2U, monitor_manager()->GetNumMonitors());
  EXPECT_EQ("0 1 0", GetCountSummary());
  EXPECT_EQ(monitor_manager()->GetMonitorAt(1), added()[0]);
  EXPECT_EQ("1001,0 600x400", added()[0]->bounds().ToString());
  reset();

  // Secondary removed, primary changed.
  UpdateMonitor("0+0-800x300");
  EXPECT_EQ(1U, monitor_manager()->GetNumMonitors());
  EXPECT_EQ("1 0 1", GetCountSummary());
  EXPECT_EQ(monitor_manager()->GetMonitorAt(0), changed()[0]);
  EXPECT_EQ("0,0 800x300", changed()[0]->bounds().ToString());
  reset();

  // # of monitor can go to zero when screen is off.
  const vector<const Monitor*> empty;
  monitor_manager()->OnNativeMonitorsChanged(empty);
  EXPECT_EQ(1U, monitor_manager()->GetNumMonitors());
  EXPECT_EQ("0 0 0", GetCountSummary());
  EXPECT_FALSE(root_window_destroyed());
  // Monitor configuration stays the same
  EXPECT_EQ("0,0 800x300",
            monitor_manager()->GetMonitorAt(0)->bounds().ToString());
  reset();

  // Connect to monitor again
  UpdateMonitor("100+100-500x400");
  EXPECT_EQ(1U, monitor_manager()->GetNumMonitors());
  EXPECT_EQ("1 0 0", GetCountSummary());
  EXPECT_FALSE(root_window_destroyed());
  EXPECT_EQ("100,100 500x400", changed()[0]->bounds().ToString());
  reset();

  // Go back to zero and wake up with multiple monitors.
  monitor_manager()->OnNativeMonitorsChanged(empty);
  EXPECT_EQ(1U, monitor_manager()->GetNumMonitors());
  EXPECT_FALSE(root_window_destroyed());
  reset();

  // Add secondary.
  UpdateMonitor("0+0-1000x600,1000+0-600x400");
  EXPECT_EQ(2U, monitor_manager()->GetNumMonitors());
  EXPECT_EQ("0,0 1000x600",
            monitor_manager()->GetMonitorAt(0)->bounds().ToString());
  EXPECT_EQ("1000,0 600x400",
            monitor_manager()->GetMonitorAt(1)->bounds().ToString());
  reset();

  aura::MonitorManager::set_use_fullscreen_host_window(false);
}

// Test in emulation mode (use_fullscreen_host_window=false)
TEST_F(MultiMonitorManagerTest, EmulatorTest) {
  EXPECT_EQ(1U, monitor_manager()->GetNumMonitors());

  internal::MultiMonitorManager::AddRemoveMonitor();
  // Update primary and add seconary.
  EXPECT_EQ(2U, monitor_manager()->GetNumMonitors());
#if defined(OS_WIN)
  // TODO(oshima): Windows receives resize event for some reason.
  EXPECT_EQ("1 1 0", GetCountSummary());
#else
  EXPECT_EQ("0 1 0", GetCountSummary());
#endif
  reset();

  internal::MultiMonitorManager::CycleMonitor();
  EXPECT_EQ(2U, monitor_manager()->GetNumMonitors());
  // Observer gets called twice in this mode because
  // it gets notified both from |OnNativeMonitorChagned|
  // and from |RootWindowObserver|, which is the consequence of
  // |SetHostSize()|.
  EXPECT_EQ("4 0 0", GetCountSummary());
  reset();

  internal::MultiMonitorManager::AddRemoveMonitor();
  EXPECT_EQ(1U, monitor_manager()->GetNumMonitors());
  EXPECT_EQ("0 0 1", GetCountSummary());
  reset();

  internal::MultiMonitorManager::CycleMonitor();
  EXPECT_EQ(1U, monitor_manager()->GetNumMonitors());
  EXPECT_EQ("0 0 0", GetCountSummary());
  reset();
}

}  // namespace test
}  // namespace ash
