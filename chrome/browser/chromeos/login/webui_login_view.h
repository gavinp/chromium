// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_WEBUI_LOGIN_VIEW_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_WEBUI_LOGIN_VIEW_H_
#pragma once

#include <map>
#include <string>

#include "chrome/browser/chromeos/login/login_html_dialog.h"
#include "chrome/browser/chromeos/status/status_area_button.h"
#include "chrome/browser/chromeos/status/status_area_view_chromeos.h"
#include "chrome/browser/tab_render_watcher.h"
#include "chrome/browser/ui/views/unhandled_keyboard_event_handler.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/web_contents_delegate.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

class DOMView;
class GURL;
class StatusAreaView;

namespace content {
class WebUI;
}

namespace views {
class View;
class Widget;
}

namespace chromeos {

// View used to render a WebUI supporting Widget. This widget is used for the
// WebUI based start up and lock screens. It contains a StatusAreaView and
// DOMView.
class WebUILoginView : public views::WidgetDelegateView,
                       public StatusAreaButton::Delegate,
                       public content::WebContentsDelegate,
                       public content::NotificationObserver,
                       public TabRenderWatcher::Delegate {
 public:
  static const int kStatusAreaCornerPadding;

  WebUILoginView();
  virtual ~WebUILoginView();

  // Initializes the webui login view.
  virtual void Init(views::Widget* login_window);

  // Overridden from views::Views:
  virtual bool AcceleratorPressed(
      const ui::Accelerator& accelerator) OVERRIDE;
  virtual std::string GetClassName() const OVERRIDE;

  // Called when WebUI window is created.
  virtual void OnWindowCreated();

  // Gets the native window from the view widget.
  gfx::NativeWindow GetNativeWindow() const;

  // Invokes SetWindowType for the window. This is invoked during startup and
  // after we've painted.
  void UpdateWindowType();

  // Loads given page. Should be called after Init() has been called.
  void LoadURL(const GURL& url);

  // Returns current WebUI.
  content::WebUI* GetWebUI();

  // Opens proxy settings dialog.
  void OpenProxySettings();

  // Toggles whether status area is enabled.
  void SetStatusAreaEnabled(bool enable);

  // Toggles status area visibility.
  void SetStatusAreaVisible(bool visible);

 protected:
  // Let non-login derived classes suppress emission of this signal.
  void set_should_emit_login_prompt_visible(bool emit) {
    should_emit_login_prompt_visible_ = emit;
  }

  // Overridden from views::View:
  virtual void Layout() OVERRIDE;
  virtual void OnLocaleChanged() OVERRIDE;
  virtual void ChildPreferredSizeChanged(View* child) OVERRIDE;

  // Overridden from StatusAreaButton::Delegate:
  virtual bool ShouldExecuteStatusAreaCommand(
      const views::View* button_view, int command_id) const OVERRIDE;
  virtual void ExecuteStatusAreaCommand(
      const views::View* button_view, int command_id) OVERRIDE;
  virtual StatusAreaButton::TextStyle GetStatusAreaTextStyle() const OVERRIDE;
  virtual void ButtonVisibilityChanged(views::View* button_view) OVERRIDE;

  // TabRenderWatcher::Delegate implementation.
  virtual void OnRenderHostCreated(content::RenderViewHost* host) OVERRIDE;
  virtual void OnTabMainFrameLoaded() OVERRIDE;
  virtual void OnTabMainFrameRender() OVERRIDE;

  // Creates and adds the status area (separate window).
  virtual void InitStatusArea();

  // Returns the screen mode to set on the status area view.
  virtual StatusAreaViewChromeos::ScreenMode GetScreenMode();

  // Returns the type to use for the status area widget.
  virtual views::Widget::InitParams::Type GetStatusAreaWidgetType();

  // Overridden from content::NotificationObserver.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  StatusAreaView* status_area_;

  // DOMView for rendering a webpage as a webui login.
  DOMView* webui_login_;

 private:
  // Map type for the accelerator-to-identifier map.
  typedef std::map<ui::Accelerator, std::string> AccelMap;

  // Overridden from content::WebContentsDelegate.
  virtual bool HandleContextMenu(
      const content::ContextMenuParams& params) OVERRIDE;
  virtual void HandleKeyboardEvent(
      const NativeWebKeyboardEvent& event) OVERRIDE;
  virtual bool IsPopupOrPanel(
      const content::WebContents* source) const OVERRIDE;
  virtual bool TakeFocus(bool reverse) OVERRIDE;

  // Called when focus is returned from status area.
  // |reverse| is true when focus is traversed backwards (using Shift-Tab).
  void ReturnFocus(bool reverse);

  content::NotificationRegistrar registrar_;

  // Login window which shows the view.
  views::Widget* login_window_;

  // Window that contains status area.
  // TODO(nkostylev): Temporary solution till we have
  // RenderWidgetHostViewViews working.
  views::Widget* status_window_;

  // Converts keyboard events on the TabContents to accelerators.
  UnhandledKeyboardEventHandler unhandled_keyboard_event_handler_;

  // Maps installed accelerators to OOBE webui accelerator identifiers.
  AccelMap accel_map_;

  // Watches webui_login_'s TabContents rendering.
  scoped_ptr<TabRenderWatcher> tab_watcher_;

  // Whether the host window is frozen.
  bool host_window_frozen_;

  // Caches StatusArea visibility setting before it has been initialized.
  bool status_area_visibility_on_init_;

  // Has the login page told us that it's ready?  This is triggered by either
  // all of the user images or the GAIA prompt being loaded, whichever comes
  // first.
  bool login_page_is_loaded_;

  // Should we emit the login-prompt-visible signal when the login page is
  // displayed?
  bool should_emit_login_prompt_visible_;

  DISALLOW_COPY_AND_ASSIGN(WebUILoginView);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_WEBUI_LOGIN_VIEW_H_
