// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/panels/panel_manager.h"

#include "base/auto_reset.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/fullscreen.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/panels/detached_panel_strip.h"
#include "chrome/browser/ui/panels/docked_panel_strip.h"
#include "chrome/browser/ui/panels/overflow_panel_strip.h"
#include "chrome/browser/ui/panels/panel_drag_controller.h"
#include "chrome/browser/ui/panels/panel_mouse_watcher.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/chrome_version_info.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"

#if defined(TOOLKIT_GTK)
#include "ui/base/x/x11_util.h"
#endif

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
bool PanelManager::shorten_time_intervals_ = false;

// static
PanelManager* PanelManager::GetInstance() {
  static base::LazyInstance<PanelManager> instance = LAZY_INSTANCE_INITIALIZER;
  return instance.Pointer();
}

// static
bool PanelManager::ShouldUsePanels(const std::string& extension_id) {
#if defined(TOOLKIT_GTK)
  // Panels are only supported on a white list of window managers for Linux.
  ui::WindowManagerName wm_type = ui::GuessWindowManager();
  if (wm_type != ui::WM_COMPIZ &&
      wm_type != ui::WM_ICE_WM &&
      wm_type != ui::WM_KWIN &&
      wm_type != ui::WM_METACITY &&
      wm_type != ui::WM_MUTTER) {
    return false;
  }
#endif  // TOOLKIT_GTK

  chrome::VersionInfo::Channel channel = chrome::VersionInfo::GetChannel();
  if (channel == chrome::VersionInfo::CHANNEL_STABLE ||
      channel == chrome::VersionInfo::CHANNEL_BETA) {
    return CommandLine::ForCurrentProcess()->HasSwitch(
        switches::kEnablePanels) ||
        extension_id == std::string("nckgahadagoaajjgafhacjanaoiihapd") ||
        extension_id == std::string("ljclpkphhpbpinifbeabbhlfddcpfdde") ||
        extension_id == std::string("ppleadejekpmccmnpjdimmlfljlkdfej") ||
        extension_id == std::string("eggnbpckecmjlblplehfpjjdhhidfdoj");
  }

  return true;
}

PanelManager::PanelManager()
    : panel_mouse_watcher_(PanelMouseWatcher::Create()),
      auto_sizing_enabled_(true),
      is_full_screen_(false),
      is_processing_overflow_(false) {
  detached_strip_.reset(new DetachedPanelStrip(this));
  docked_strip_.reset(new DockedPanelStrip(this));
  overflow_strip_.reset(new OverflowPanelStrip(this));
  drag_controller_.reset(new PanelDragController(this));
  resize_controller_.reset(new PanelResizeController(this));
  display_settings_provider_.reset(DisplaySettingsProvider::Create(this));

  OnDisplayChanged();
}

PanelManager::~PanelManager() {
}

void PanelManager::OnDisplayChanged() {
  gfx::Rect work_area = display_settings_provider_->GetWorkArea();
  if (work_area == work_area_)
    return;
  work_area_ = work_area;

  AdjustWorkAreaForDisplaySettingsProviders();
  Layout();
}

void PanelManager::Layout() {
  int height =
      static_cast<int>(adjusted_work_area_.height() * kPanelStripHeightFactor);
  gfx::Rect docked_strip_bounds;
  docked_strip_bounds.set_x(adjusted_work_area_.x() + kPanelStripLeftMargin);
  docked_strip_bounds.set_y(adjusted_work_area_.bottom() - height);
  docked_strip_bounds.set_width(adjusted_work_area_.width() -
                                kPanelStripLeftMargin - kPanelStripRightMargin);
  docked_strip_bounds.set_height(height);
  docked_strip_->SetDisplayArea(docked_strip_bounds);

  gfx::Rect overflow_area(adjusted_work_area_);
  overflow_area.set_width(kOverflowStripThickness);
  overflow_strip_->SetDisplayArea(overflow_area);
}

Panel* PanelManager::CreatePanel(Browser* browser) {
  // Need to sync the display area if no panel is present. This is because we
  // could only get display area notifications through a panel window.
  if (num_panels() == 0)
    OnDisplayChanged();

  int width = browser->override_bounds().width();
  int height = browser->override_bounds().height();
  Panel* panel = new Panel(browser, gfx::Size(width, height));
  docked_strip_->AddPanel(panel, PanelStrip::DEFAULT_POSITION);
  docked_strip_->UpdatePanelOnStripChange(panel);

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
  return docked_strip_->StartingRightPosition();
}

void PanelManager::CheckFullScreenMode() {
  bool is_full_screen_new = IsFullScreenMode();
  if (is_full_screen_ == is_full_screen_new)
    return;
  is_full_screen_ = is_full_screen_new;
  docked_strip_->OnFullScreenModeChanged(is_full_screen_);
  overflow_strip_->OnFullScreenModeChanged(is_full_screen_);
}

void PanelManager::OnPanelClosed(Panel* panel) {
  if (num_panels() == 1)
    full_screen_mode_timer_.Stop();

  drag_controller_->OnPanelClosed(panel);
  resize_controller_->OnPanelClosed(panel);
  panel->panel_strip()->RemovePanel(panel);

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_PANEL_CLOSED,
      content::Source<Panel>(panel),
      content::NotificationService::NoDetails());
}

void PanelManager::StartDragging(Panel* panel,
                                 const gfx::Point& mouse_location) {
  drag_controller_->StartDragging(panel, mouse_location);
}

void PanelManager::Drag(const gfx::Point& mouse_location) {
  drag_controller_->Drag(mouse_location);
}

void PanelManager::EndDragging(bool cancelled) {
  drag_controller_->EndDragging(cancelled);
}

void PanelManager::StartResizingByMouse(Panel* panel,
    const gfx::Point& mouse_location,
    PanelResizeController::ResizingSides sides) {
   if (panel->panel_strip() && panel->panel_strip()->CanResizePanel(panel) &&
      sides != PanelResizeController::NO_SIDES)
    resize_controller_->StartResizing(panel, mouse_location, sides);
}

void PanelManager::ResizeByMouse(const gfx::Point& mouse_location) {
  if (resize_controller_->IsResizing())
    resize_controller_->Resize(mouse_location);
}

void PanelManager::EndResizingByMouse(bool cancelled) {
  if (resize_controller_->IsResizing())
    resize_controller_->EndResizing(cancelled);
}

void PanelManager::OnPanelExpansionStateChanged(Panel* panel) {
  // For panels outside of the docked strip changing state is a no-op.
  // But since this method may be called for panels in other strips
  // we need to check this condition.
  if (panel->panel_strip() == docked_strip_.get())
    docked_strip_->OnPanelExpansionStateChanged(panel);

}

void PanelManager::OnWindowAutoResized(Panel* panel,
                                       const gfx::Size& preferred_window_size) {
  DCHECK(auto_sizing_enabled_);
  // Even though overflow panels are always minimized, we need
  // to keep track of their size to put them back into the
  // docked strip when they fit. So the docked panel strip manages
  // the size of panels for the overflow strip as well.
  if (panel->panel_strip() == overflow_strip_.get())
    docked_strip_->ResizePanelWindow(panel, preferred_window_size);
  else
    panel->panel_strip()->ResizePanelWindow(panel, preferred_window_size);
}

void PanelManager::ResizePanel(Panel* panel, const gfx::Size& new_size) {
  // See the comment in OnWindowAutoResized()
  if (panel->panel_strip() == overflow_strip_.get())
    docked_strip_->ResizePanelWindow(panel, new_size);
  else
    panel->panel_strip()->ResizePanelWindow(panel, new_size);
  panel->SetAutoResizable(false);
}

void PanelManager::SetPanelBounds(Panel* panel,
                                  const gfx::Rect& new_bounds) {
  panel->panel_strip()->SetPanelBounds(panel, new_bounds);
  panel->SetAutoResizable(false);

}

void PanelManager::MovePanelToStrip(
    Panel* panel,
    PanelStrip::Type new_layout,
    PanelStrip::PositioningMask positioning_mask) {
  DCHECK(panel);
  PanelStrip* current_strip = panel->panel_strip();
  DCHECK(current_strip);
  DCHECK_NE(current_strip->type(), new_layout);
  current_strip->RemovePanel(panel);

  PanelStrip* target_strip = NULL;
  switch (new_layout) {
    case PanelStrip::DETACHED:
      target_strip = detached_strip_.get();
      break;
    case PanelStrip::DOCKED:
      target_strip = docked_strip_.get();
      break;
    case PanelStrip::IN_OVERFLOW:
      target_strip = overflow_strip_.get();
      break;
    default:
      NOTREACHED();
  }

  target_strip->AddPanel(panel, positioning_mask);
  target_strip->UpdatePanelOnStripChange(panel);

  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_PANEL_CHANGED_LAYOUT_MODE,
      content::Source<Panel>(panel),
      content::NotificationService::NoDetails());
}

void PanelManager::MovePanelsToOverflow(Panel* last_panel_to_move) {
  AutoReset<bool> processing_overflow(&is_processing_overflow_, true);
  // Move panels to overflow in reverse to maintain their order.
  Panel* bumped_panel;
  while ((bumped_panel = docked_strip_->last_panel())) {
    MovePanelToStrip(bumped_panel,
                     PanelStrip::IN_OVERFLOW,
                     PanelStrip::DEFAULT_POSITION);
    if (bumped_panel == last_panel_to_move)
      break;
  }
  DCHECK(!docked_strip_->panels().empty());
}

void PanelManager::MovePanelsOutOfOverflowIfCanFit() {
  if (is_processing_overflow_)
    return;

  Panel* overflow_panel;
  while ((overflow_panel = overflow_strip_->first_panel()) &&
         docked_strip_->CanFitPanel(overflow_panel)) {
    MovePanelToStrip(overflow_panel,
                     PanelStrip::DOCKED,
                     PanelStrip::DEFAULT_POSITION);
  }
}

bool PanelManager::ShouldBringUpTitlebars(int mouse_x, int mouse_y) const {
  return docked_strip_->ShouldBringUpTitlebars(mouse_x, mouse_y);
}

void PanelManager::BringUpOrDownTitlebars(bool bring_up) {
  docked_strip_->BringUpOrDownTitlebars(bring_up);
}

void PanelManager::AdjustWorkAreaForDisplaySettingsProviders() {
  // Note that we do not care about the desktop bar aligned to the top edge
  // since panels could not reach so high due to size constraint.
  adjusted_work_area_ = work_area_;
  if (display_settings_provider_->IsAutoHidingDesktopBarEnabled(
      DisplaySettingsProvider::DESKTOP_BAR_ALIGNED_LEFT)) {
    int space = display_settings_provider_->GetDesktopBarThickness(
        DisplaySettingsProvider::DESKTOP_BAR_ALIGNED_LEFT);
    adjusted_work_area_.set_x(adjusted_work_area_.x() + space);
    adjusted_work_area_.set_width(adjusted_work_area_.width() - space);
  }
  if (display_settings_provider_->IsAutoHidingDesktopBarEnabled(
      DisplaySettingsProvider::DESKTOP_BAR_ALIGNED_RIGHT)) {
    int space = display_settings_provider_->GetDesktopBarThickness(
        DisplaySettingsProvider::DESKTOP_BAR_ALIGNED_RIGHT);
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
  AdjustWorkAreaForDisplaySettingsProviders();
  Layout();
}

void PanelManager::OnAutoHidingDesktopBarVisibilityChanged(
    DisplaySettingsProvider::DesktopBarAlignment alignment,
    DisplaySettingsProvider::DesktopBarVisibility visibility) {
  docked_strip_->OnAutoHidingDesktopBarVisibilityChanged(alignment, visibility);
}

void PanelManager::CloseAll() {
  DCHECK(!drag_controller_->IsDragging());

  detached_strip_->CloseAll();
  docked_strip_->CloseAll();
  overflow_strip_->CloseAll();
}

int PanelManager::num_panels() const {
  return detached_strip_->num_panels() +
         docked_strip_->num_panels() +
         overflow_strip_->num_panels();
}

std::vector<Panel*> PanelManager::panels() const {
  std::vector<Panel*> panels;
  for (DetachedPanelStrip::Panels::const_iterator iter =
           detached_strip_->panels().begin();
       iter != detached_strip_->panels().end(); ++iter)
    panels.push_back(*iter);
  for (DockedPanelStrip::Panels::const_iterator iter =
           docked_strip_->panels().begin();
       iter != docked_strip_->panels().end(); ++iter)
    panels.push_back(*iter);
  for (OverflowPanelStrip::Panels::const_iterator iter =
           overflow_strip_->panels().begin();
       iter != overflow_strip_->panels().end(); ++iter)
    panels.push_back(*iter);
  return panels;
}

int PanelManager::overflow_strip_width() const {
  return kOverflowStripThickness;
}

void PanelManager::SetMouseWatcher(PanelMouseWatcher* watcher) {
  panel_mouse_watcher_.reset(watcher);
}
