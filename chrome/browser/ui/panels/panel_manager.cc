// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/panels/panel_manager.h"

#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/fullscreen.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/panels/panel_mouse_watcher.h"
#include "chrome/browser/ui/panels/panel_overflow_strip.h"
#include "chrome/browser/ui/panels/panel_strip.h"
#include "chrome/browser/ui/window_sizer.h"
#include "chrome/common/chrome_notification_types.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"

namespace {
const int kOverflowStripThickness = 26;

// Width of spacing around panel strip and the left/right edges of the screen.
const int kPanelStripLeftMargin = kOverflowStripThickness + 6;
const int kPanelStripRightMargin = 24;

// Height of panel strip is based on the factor of the working area.
const double kPanelStripHeightFactor = 0.5;

static const int kFullScreenModeCheckIntervalMs = 1000;

}  // namespace

// static
PanelManager* PanelManager::GetInstance() {
  static base::LazyInstance<PanelManager> instance = LAZY_INSTANCE_INITIALIZER;
  return instance.Pointer();
}

PanelManager::PanelManager()
    : panel_mouse_watcher_(PanelMouseWatcher::Create()),
      auto_sizing_enabled_(true),
      is_full_screen_(false) {
  panel_strip_.reset(new PanelStrip(this));
  panel_overflow_strip_.reset(new PanelOverflowStrip(this));
  auto_hiding_desktop_bar_ = AutoHidingDesktopBar::Create(this);
  OnDisplayChanged();
}

PanelManager::~PanelManager() {
}

void PanelManager::OnDisplayChanged() {
  scoped_ptr<WindowSizer::MonitorInfoProvider> info_provider(
      WindowSizer::CreateDefaultMonitorInfoProvider());
#if defined(OS_MACOSX)
  // On OSX, panels should be dropped all the way to the bottom edge of the
  // screen (and overlap Dock).
  gfx::Rect work_area = info_provider->GetPrimaryMonitorBounds();
#else
  gfx::Rect work_area = info_provider->GetPrimaryMonitorWorkArea();
#endif
  SetWorkArea(work_area);
}

void PanelManager::SetWorkArea(const gfx::Rect& work_area) {
  if (work_area == work_area_)
    return;
  work_area_ = work_area;

  auto_hiding_desktop_bar_->UpdateWorkArea(work_area_);
  AdjustWorkAreaForAutoHidingDesktopBars();
  Layout();
}

void PanelManager::Layout() {
  int height =
      static_cast<int>(adjusted_work_area_.height() * kPanelStripHeightFactor);
  gfx::Rect panel_strip_bounds;
  panel_strip_bounds.set_x(adjusted_work_area_.x() + kPanelStripLeftMargin);
  panel_strip_bounds.set_y(adjusted_work_area_.bottom() - height);
  panel_strip_bounds.set_width(adjusted_work_area_.width() -
                               kPanelStripLeftMargin - kPanelStripRightMargin);
  panel_strip_bounds.set_height(height);
  panel_strip_->SetDisplayArea(panel_strip_bounds);

  gfx::Rect overflow_area(adjusted_work_area_);
  overflow_area.set_width(kOverflowStripThickness);
  panel_overflow_strip_->SetDisplayArea(overflow_area);
}

Panel* PanelManager::CreatePanel(Browser* browser) {
  int width = browser->override_bounds().width();
  int height = browser->override_bounds().height();
  Panel* panel = new Panel(browser, gfx::Size(width, height));
  panel_strip_->AddPanel(panel);

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_PANEL_ADDED,
      content::Source<Panel>(panel),
      content::NotificationService::NoDetails());

  if (num_panels() == 1) {
    full_screen_mode_timer_.Start(FROM_HERE,
        base::TimeDelta::FromMilliseconds(kFullScreenModeCheckIntervalMs),
        this, &PanelManager::CheckFullScreenMode);
  }

  return panel;
}

int PanelManager::StartingRightPosition() const {
  return panel_strip_->StartingRightPosition();
}

void PanelManager::CheckFullScreenMode() {
  bool is_full_screen_new = IsFullScreenMode();
  if (is_full_screen_ == is_full_screen_new)
    return;
  is_full_screen_ = is_full_screen_new;
  panel_strip_->OnFullScreenModeChanged(is_full_screen_);
  panel_overflow_strip_->OnFullScreenModeChanged(is_full_screen_);
}

void PanelManager::Remove(Panel* panel) {
  if (num_panels() == 1)
    full_screen_mode_timer_.Stop();

  if (panel_strip_->Remove(panel))
    return;
  bool removed = panel_overflow_strip_->Remove(panel);
  DCHECK(removed);
}

void PanelManager::OnPanelRemoved(Panel* panel) {
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_PANEL_REMOVED,
      content::Source<Panel>(panel),
      content::NotificationService::NoDetails());
}

void PanelManager::StartDragging(Panel* panel) {
  panel_strip_->StartDragging(panel);
}

void PanelManager::Drag(int delta_x) {
  panel_strip_->Drag(delta_x);
}

void PanelManager::EndDragging(bool cancelled) {
  panel_strip_->EndDragging(cancelled);
}

void PanelManager::OnPanelExpansionStateChanged(
    Panel* panel, Panel::ExpansionState old_state) {
  if (panel->expansion_state() == Panel::IN_OVERFLOW)
    panel_overflow_strip_->OnPanelExpansionStateChanged(panel, old_state);
  else
    panel_strip_->OnPanelExpansionStateChanged(panel, old_state);
}

void PanelManager::OnPreferredWindowSizeChanged(
    Panel* panel, const gfx::Size& preferred_window_size) {
  if (!auto_sizing_enabled_)
    return;
  panel_strip_->OnPreferredWindowSizeChanged(panel, preferred_window_size);
}

bool PanelManager::ShouldBringUpTitlebars(int mouse_x, int mouse_y) const {
  return panel_strip_->ShouldBringUpTitlebars(mouse_x, mouse_y);
}

void PanelManager::BringUpOrDownTitlebars(bool bring_up) {
  panel_strip_->BringUpOrDownTitlebars(bring_up);
}

void PanelManager::AdjustWorkAreaForAutoHidingDesktopBars() {
  // Note that we do not care about the desktop bar aligned to the top edge
  // since panels could not reach so high due to size constraint.
  adjusted_work_area_ = work_area_;
  if (auto_hiding_desktop_bar_->IsEnabled(AutoHidingDesktopBar::ALIGN_LEFT)) {
    int space = auto_hiding_desktop_bar_->GetThickness(
        AutoHidingDesktopBar::ALIGN_LEFT);
    adjusted_work_area_.set_x(adjusted_work_area_.x() + space);
    adjusted_work_area_.set_width(adjusted_work_area_.width() - space);
  }
  if (auto_hiding_desktop_bar_->IsEnabled(AutoHidingDesktopBar::ALIGN_RIGHT)) {
    int space = auto_hiding_desktop_bar_->GetThickness(
        AutoHidingDesktopBar::ALIGN_RIGHT);
    adjusted_work_area_.set_width(adjusted_work_area_.width() - space);
  }
}

BrowserWindow* PanelManager::GetNextBrowserWindowToActivate(
    Panel* panel) const {
  // Find the last active browser window that is not minimized.
  BrowserList::const_reverse_iterator iter = BrowserList::begin_last_active();
  BrowserList::const_reverse_iterator end = BrowserList::end_last_active();
  for (; (iter != end); ++iter) {
    Browser* browser = *iter;
    if (panel->browser() != browser && !browser->window()->IsMinimized())
      return browser->window();
  }

  return NULL;
}

void PanelManager::OnAutoHidingDesktopBarThicknessChanged() {
  AdjustWorkAreaForAutoHidingDesktopBars();
  Layout();
}

void PanelManager::OnAutoHidingDesktopBarVisibilityChanged(
    AutoHidingDesktopBar::Alignment alignment,
    AutoHidingDesktopBar::Visibility visibility) {
  panel_strip_->OnAutoHidingDesktopBarVisibilityChanged(alignment, visibility);
}

void PanelManager::RemoveAll() {
  panel_strip_->RemoveAll();
  panel_overflow_strip_->RemoveAll();
}

int PanelManager::num_panels() const {
  return panel_strip_->num_panels() + panel_overflow_strip_->num_panels();
}

bool PanelManager::is_dragging_panel() const {
  return panel_strip_->is_dragging_panel();
}

const PanelManager::Panels& PanelManager::panels() const {
  return panel_strip_->panels();
}
