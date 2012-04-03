// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/tab_contents/tab_contents_view_win.h"

#include "base/bind.h"
#include "content/browser/renderer_host/render_view_host_factory.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_view_win.h"
#include "content/browser/tab_contents/interstitial_page_impl.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/browser/tab_contents/web_contents_drag_win.h"
#include "content/browser/tab_contents/web_drag_dest_win.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_view_delegate.h"
#include "ui/gfx/screen.h"

using content::RenderViewHost;
using content::RenderWidgetHostView;
using content::WebContents;
using content::WebContentsViewDelegate;

namespace {

// We need to have a parent window for the compositing code to work correctly.
//
// A tab will not have a parent HWND whenever it is not active in its
// host window - for example at creation time and when it's in the
// background, so we provide a default widget to host them.
//
// It may be tempting to use GetDesktopWindow() instead, but this is
// problematic as the shell sends messages to children of the desktop
// window that interact poorly with us.
//
// See: http://crbug.com/16476
class TempParent : public ui::WindowImpl {
 public:
  static TempParent* Get() {
    static TempParent* g_temp_parent;
    if (!g_temp_parent) {
      g_temp_parent = new TempParent();

      g_temp_parent->set_window_style(WS_POPUP);
      g_temp_parent->set_window_ex_style(WS_EX_TOOLWINDOW);
      g_temp_parent->Init(GetDesktopWindow(), gfx::Rect());
      EnableWindow(g_temp_parent->hwnd(), FALSE);
    }
    return g_temp_parent;
  }

 private:
  // Explicitly do nothing in Close. We do this as some external apps may get a
  // handle to this window and attempt to close it.
  void OnClose() {
  }

  BEGIN_MSG_MAP_EX(TabContentsViewWin)
    MSG_WM_CLOSE(OnClose)
  END_MSG_MAP()
};

}  // namespace namespace

TabContentsViewWin::TabContentsViewWin(TabContents* tab_contents,
                                       WebContentsViewDelegate* delegate)
    : tab_contents_(tab_contents),
      view_(NULL),
      delegate_(delegate),
      close_tab_after_drag_ends_(false) {
}

TabContentsViewWin::~TabContentsViewWin() {
  if (IsWindow(hwnd()))
    DestroyWindow(hwnd());
}

void TabContentsViewWin::CreateView(const gfx::Size& initial_size) {
  initial_size_ = initial_size;

  set_window_style(WS_VISIBLE | WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);

  Init(TempParent::Get()->hwnd(), gfx::Rect(initial_size_));

  // Remove the root view drop target so we can register our own.
  RevokeDragDrop(GetNativeView());
  drag_dest_ = new WebDragDest(hwnd(), tab_contents_);
  if (delegate_.get()) {
    content::WebDragDestDelegate* delegate = delegate_->GetDragDestDelegate();
    if (delegate)
      drag_dest_->set_delegate(delegate);
  }
}

RenderWidgetHostView* TabContentsViewWin::CreateViewForWidget(
    content::RenderWidgetHost* render_widget_host)  {
  if (render_widget_host->GetView()) {
    // During testing, the view will already be set up in most cases to the
    // test view, so we don't want to clobber it with a real one. To verify that
    // this actually is happening (and somebody isn't accidentally creating the
    // view twice), we check for the RVH Factory, which will be set when we're
    // making special ones (which go along with the special views).
    DCHECK(RenderViewHostFactory::has_factory());
    return render_widget_host->GetView();
  }

  view_ = static_cast<RenderWidgetHostViewWin*>(
      RenderWidgetHostView::CreateViewForWidget(render_widget_host));
  view_->CreateWnd(GetNativeView());
  view_->ShowWindow(SW_SHOW);
  view_->SetSize(initial_size_);
  return view_;
}

gfx::NativeView TabContentsViewWin::GetNativeView() const {
  return hwnd();
}

gfx::NativeView TabContentsViewWin::GetContentNativeView() const {
  RenderWidgetHostView* rwhv = tab_contents_->GetRenderWidgetHostView();
  return rwhv ? rwhv->GetNativeView() : NULL;
}

gfx::NativeWindow TabContentsViewWin::GetTopLevelNativeWindow() const {
  return GetParent(GetNativeView());
}

void TabContentsViewWin::GetContainerBounds(gfx::Rect *out) const {
  // Copied from NativeWidgetWin::GetClientAreaScreenBounds().
  RECT r;
  GetClientRect(hwnd(), &r);
  POINT point = { r.left, r.top };
  ClientToScreen(hwnd(), &point);
  *out = gfx::Rect(point.x, point.y, r.right - r.left, r.bottom - r.top);
}

void TabContentsViewWin::SetPageTitle(const string16& title) {
  // It's possible to get this after the hwnd has been destroyed.
  if (GetNativeView())
    ::SetWindowText(GetNativeView(), title.c_str());
}

void TabContentsViewWin::OnTabCrashed(base::TerminationStatus status,
                                      int error_code) {
  // TODO(avi): No other TCV implementation does anything in this callback. Can
  // this be moved elsewhere so that |OnTabCrashed| can be removed everywhere?
  view_ = NULL;
}

void TabContentsViewWin::SizeContents(const gfx::Size& size) {
  gfx::Rect bounds;
  GetContainerBounds(&bounds);
  if (bounds.size() != size) {
    SetWindowPos(hwnd(), NULL, 0, 0, size.width(), size.height(),
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE);
  } else {
    // Our size matches what we want but the renderers size may not match.
    // Pretend we were resized so that the renderers size is updated too.
    if (tab_contents_->GetInterstitialPage())
      tab_contents_->GetInterstitialPage()->SetSize(size);
    RenderWidgetHostView* rwhv = tab_contents_->GetRenderWidgetHostView();
    if (rwhv)
      rwhv->SetSize(size);
  }
}

void TabContentsViewWin::RenderViewCreated(RenderViewHost* host) {
}

void TabContentsViewWin::Focus() {
  if (tab_contents_->GetInterstitialPage()) {
    tab_contents_->GetInterstitialPage()->Focus();
    return;
  }

  if (delegate_.get() && delegate_->Focus())
    return;

  RenderWidgetHostView* rwhv = tab_contents_->GetRenderWidgetHostView();
  if (rwhv)
    rwhv->Focus();
}

void TabContentsViewWin::SetInitialFocus() {
  if (tab_contents_->FocusLocationBarByDefault())
    tab_contents_->SetFocusToLocationBar(false);
  else
    Focus();
}

void TabContentsViewWin::StoreFocus() {
  if (delegate_.get())
    delegate_->StoreFocus();
}

void TabContentsViewWin::RestoreFocus() {
  if (delegate_.get())
    delegate_->RestoreFocus();
}

bool TabContentsViewWin::IsDoingDrag() const {
  return drag_handler_.get() != NULL;
}

void TabContentsViewWin::CancelDragAndCloseTab() {
  DCHECK(IsDoingDrag());
  // We can't close the tab while we're in the drag and
  // |drag_handler_->CancelDrag()| is async.  Instead, set a flag to cancel
  // the drag and when the drag nested message loop ends, close the tab.
  drag_handler_->CancelDrag();
  close_tab_after_drag_ends_ = true;
}

bool TabContentsViewWin::IsEventTracking() const {
  return false;
}

void TabContentsViewWin::CloseTabAfterEventTracking() {
}

void TabContentsViewWin::GetViewBounds(gfx::Rect* out) const {
  RECT r;
  GetWindowRect(hwnd(), &r);
  *out = gfx::Rect(r);
}

void TabContentsViewWin::CreateNewWindow(
    int route_id,
    const ViewHostMsg_CreateWindow_Params& params) {
  tab_contents_view_helper_.CreateNewWindow(tab_contents_, route_id, params);
}

void TabContentsViewWin::CreateNewWidget(int route_id,
                                         WebKit::WebPopupType popup_type) {
  tab_contents_view_helper_.CreateNewWidget(tab_contents_,
                                            route_id,
                                            false,
                                            popup_type);
}

void TabContentsViewWin::CreateNewFullscreenWidget(int route_id) {
  tab_contents_view_helper_.CreateNewWidget(tab_contents_,
                                            route_id,
                                            true,
                                            WebKit::WebPopupTypeNone);
}

void TabContentsViewWin::ShowCreatedWindow(int route_id,
                                           WindowOpenDisposition disposition,
                                           const gfx::Rect& initial_pos,
                                           bool user_gesture) {
  tab_contents_view_helper_.ShowCreatedWindow(
      tab_contents_, route_id, disposition, initial_pos, user_gesture);
}

void TabContentsViewWin::ShowCreatedWidget(int route_id,
                                           const gfx::Rect& initial_pos) {
  tab_contents_view_helper_.ShowCreatedWidget(tab_contents_,
                                              route_id,
                                              false,
                                              initial_pos);
}

void TabContentsViewWin::ShowCreatedFullscreenWidget(int route_id) {
  tab_contents_view_helper_.ShowCreatedWidget(tab_contents_,
                                              route_id,
                                              true,
                                              gfx::Rect());
}

void TabContentsViewWin::ShowContextMenu(
    const content::ContextMenuParams& params) {
  // Allow WebContentsDelegates to handle the context menu operation first.
  if (tab_contents_->GetDelegate() &&
      tab_contents_->GetDelegate()->HandleContextMenu(params)) {
    return;
  }

  if (delegate_.get())
    delegate_->ShowContextMenu(params);
}

void TabContentsViewWin::ShowPopupMenu(const gfx::Rect& bounds,
                                       int item_height,
                                       double item_font_size,
                                       int selected_item,
                                       const std::vector<WebMenuItem>& items,
                                       bool right_aligned) {
  // External popup menus are only used on Mac.
  NOTIMPLEMENTED();
}

void TabContentsViewWin::StartDragging(const WebDropData& drop_data,
                                       WebKit::WebDragOperationsMask operations,
                                       const SkBitmap& image,
                                       const gfx::Point& image_offset) {
  drag_handler_ = new WebContentsDragWin(
      GetNativeView(),
      tab_contents_,
      drag_dest_,
      base::Bind(&TabContentsViewWin::EndDragging, base::Unretained(this)));
  drag_handler_->StartDragging(drop_data, operations, image, image_offset);
}

void TabContentsViewWin::UpdateDragCursor(WebKit::WebDragOperation operation) {
  drag_dest_->set_drag_cursor(operation);
}

void TabContentsViewWin::GotFocus() {
  if (tab_contents_->GetDelegate())
    tab_contents_->GetDelegate()->WebContentsFocused(tab_contents_);
}

void TabContentsViewWin::TakeFocus(bool reverse) {
  if (tab_contents_->GetDelegate() &&
      !tab_contents_->GetDelegate()->TakeFocus(reverse) &&
      delegate_.get()) {
    delegate_->TakeFocus(reverse);
  }
}

void TabContentsViewWin::EndDragging() {
  drag_handler_ = NULL;
  if (close_tab_after_drag_ends_) {
    close_tab_timer_.Start(FROM_HERE, base::TimeDelta::FromMilliseconds(0),
                           this, &TabContentsViewWin::CloseTab);
  }
  tab_contents_->SystemDragEnded();
}

void TabContentsViewWin::CloseTab() {
  RenderViewHost* rvh = tab_contents_->GetRenderViewHost();
  rvh->GetDelegate()->Close(rvh);
}

LRESULT TabContentsViewWin::OnDestroy(
    UINT message, WPARAM wparam, LPARAM lparam, BOOL& handled) {
  if (drag_dest_.get()) {
    RevokeDragDrop(GetNativeView());
    drag_dest_ = NULL;
  }
  return 0;
}

LRESULT TabContentsViewWin::OnWindowPosChanged(
    UINT message, WPARAM wparam, LPARAM lparam, BOOL& handled) {
  WINDOWPOS* window_pos = reinterpret_cast<WINDOWPOS*>(lparam);
  if (window_pos->flags & SWP_HIDEWINDOW) {
    tab_contents_->HideContents();
    return 0;
  }

  // The TabContents was shown by a means other than the user selecting a
  // Tab, e.g. the window was minimized then restored.
  if (window_pos->flags & SWP_SHOWWINDOW)
    tab_contents_->ShowContents();

  // Unless we were specifically told not to size, cause the renderer to be
  // sized to the new bounds, which forces a repaint. Not required for the
  // simple minimize-restore case described above, for example, since the
  // size hasn't changed.
  if (window_pos->flags & SWP_NOSIZE)
    return 0;

  gfx::Size size(window_pos->cx, window_pos->cy);
  if (tab_contents_->GetInterstitialPage())
    tab_contents_->GetInterstitialPage()->SetSize(size);
  RenderWidgetHostView* rwhv = tab_contents_->GetRenderWidgetHostView();
  if (rwhv)
    rwhv->SetSize(size);

  if (delegate_.get())
    delegate_->SizeChanged(size);

  return 0;
}

LRESULT TabContentsViewWin::OnMouseDown(
    UINT message, WPARAM wparam, LPARAM lparam, BOOL& handled) {
  // Make sure this TabContents is activated when it is clicked on.
  if (tab_contents_->GetDelegate())
    tab_contents_->GetDelegate()->ActivateContents(tab_contents_);
  return 0;
}

LRESULT TabContentsViewWin::OnMouseMove(
    UINT message, WPARAM wparam, LPARAM lparam, BOOL& handled) {
  // Let our delegate know that the mouse moved (useful for resetting status
  // bubble state).
  if (tab_contents_->GetDelegate()) {
    tab_contents_->GetDelegate()->ContentsMouseEvent(
        tab_contents_, gfx::Screen::GetCursorScreenPoint(), true);
  }
  return 0;
}

LRESULT TabContentsViewWin::OnReflectedMessage(
    UINT msg, WPARAM wparam, LPARAM lparam, BOOL& handled) {
  MSG* message = reinterpret_cast<MSG*>(lparam);
  switch (message->message) {
    case WM_MOUSEWHEEL:
      // This message is reflected from the view() to this window.
      if (GET_KEYSTATE_WPARAM(message->wParam) & MK_CONTROL) {
        tab_contents_->GetDelegate()->ContentsZoomChange(
            GET_WHEEL_DELTA_WPARAM(message->wParam) > 0);
        return 1;
      }
    break;
  }
  return 0;
}

LRESULT TabContentsViewWin::OnNCCalcSize(
    UINT message, WPARAM wparam, LPARAM lparam, BOOL& handled) {
  // Hack for ThinkPad mouse wheel driver. We have set the fake scroll bars
  // to receive scroll messages from ThinkPad touch-pad driver. Suppress
  // painting of scrollbars by returning 0 size for them.
  return 0;
}

LRESULT TabContentsViewWin::OnScroll(
    UINT message, WPARAM wparam, LPARAM lparam, BOOL& handled) {
  int scroll_type = LOWORD(wparam);
  short position = HIWORD(wparam);
  HWND scrollbar = reinterpret_cast<HWND>(lparam);
  // This window can receive scroll events as a result of the ThinkPad's
  // touch-pad scroll wheel emulation.
  // If ctrl is held, zoom the UI.  There are three issues with this:
  // 1) Should the event be eaten or forwarded to content?  We eat the event,
  //    which is like Firefox and unlike IE.
  // 2) Should wheel up zoom in or out?  We zoom in (increase font size), which
  //    is like IE and Google maps, but unlike Firefox.
  // 3) Should the mouse have to be over the content area?  We zoom as long as
  //    content has focus, although FF and IE require that the mouse is over
  //    content.  This is because all events get forwarded when content has
  //    focus.
  if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
    int distance = 0;
    switch (scroll_type) {
      case SB_LINEUP:
        distance = WHEEL_DELTA;
        break;
      case SB_LINEDOWN:
        distance = -WHEEL_DELTA;
        break;
        // TODO(joshia): Handle SB_PAGEUP, SB_PAGEDOWN, SB_THUMBPOSITION,
        // and SB_THUMBTRACK for completeness
      default:
        break;
    }

    tab_contents_->GetDelegate()->ContentsZoomChange(distance > 0);
    return 0;
  }

  // Reflect scroll message to the view() to give it a chance
  // to process scrolling.
  SendMessage(GetContentNativeView(), message, wparam, lparam);
  return 0;
}

LRESULT TabContentsViewWin::OnSize(
    UINT message, WPARAM wparam, LPARAM lparam, BOOL& handled) {
  // NOTE: Because we handle OnWindowPosChanged without calling DefWindowProc,
  // OnSize is NOT called on window resize. This handler is called only once
  // when the window is created.
  // Don't call base class OnSize to avoid useless layout for 0x0 size.
  // We will get OnWindowPosChanged later and layout root view in WasSized.

  // Hack for ThinkPad touch-pad driver.
  // Set fake scrollbars so that we can get scroll messages,
  SCROLLINFO si = {0};
  si.cbSize = sizeof(si);
  si.fMask = SIF_ALL;

  si.nMin = 1;
  si.nMax = 100;
  si.nPage = 10;
  si.nPos = 50;

  ::SetScrollInfo(hwnd(), SB_HORZ, &si, FALSE);
  ::SetScrollInfo(hwnd(), SB_VERT, &si, FALSE);

  return 1;
}
