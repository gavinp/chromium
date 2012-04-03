// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/shell.h"

#include <algorithm>

#include "ash/app_list/app_list.h"
#include "ash/ash_switches.h"
#include "ash/desktop_background/desktop_background_controller.h"
#include "ash/desktop_background/desktop_background_resources.h"
#include "ash/desktop_background/desktop_background_view.h"
#include "ash/drag_drop/drag_drop_controller.h"
#include "ash/focus_cycler.h"
#include "ash/ime/input_method_event_filter.h"
#include "ash/launcher/launcher.h"
#include "ash/monitor/multi_monitor_manager.h"
#include "ash/monitor/monitor_controller.h"
#include "ash/screen_ash.h"
#include "ash/shell_delegate.h"
#include "ash/shell_factory.h"
#include "ash/shell_window_ids.h"
#include "ash/system/audio/tray_volume.h"
#include "ash/system/bluetooth/tray_bluetooth.h"
#include "ash/system/brightness/tray_brightness.h"
#include "ash/system/date/tray_date.h"
#include "ash/system/ime/tray_ime.h"
#include "ash/system/network/tray_network.h"
#include "ash/system/power/power_status_observer.h"
#include "ash/system/power/power_supply_status.h"
#include "ash/system/power/tray_power.h"
#include "ash/system/settings/tray_settings.h"
#include "ash/system/tray/system_tray_delegate.h"
#include "ash/system/tray/system_tray.h"
#include "ash/system/tray/tray_empty.h"
#include "ash/system/tray_accessibility.h"
#include "ash/system/tray_caps_lock.h"
#include "ash/system/tray_update.h"
#include "ash/system/user/tray_user.h"
#include "ash/tooltips/tooltip_controller.h"
#include "ash/wm/activation_controller.h"
#include "ash/wm/base_layout_manager.h"
#include "ash/wm/custom_frame_view_ash.h"
#include "ash/wm/dialog_frame_view.h"
#include "ash/wm/event_client_impl.h"
#include "ash/wm/key_rewriter_event_filter.h"
#include "ash/wm/panel_window_event_filter.h"
#include "ash/wm/panel_layout_manager.h"
#include "ash/wm/partial_screenshot_event_filter.h"
#include "ash/wm/power_button_controller.h"
#include "ash/wm/resize_shadow_controller.h"
#include "ash/wm/root_window_event_filter.h"
#include "ash/wm/root_window_layout_manager.h"
#include "ash/wm/shadow_controller.h"
#include "ash/wm/shelf_layout_manager.h"
#include "ash/wm/stacking_controller.h"
#include "ash/wm/status_area_layout_manager.h"
#include "ash/wm/system_modal_container_layout_manager.h"
#include "ash/wm/toplevel_window_event_filter.h"
#include "ash/wm/video_detector.h"
#include "ash/wm/visibility_controller.h"
#include "ash/wm/window_cycle_controller.h"
#include "ash/wm/window_modality_controller.h"
#include "ash/wm/window_util.h"
#include "ash/wm/workspace/always_on_top_layout_manager.h"
#include "ash/wm/workspace_controller.h"
#include "ash/wm/workspace/workspace_event_filter.h"
#include "ash/wm/workspace/workspace_layout_manager.h"
#include "ash/wm/workspace/workspace_manager.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "grit/ui_resources.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/env.h"
#include "ui/aura/layout_manager.h"
#include "ui/aura/monitor.h"
#include "ui/aura/monitor_manager.h"
#include "ui/aura/monitor_manager.h"
#include "ui/aura/root_window.h"
#include "ui/aura/ui_controls_aura.h"
#include "ui/aura/window.h"
#include "ui/gfx/compositor/layer.h"
#include "ui/gfx/compositor/layer_animator.h"
#include "ui/gfx/size.h"
#include "ui/ui_controls/ui_controls.h"
#include "ui/views/widget/native_widget_aura.h"
#include "ui/views/widget/widget.h"

#if !defined(OS_MACOSX)
#include "ash/accelerators/accelerator_controller.h"
#include "ash/accelerators/accelerator_filter.h"
#include "ash/accelerators/nested_dispatcher_controller.h"
#endif

namespace ash {

namespace {

using aura::Window;
using views::Widget;

// Creates a new window for use as a container.
aura::Window* CreateContainer(int window_id,
                              const char* name,
                              aura::Window* parent) {
  aura::Window* container = new aura::Window(NULL);
  container->set_id(window_id);
  container->SetName(name);
  container->Init(ui::LAYER_NOT_DRAWN);
  parent->AddChild(container);
  if (window_id != internal::kShellWindowId_UnparentedControlContainer)
    container->Show();
  return container;
}

// Creates each of the special window containers that holds windows of various
// types in the shell UI.
void CreateSpecialContainers(aura::RootWindow* root_window) {
  // These containers are just used by PowerButtonController to animate groups
  // of containers simultaneously without messing up the current transformations
  // on those containers.  These are direct children of the root window; all of
  // the other containers are their children.
  aura::Window* non_lock_screen_containers = CreateContainer(
      internal::kShellWindowId_NonLockScreenContainersContainer,
      "NonLockScreenContainersContainer",
      root_window);
  aura::Window* lock_screen_containers = CreateContainer(
      internal::kShellWindowId_LockScreenContainersContainer,
      "LockScreenContainersContainer",
      root_window);
  aura::Window* lock_screen_related_containers = CreateContainer(
      internal::kShellWindowId_LockScreenRelatedContainersContainer,
      "LockScreenRelatedContainersContainer",
      root_window);

  CreateContainer(internal::kShellWindowId_UnparentedControlContainer,
                  "UnparentedControlContainer",
                  non_lock_screen_containers);

  CreateContainer(internal::kShellWindowId_DesktopBackgroundContainer,
                  "DesktopBackgroundContainer",
                  non_lock_screen_containers);

  aura::Window* default_container = CreateContainer(
      internal::kShellWindowId_DefaultContainer,
      "DefaultContainer",
      non_lock_screen_containers);
  default_container->SetEventFilter(
      new ToplevelWindowEventFilter(default_container));
  SetChildWindowVisibilityChangesAnimated(default_container);

  aura::Window* always_on_top_container = CreateContainer(
      internal::kShellWindowId_AlwaysOnTopContainer,
      "AlwaysOnTopContainer",
      non_lock_screen_containers);
  always_on_top_container->SetEventFilter(
      new ToplevelWindowEventFilter(always_on_top_container));
  SetChildWindowVisibilityChangesAnimated(always_on_top_container);

  aura::Window* panel_container = CreateContainer(
      internal::kShellWindowId_PanelContainer,
      "PanelContainer",
      non_lock_screen_containers);
  if (CommandLine::ForCurrentProcess()->
      HasSwitch(switches::kAuraPanelManager)) {
    internal::PanelLayoutManager* layout_manager =
        new internal::PanelLayoutManager(panel_container);
    panel_container->SetEventFilter(
        new internal::PanelWindowEventFilter(panel_container, layout_manager));
    panel_container->SetLayoutManager(layout_manager);
  }

  CreateContainer(internal::kShellWindowId_AppListContainer,
                  "AppListContainer",
                  non_lock_screen_containers);

  CreateContainer(internal::kShellWindowId_LauncherContainer,
                  "LauncherContainer",
                  non_lock_screen_containers);

  aura::Window* modal_container = CreateContainer(
      internal::kShellWindowId_SystemModalContainer,
      "SystemModalContainer",
      non_lock_screen_containers);
  modal_container->SetEventFilter(
      new ToplevelWindowEventFilter(modal_container));
  modal_container->SetLayoutManager(
      new internal::SystemModalContainerLayoutManager(modal_container));
  SetChildWindowVisibilityChangesAnimated(modal_container);

  // TODO(beng): Figure out if we can make this use
  // SystemModalContainerEventFilter instead of stops_event_propagation.
  aura::Window* lock_container = CreateContainer(
      internal::kShellWindowId_LockScreenContainer,
      "LockScreenContainer",
      lock_screen_containers);
  lock_container->SetLayoutManager(
      new internal::BaseLayoutManager(root_window));
  // TODO(beng): stopsevents

  aura::Window* lock_modal_container = CreateContainer(
      internal::kShellWindowId_LockSystemModalContainer,
      "LockSystemModalContainer",
      lock_screen_containers);
  lock_modal_container->SetEventFilter(
      new ToplevelWindowEventFilter(lock_modal_container));
  lock_modal_container->SetLayoutManager(
      new internal::SystemModalContainerLayoutManager(lock_modal_container));
  SetChildWindowVisibilityChangesAnimated(lock_modal_container);

  CreateContainer(internal::kShellWindowId_StatusContainer,
                  "StatusContainer",
                  lock_screen_related_containers);

  aura::Window* menu_container = CreateContainer(
      internal::kShellWindowId_MenuContainer,
      "MenuContainer",
      lock_screen_related_containers);
  SetChildWindowVisibilityChangesAnimated(menu_container);

  aura::Window* drag_drop_container = CreateContainer(
      internal::kShellWindowId_DragImageAndTooltipContainer,
      "DragImageAndTooltipContainer",
      lock_screen_related_containers);
  SetChildWindowVisibilityChangesAnimated(drag_drop_container);

  aura::Window* settings_bubble_container = CreateContainer(
      internal::kShellWindowId_SettingBubbleContainer,
      "SettingBubbleContainer",
      lock_screen_related_containers);
  SetChildWindowVisibilityChangesAnimated(settings_bubble_container);

  CreateContainer(internal::kShellWindowId_OverlayContainer,
                  "OverlayContainer",
                  lock_screen_related_containers);
}

// This dummy class is used for shell unit tests. We dont have chrome delegate
// in these tests.
class DummyUserWallpaperDelegate : public UserWallpaperDelegate {
 public:
  DummyUserWallpaperDelegate() {}

  virtual ~DummyUserWallpaperDelegate() {}

  virtual const int GetUserWallpaperIndex() OVERRIDE {
    return 0;
  }

  virtual void OpenSetWallpaperPage() OVERRIDE {
  }

 private:
   DISALLOW_COPY_AND_ASSIGN(DummyUserWallpaperDelegate);
};

class DummySystemTrayDelegate : public SystemTrayDelegate {
 public:
  DummySystemTrayDelegate()
      : muted_(false),
        wifi_enabled_(true),
        cellular_enabled_(true),
        bluetooth_enabled_(true),
        volume_(0.5) {
  }

  virtual ~DummySystemTrayDelegate() {}

 private:

  virtual bool GetTrayVisibilityOnStartup() OVERRIDE { return true; }

  // Overridden from SystemTrayDelegate:
  virtual const std::string GetUserDisplayName() const OVERRIDE {
    return "Über tray Über tray Über tray Über tray";
  }

  virtual const std::string GetUserEmail() const OVERRIDE {
    return "über@tray";
  }

  virtual const SkBitmap& GetUserImage() const OVERRIDE {
    return null_image_;
  }

  virtual user::LoginStatus GetUserLoginStatus() const OVERRIDE {
    return user::LOGGED_IN_USER;
  }

  virtual bool SystemShouldUpgrade() const OVERRIDE {
    return true;
  }

  virtual int GetSystemUpdateIconResource() const OVERRIDE {
    return IDR_AURA_UBER_TRAY_UPDATE;
  }

  virtual base::HourClockType GetHourClockType() const OVERRIDE {
    return base::k24HourClock;
  }

  virtual PowerSupplyStatus GetPowerSupplyStatus() const OVERRIDE {
    return PowerSupplyStatus();
  }

  virtual void ShowSettings() OVERRIDE {
  }

  virtual void ShowDateSettings() OVERRIDE {
  }

  virtual void ShowNetworkSettings() OVERRIDE {
  }

  virtual void ShowBluetoothSettings() OVERRIDE {
  }

  virtual void ShowIMESettings() OVERRIDE {
  }

  virtual void ShowHelp() OVERRIDE {
  }

  virtual bool IsAudioMuted() const OVERRIDE {
    return muted_;
  }

  virtual void SetAudioMuted(bool muted) OVERRIDE {
    muted_ = muted;
  }

  virtual float GetVolumeLevel() const OVERRIDE {
    return volume_;
  }

  virtual void SetVolumeLevel(float volume) OVERRIDE {
    volume_ = volume;
  }

  virtual bool IsCapsLockOn() const OVERRIDE {
    return false;
  }

  virtual bool IsInAccessibilityMode() const OVERRIDE {
    return false;
  }

  virtual void ShutDown() OVERRIDE {}

  virtual void SignOut() OVERRIDE {
    MessageLoop::current()->Quit();
  }

  virtual void RequestLockScreen() OVERRIDE {}

  virtual void RequestRestart() OVERRIDE {}

  virtual void GetAvailableBluetoothDevices(
      BluetoothDeviceList* list) OVERRIDE {
  }

  virtual void ToggleBluetoothConnection(const std::string& address) OVERRIDE {
  }

  virtual void GetCurrentIME(IMEInfo* info) OVERRIDE {
  }

  virtual void GetAvailableIMEList(IMEInfoList* list) OVERRIDE {
  }

  virtual void GetCurrentIMEProperties(IMEPropertyInfoList* list) OVERRIDE {
  }

  virtual void SwitchIME(const std::string& ime_id) OVERRIDE {
  }

  virtual void ActivateIMEProperty(const std::string& key) OVERRIDE {
  }

  virtual void GetMostRelevantNetworkIcon(NetworkIconInfo* info,
                                          bool large) OVERRIDE {
  }

  virtual void GetAvailableNetworks(
      std::vector<NetworkIconInfo>* list) OVERRIDE {
  }

  virtual void ConnectToNetwork(const std::string& network_id) OVERRIDE {
  }

  virtual void GetNetworkAddresses(std::string* ip_address,
                                   std::string* ethernet_mac_address,
                                   std::string* wifi_mac_address) OVERRIDE {
    *ip_address = "127.0.0.1";
    *ethernet_mac_address = "00:11:22:33:44:55";
    *wifi_mac_address = "66:77:88:99:00:11";
  }

  virtual void AddBluetoothDevice() OVERRIDE {
  }

  virtual void ToggleAirplaneMode() OVERRIDE {
  }

  virtual void ToggleWifi() OVERRIDE {
    wifi_enabled_ = !wifi_enabled_;
    ash::NetworkObserver* observer =
        ash::Shell::GetInstance()->tray()->network_observer();
    if (observer) {
      ash::NetworkIconInfo info;
      observer->OnNetworkRefresh(info);
    }
  }

  virtual void ToggleCellular() OVERRIDE {
    cellular_enabled_ = !cellular_enabled_;
    ash::NetworkObserver* observer =
        ash::Shell::GetInstance()->tray()->network_observer();
    if (observer) {
      ash::NetworkIconInfo info;
      observer->OnNetworkRefresh(info);
    }
  }

  virtual void ToggleBluetooth() OVERRIDE {
    bluetooth_enabled_ = !bluetooth_enabled_;
    ash::BluetoothObserver* observer =
        ash::Shell::GetInstance()->tray()->bluetooth_observer();
    if (observer)
      observer->OnBluetoothRefresh();
  }

  virtual void ShowOtherWifi() OVERRIDE {
  }

  virtual void ShowOtherCellular() OVERRIDE {
  }

  virtual bool GetWifiAvailable() OVERRIDE {
    return true;
  }

  virtual bool GetCellularAvailable() OVERRIDE {
    return true;
  }

  virtual bool GetBluetoothAvailable() OVERRIDE {
    return true;
  }

  virtual bool GetWifiEnabled() OVERRIDE {
    return wifi_enabled_;
  }

  virtual bool GetCellularEnabled() OVERRIDE {
    return cellular_enabled_;
  }

  virtual bool GetBluetoothEnabled() OVERRIDE {
    return bluetooth_enabled_;
  }

  virtual bool GetCellularScanSupported() OVERRIDE {
    return true;
  }

  virtual bool GetCellularCarrierInfo(std::string* carrier_id,
                                      std::string* toup_url) OVERRIDE {
    return false;
  }

  virtual void ShowCellularTopupURL(const std::string& topup_url) OVERRIDE {
  }

  virtual void ChangeProxySettings() OVERRIDE {
  }

  bool muted_;
  bool wifi_enabled_;
  bool cellular_enabled_;
  bool bluetooth_enabled_;
  float volume_;
  SkBitmap null_image_;

  DISALLOW_COPY_AND_ASSIGN(DummySystemTrayDelegate);
};

}  // namespace

// static
Shell* Shell::instance_ = NULL;
// static
bool Shell::initially_hide_cursor_ = false;

////////////////////////////////////////////////////////////////////////////////
// Shell::TestApi

Shell::TestApi::TestApi(Shell* shell) : shell_(shell) {}

internal::RootWindowLayoutManager* Shell::TestApi::root_window_layout() {
  return shell_->root_window_layout_;
}

internal::InputMethodEventFilter* Shell::TestApi::input_method_event_filter() {
  return shell_->input_method_filter_.get();
}

internal::WorkspaceController* Shell::TestApi::workspace_controller() {
  return shell_->workspace_controller_.get();
}

////////////////////////////////////////////////////////////////////////////////
// Shell, public:

Shell::Shell(ShellDelegate* delegate)
    : root_window_(aura::MonitorManager::CreateRootWindowForPrimaryMonitor()),
      screen_(new ScreenAsh(root_window_.get())),
      root_filter_(NULL),
      delegate_(delegate),
      shelf_(NULL),
      root_window_layout_(NULL),
      status_widget_(NULL) {
  gfx::Screen::SetInstance(screen_);
  ui_controls::InstallUIControlsAura(CreateUIControlsAura(root_window_.get()));
}

Shell::~Shell() {
  RemoveRootWindowEventFilter(key_rewriter_filter_.get());
  RemoveRootWindowEventFilter(partial_screenshot_filter_.get());
  RemoveRootWindowEventFilter(input_method_filter_.get());
  RemoveRootWindowEventFilter(window_modality_controller_.get());
#if !defined(OS_MACOSX)
  RemoveRootWindowEventFilter(accelerator_filter_.get());
#endif

  // Close background widget now so that the focus manager of the
  // widget gets deleted in the final message loop run.
  root_window_layout_->SetBackgroundWidget(NULL);

  // TooltipController is deleted with the Shell so removing its references.
  RemoveRootWindowEventFilter(tooltip_controller_.get());
  aura::client::SetTooltipClient(GetRootWindow(), NULL);

  // Make sure we delete WorkspaceController before launcher is
  // deleted as it has a reference to launcher model.
  workspace_controller_.reset();

  // The system tray needs to be reset before all the windows are destroyed.
  tray_.reset();

  // Desroy secondary monitor's widgets before all the windows are destroyed.
  monitor_controller_.reset();

  // Delete containers now so that child windows does not access
  // observers when they are destructed.
  aura::RootWindow* root_window = GetRootWindow();
  while (!root_window->children().empty()) {
    aura::Window* child = root_window->children()[0];
    delete child;
  }

  // These need a valid Shell instance to clean up properly, so explicitly
  // delete them before invalidating the instance.
  // Alphabetical.
  activation_controller_.reset();
  drag_drop_controller_.reset();
  resize_shadow_controller_.reset();
  shadow_controller_.reset();
  window_cycle_controller_.reset();
  event_client_.reset();
  monitor_controller_.reset();

  // Launcher widget has a InputMethodBridge that references to
  // input_method_filter_'s input_method_. So explicitly release launcher_
  // before input_method_filter_. And this needs to be after we delete all
  // containers in case there are still live browser windows which access
  // LauncherModel during close.
  launcher_.reset();

  DCHECK(instance_ == this);
  instance_ = NULL;
}

// static
Shell* Shell::CreateInstance(ShellDelegate* delegate) {
  CHECK(!instance_);
  aura::Env::GetInstance()->SetMonitorManager(
      new internal::MultiMonitorManager());
  instance_ = new Shell(delegate);
  instance_->Init();
  return instance_;
}

// static
Shell* Shell::GetInstance() {
  DCHECK(instance_);
  return instance_;
}

// static
bool Shell::HasInstance() {
  return !!instance_;
}

// static
void Shell::DeleteInstance() {
  delete instance_;
  instance_ = NULL;
}

// static
aura::RootWindow* Shell::GetRootWindow() {
  return GetInstance()->root_window_.get();
}

void Shell::Init() {
  aura::RootWindow* root_window = GetRootWindow();
  root_filter_ = new internal::RootWindowEventFilter(root_window);
#if !defined(OS_MACOSX)
  nested_dispatcher_controller_.reset(new NestedDispatcherController);
  accelerator_controller_.reset(new AcceleratorController);
#endif
  // Pass ownership of the filter to the root window.
  GetRootWindow()->SetEventFilter(root_filter_);

  // KeyRewriterEventFilter must be the first one.
  DCHECK(!GetRootWindowEventFilterCount());
  key_rewriter_filter_.reset(new internal::KeyRewriterEventFilter);
  AddRootWindowEventFilter(key_rewriter_filter_.get());

  // PartialScreenshotEventFilter must be the second one to capture key
  // events when the taking partial screenshot UI is there.
  DCHECK_EQ(1U, GetRootWindowEventFilterCount());
  partial_screenshot_filter_.reset(new internal::PartialScreenshotEventFilter);
  AddRootWindowEventFilter(partial_screenshot_filter_.get());

  // Then AcceleratorFilter and InputMethodEventFilter must be added (in this
  // order) since they have the second highest priority.
  DCHECK_EQ(2U, GetRootWindowEventFilterCount());
#if !defined(OS_MACOSX)
  accelerator_filter_.reset(new internal::AcceleratorFilter);
  AddRootWindowEventFilter(accelerator_filter_.get());
  DCHECK_EQ(3U, GetRootWindowEventFilterCount());
#endif
  input_method_filter_.reset(new internal::InputMethodEventFilter);
  AddRootWindowEventFilter(input_method_filter_.get());

  root_window->SetCursor(aura::kCursorPointer);
  if (initially_hide_cursor_)
    root_window->ShowCursor(false);

  activation_controller_.reset(new internal::ActivationController);

  CreateSpecialContainers(root_window);

  stacking_controller_.reset(new internal::StackingController);

  root_window_layout_ = new internal::RootWindowLayoutManager(root_window);
  root_window->SetLayoutManager(root_window_layout_);

  event_client_.reset(new internal::EventClientImpl(root_window));

  if (delegate_.get())
    status_widget_ = delegate_->CreateStatusArea();

  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (!command_line->HasSwitch(switches::kDisableAshUberTray)) {
    // TODO(sad): This is rather ugly at the moment. This is because we are
    // supporting both the old and the new status bar at the same time. This
    // will soon get better once the new one is ready and the old one goes out
    // the door.
    tray_.reset(new SystemTray());
    if (status_widget_) {
      status_widget_->GetContentsView()->RemoveAllChildViews(false);
      status_widget_->GetContentsView()->AddChildView(tray_.get());
    }

    if (delegate_.get())
      tray_delegate_.reset(delegate_->CreateSystemTrayDelegate(tray_.get()));
    if (!tray_delegate_.get())
      tray_delegate_.reset(new DummySystemTrayDelegate());

    internal::TrayVolume* tray_volume = new internal::TrayVolume();
    internal::TrayBluetooth* tray_bluetooth = new internal::TrayBluetooth();
    internal::TrayBrightness* tray_brightness = new internal::TrayBrightness();
    internal::TrayDate* tray_date = new internal::TrayDate();
    internal::TrayPower* tray_power = new internal::TrayPower();
    internal::TrayNetwork* tray_network = new internal::TrayNetwork;
    internal::TrayUser* tray_user = new internal::TrayUser;
    internal::TrayAccessibility* tray_accessibility =
        new internal::TrayAccessibility;
    internal::TrayCapsLock* tray_caps_lock = new internal::TrayCapsLock;
    internal::TrayIME* tray_ime = new internal::TrayIME;
    internal::TrayUpdate* tray_update = new internal::TrayUpdate;

    tray_->accessibility_observer_ = tray_accessibility;
    tray_->audio_observer_ = tray_volume;
    tray_->bluetooth_observer_ = tray_bluetooth;
    tray_->brightness_observer_ = tray_brightness;
    tray_->caps_lock_observer_ = tray_caps_lock;
    tray_->clock_observer_ = tray_date;
    tray_->ime_observer_ = tray_ime;
    tray_->network_observer_ = tray_network;
    tray_->power_status_observer_ = tray_power;
    tray_->update_observer_ = tray_update;
    tray_->user_observer_ = tray_user;

    tray_->AddTrayItem(tray_user);
    tray_->AddTrayItem(new internal::TrayEmpty());
    tray_->AddTrayItem(tray_power);
    tray_->AddTrayItem(tray_network);
    tray_->AddTrayItem(tray_bluetooth);
    tray_->AddTrayItem(tray_ime);
    tray_->AddTrayItem(tray_volume);
    tray_->AddTrayItem(tray_brightness);
    tray_->AddTrayItem(tray_update);
    tray_->AddTrayItem(new internal::TraySettings());
    tray_->AddTrayItem(tray_accessibility);
    tray_->AddTrayItem(tray_caps_lock);
    tray_->AddTrayItem(tray_date);

    tray_->SetVisible(tray_delegate_->GetTrayVisibilityOnStartup());
  }
  if (!status_widget_)
    status_widget_ = internal::CreateStatusArea(tray_.get());

  // This controller needs to be set before SetupManagedWindowMode.
  desktop_background_controller_.reset(new DesktopBackgroundController);
  if (delegate_.get())
    user_wallpaper_delegate_.reset(delegate_->CreateUserWallpaperDelegate());
  if (!user_wallpaper_delegate_.get())
    user_wallpaper_delegate_.reset(new DummyUserWallpaperDelegate());

  InitLayoutManagers();

  if (!command_line->HasSwitch(switches::kAuraNoShadows)) {
    resize_shadow_controller_.reset(new internal::ResizeShadowController());
    shadow_controller_.reset(new internal::ShadowController());
  }

  focus_cycler_.reset(new internal::FocusCycler());
  focus_cycler_->AddWidget(status_widget_);

  if (!delegate_.get() || delegate_->IsUserLoggedIn())
    CreateLauncher();

  // Force a layout.
  root_window->layout_manager()->OnWindowResized();

  window_modality_controller_.reset(new internal::WindowModalityController);
  AddRootWindowEventFilter(window_modality_controller_.get());

  visibility_controller_.reset(new internal::VisibilityController);

  tooltip_controller_.reset(new internal::TooltipController);
  AddRootWindowEventFilter(tooltip_controller_.get());

  drag_drop_controller_.reset(new internal::DragDropController);
  power_button_controller_.reset(new PowerButtonController);
  video_detector_.reset(new VideoDetector);
  window_cycle_controller_.reset(new WindowCycleController);
  monitor_controller_.reset(new internal::MonitorController);
}

aura::Window* Shell::GetContainer(int container_id) {
  return const_cast<aura::Window*>(
      const_cast<const Shell*>(this)->GetContainer(container_id));
}

const aura::Window* Shell::GetContainer(int container_id) const {
  return GetRootWindow()->GetChildById(container_id);
}

void Shell::AddRootWindowEventFilter(aura::EventFilter* filter) {
  static_cast<internal::RootWindowEventFilter*>(
      GetRootWindow()->event_filter())->AddFilter(filter);
}

void Shell::RemoveRootWindowEventFilter(aura::EventFilter* filter) {
  static_cast<internal::RootWindowEventFilter*>(
      GetRootWindow()->event_filter())->RemoveFilter(filter);
}

size_t Shell::GetRootWindowEventFilterCount() const {
  return static_cast<internal::RootWindowEventFilter*>(
      GetRootWindow()->event_filter())->GetFilterCount();
}

void Shell::ShowBackgroundMenu(views::Widget* widget,
                               const gfx::Point& location) {
  if (workspace_controller_.get())
    workspace_controller_->ShowMenu(widget, location);
}

void Shell::ToggleAppList() {
  if (!app_list_.get())
    app_list_.reset(new internal::AppList);
  app_list_->SetVisible(!app_list_->IsVisible());
}

bool Shell::GetAppListTargetVisibility() const {
  return app_list_.get() && app_list_->GetTargetVisibility();
}

bool Shell::IsScreenLocked() const {
  return !delegate_.get() || delegate_->IsScreenLocked();
}

bool Shell::IsModalWindowOpen() const {
  const aura::Window* modal_container = GetContainer(
      internal::kShellWindowId_SystemModalContainer);
  return !modal_container->children().empty();
}

views::NonClientFrameView* Shell::CreateDefaultNonClientFrameView(
    views::Widget* widget) {
  if (CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kAuraGoogleDialogFrames)) {
    return new internal::DialogFrameView;
  }
  // Use translucent-style window frames for dialogs.
  CustomFrameViewAsh* frame_view = new CustomFrameViewAsh;
  frame_view->Init(widget);
  return frame_view;
}

void Shell::RotateFocus(Direction direction) {
  focus_cycler_->RotateFocus(
      direction == FORWARD ? internal::FocusCycler::FORWARD :
                             internal::FocusCycler::BACKWARD);
}

void Shell::SetMonitorWorkAreaInsets(Window* contains,
                                     const gfx::Insets& insets) {
  aura::Monitor* monitor = aura::Env::GetInstance()->monitor_manager()->
      GetMonitorNearestWindow(contains);
  if (monitor->work_area_insets() == insets)
    return;
  monitor->set_work_area_insets(insets);
  FOR_EACH_OBSERVER(ShellObserver, observers_,
                    OnMonitorWorkAreaInsetsChanged());
}

void Shell::CreateLauncher() {
  if (launcher_.get())
    return;

  aura::Window* default_container =
      GetContainer(internal::kShellWindowId_DefaultContainer);
  launcher_.reset(new Launcher(default_container));

  launcher_->SetFocusCycler(focus_cycler_.get());
  shelf_->SetLauncher(launcher_.get());

  launcher_->widget()->Show();
}

void Shell::AddShellObserver(ShellObserver* observer) {
  observers_.AddObserver(observer);
}

void Shell::RemoveShellObserver(ShellObserver* observer) {
  observers_.RemoveObserver(observer);
}

void Shell::UpdateShelfVisibility() {
  shelf_->UpdateVisibilityState();
}

void Shell::SetShelfAutoHideBehavior(ShelfAutoHideBehavior behavior) {
  shelf_->SetAutoHideBehavior(behavior);
}

ShelfAutoHideBehavior Shell::GetShelfAutoHideBehavior() const {
  return shelf_->auto_hide_behavior();
}

int Shell::GetGridSize() const {
  return workspace_controller_->workspace_manager()->grid_size();
}

////////////////////////////////////////////////////////////////////////////////
// Shell, private:

void Shell::InitLayoutManagers() {
  DCHECK(root_window_layout_);
  DCHECK(status_widget_);

  internal::ShelfLayoutManager* shelf_layout_manager =
      new internal::ShelfLayoutManager(status_widget_);
  GetContainer(internal::kShellWindowId_LauncherContainer)->
      SetLayoutManager(shelf_layout_manager);
  shelf_ = shelf_layout_manager;

  internal::StatusAreaLayoutManager* status_area_layout_manager =
      new internal::StatusAreaLayoutManager(shelf_layout_manager);
  GetContainer(internal::kShellWindowId_StatusContainer)->
      SetLayoutManager(status_area_layout_manager);

  aura::Window* default_container =
      GetContainer(internal::kShellWindowId_DefaultContainer);
  // Workspace manager has its own layout managers.
  workspace_controller_.reset(
      new internal::WorkspaceController(default_container));
  workspace_controller_->workspace_manager()->set_shelf(shelf_layout_manager);
  shelf_layout_manager->set_workspace_manager(
      workspace_controller_->workspace_manager());

  aura::Window* always_on_top_container =
      GetContainer(internal::kShellWindowId_AlwaysOnTopContainer);
  always_on_top_container->SetLayoutManager(
      new internal::AlwaysOnTopLayoutManager(
          always_on_top_container->GetRootWindow()));

  // Create desktop background widget.
  // TODO(bshe): We should be able to use OnDesktopBackgroundChanged function
  // here after issue 117244 got fixed.
  int index = user_wallpaper_delegate_->GetUserWallpaperIndex();
  desktop_background_controller_->SetDesktopBackgroundImageMode(
      GetWallpaper(index), GetWallpaperInfo(index).layout);
}

void Shell::DisableWorkspaceGridLayout() {
  if (workspace_controller_.get())
    workspace_controller_->workspace_manager()->set_grid_size(0);
}

}  // namespace ash
