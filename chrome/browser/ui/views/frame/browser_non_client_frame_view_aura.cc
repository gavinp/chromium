// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/browser_non_client_frame_view_aura.h"

#include "ash/wm/frame_painter.h"
#include "ash/wm/workspace/frame_maximize_button.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/ui/views/avatar_menu_button.h"
#include "chrome/browser/ui/views/frame/browser_frame.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/web_contents.h"
#include "grit/generated_resources.h"  // Accessibility names
#include "grit/theme_resources.h"
#include "grit/theme_resources_standard.h"
#include "grit/ui_resources.h"
#include "ui/aura/window.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/base/accessibility/accessible_view_state.h"
#include "ui/base/hit_test.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/theme_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace {

// The avatar ends 2 px above the bottom of the tabstrip (which, given the
// way the tabstrip draws its bottom edge, will appear like a 1 px gap to the
// user).
const int kAvatarBottomSpacing = 2;
// There are 2 px on each side of the avatar (between the frame border and
// it on the left, and between it and the tabstrip on the right).
const int kAvatarSideSpacing = 2;
// Space between left edge of window and tabstrip.
const int kTabstripLeftSpacing = 0;
// Space between right edge of tabstrip and maximize button.
const int kTabstripRightSpacing = 10;
// Space between top of window and top of tabstrip for restored windows.
const int kTabstripTopSpacingRestored = 7;
// Space between top of window and top of tabstrip for maximized windows.
// Place them flush to the top to make them clickable when the cursor is at
// the screen edge.
const int kTabstripTopSpacingMaximized = 0;
// Height of the shadow in the tab image, used to ensure clicks in the shadow
// area still drag restored windows.  This keeps the clickable area large enough
// to hit easily.
const int kTabShadowHeight = 4;
// Height of the shadow of the content area, at the top of the toolbar.
const int kContentShadowHeight = 1;

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// BrowserNonClientFrameViewAura, public:

BrowserNonClientFrameViewAura::BrowserNonClientFrameViewAura(
    BrowserFrame* frame, BrowserView* browser_view)
    : BrowserNonClientFrameView(frame, browser_view),
      size_button_(NULL),
      close_button_(NULL),
      window_icon_(NULL),
      frame_painter_(new ash::FramePainter),
      size_button_minimizes_(false) {
}

BrowserNonClientFrameViewAura::~BrowserNonClientFrameViewAura() {
}

void BrowserNonClientFrameViewAura::Init() {
  // Panels only minimize.
  ash::FramePainter::SizeButtonBehavior size_button_behavior;
  if (browser_view()->browser()->is_type_panel() &&
      browser_view()->browser()->app_type() == Browser::APP_TYPE_CHILD) {
    size_button_minimizes_ = true;
    size_button_ = new views::ImageButton(this);
    size_button_behavior = ash::FramePainter::SIZE_BUTTON_MINIMIZES;
  } else {
    size_button_ = new ash::FrameMaximizeButton(this, this);
    size_button_behavior = ash::FramePainter::SIZE_BUTTON_MAXIMIZES;
  }
  size_button_->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_ACCNAME_MAXIMIZE));
  AddChildView(size_button_);
  close_button_ = new views::ImageButton(this);
  close_button_->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_ACCNAME_CLOSE));
  AddChildView(close_button_);

  // Initializing the TabIconView is expensive, so only do it if we need to.
  if (browser_view()->ShouldShowWindowIcon()) {
    window_icon_ = new TabIconView(this);
    window_icon_->set_is_light(true);
    AddChildView(window_icon_);
    window_icon_->Update();
  }

  // Create incognito icon if necessary.
  UpdateAvatarInfo();

  // Frame painter handles layout of these buttons.
  frame_painter_->Init(frame(), window_icon_, size_button_, close_button_,
                       size_button_behavior);
}

///////////////////////////////////////////////////////////////////////////////
// BrowserNonClientFrameView overrides:

gfx::Rect BrowserNonClientFrameViewAura::GetBoundsForTabStrip(
    views::View* tabstrip) const {
  if (!tabstrip)
    return gfx::Rect();
  int tabstrip_x =
      avatar_button() ?
      (avatar_button()->bounds().right() + kAvatarSideSpacing) :
      kTabstripLeftSpacing;
  int tabstrip_width =
      size_button_->x() - kTabstripRightSpacing - tabstrip_x;
  return gfx::Rect(tabstrip_x,
                   GetHorizontalTabStripVerticalOffset(false),
                   std::max(0, tabstrip_width),
                   tabstrip->GetPreferredSize().height());
}

int BrowserNonClientFrameViewAura::GetHorizontalTabStripVerticalOffset(
    bool force_restored) const {
  return NonClientTopBorderHeight(force_restored);
}

void BrowserNonClientFrameViewAura::UpdateThrobber(bool running) {
  if (window_icon_)
    window_icon_->Update();
}

///////////////////////////////////////////////////////////////////////////////
// views::NonClientFrameView overrides:

gfx::Rect BrowserNonClientFrameViewAura::GetBoundsForClientView() const {
  int top_height = NonClientTopBorderHeight(false);
  return frame_painter_->GetBoundsForClientView(top_height, bounds());
}

gfx::Rect BrowserNonClientFrameViewAura::GetWindowBoundsForClientBounds(
    const gfx::Rect& client_bounds) const {
  int top_height = NonClientTopBorderHeight(false);
  return frame_painter_->GetWindowBoundsForClientBounds(top_height,
                                                        client_bounds);
}

int BrowserNonClientFrameViewAura::NonClientHitTest(const gfx::Point& point) {
  int hit_test = frame_painter_->NonClientHitTest(this, point);
  // When the window is restored we want a large click target above the tabs
  // to drag the window, so redirect clicks in the tab's shadow to caption.
  if (hit_test == HTCLIENT && !frame()->IsMaximized()) {
    // Convert point to client coordinates.
    gfx::Point client_point(point);
    View::ConvertPointToView(this, frame()->client_view(), &client_point);
    // Report hits in shadow at top of tabstrip as caption.
    gfx::Rect tabstrip_bounds(browser_view()->tabstrip()->bounds());
    if (client_point.y() < tabstrip_bounds.y() + kTabShadowHeight)
      hit_test = HTCAPTION;
  }
  return hit_test;
}

void BrowserNonClientFrameViewAura::GetWindowMask(const gfx::Size& size,
                                                  gfx::Path* window_mask) {
  // Aura does not use window masks.
}

void BrowserNonClientFrameViewAura::ResetWindowControls() {
  size_button_->SetState(views::CustomButton::BS_NORMAL);
  // The close button isn't affected by this constraint.
}

void BrowserNonClientFrameViewAura::UpdateWindowIcon() {
  if (window_icon_)
    window_icon_->SchedulePaint();
}

///////////////////////////////////////////////////////////////////////////////
// views::View overrides:

void BrowserNonClientFrameViewAura::OnPaint(gfx::Canvas* canvas) {
  if (frame()->IsFullscreen())
    return;  // Nothing visible, don't paint.
  // The primary header image changes based on window activation state and
  // theme, so we look it up for each paint.
  frame_painter_->PaintHeader(
      this,
      canvas,
      ShouldPaintAsActive() ?
          ash::FramePainter::ACTIVE : ash::FramePainter::INACTIVE,
      GetThemeFrameBitmap(),
      GetThemeFrameOverlayBitmap());
  if (browser_view()->ShouldShowWindowTitle())
    frame_painter_->PaintTitleBar(this, canvas, BrowserFrame::GetTitleFont());
  if (browser_view()->IsToolbarVisible())
    PaintToolbarBackground(canvas);
  else
    PaintContentEdge(canvas);
}

void BrowserNonClientFrameViewAura::Layout() {
  // Maximized windows and app/popup windows use shorter buttons.
  bool maximized_layout =
      frame()->IsMaximized() || !browser_view()->IsBrowserTypeNormal();
  frame_painter_->LayoutHeader(this, maximized_layout);
  if (avatar_button())
    LayoutAvatar();
  BrowserNonClientFrameView::Layout();
}

bool BrowserNonClientFrameViewAura::HitTest(const gfx::Point& l) const {
  // If the point is outside the bounds of the client area, claim it.
  if (NonClientFrameView::HitTest(l))
    return true;

  // Otherwise claim it only if it's in a non-tab portion of the tabstrip.
  if (!browser_view()->tabstrip())
    return false;
  gfx::Rect tabstrip_bounds(browser_view()->tabstrip()->bounds());
  gfx::Point tabstrip_origin(tabstrip_bounds.origin());
  View::ConvertPointToView(frame()->client_view(), this, &tabstrip_origin);
  tabstrip_bounds.set_origin(tabstrip_origin);
  if (l.y() > tabstrip_bounds.bottom())
    return false;

  // We convert from our parent's coordinates since we assume we fill its bounds
  // completely. We need to do this since we're not a parent of the tabstrip,
  // meaning ConvertPointToView would otherwise return something bogus.
  gfx::Point browser_view_point(l);
  View::ConvertPointToView(parent(), browser_view(), &browser_view_point);
  return browser_view()->IsPositionInWindowCaption(browser_view_point);
}

void BrowserNonClientFrameViewAura::GetAccessibleState(
    ui::AccessibleViewState* state) {
  state->role = ui::AccessibilityTypes::ROLE_TITLEBAR;
}

gfx::Size BrowserNonClientFrameViewAura::GetMinimumSize() {
  return frame_painter_->GetMinimumSize(this);
}

///////////////////////////////////////////////////////////////////////////////
// views::ButtonListener overrides:

void BrowserNonClientFrameViewAura::ButtonPressed(views::Button* sender,
                                                  const views::Event& event) {
  if (sender == size_button_) {
    // The maximize button may move out from under the cursor.
    ResetWindowControls();
    if (size_button_minimizes_)
      frame()->Minimize();
    else if (frame()->IsMaximized())
      frame()->Restore();
    else
      frame()->Maximize();
    // |this| may be deleted - some windows delete their frames on maximize.
  } else if (sender == close_button_) {
    frame()->Close();
  }
}

///////////////////////////////////////////////////////////////////////////////
// TabIconView::TabIconViewModel overrides:

bool BrowserNonClientFrameViewAura::ShouldTabIconViewAnimate() const {
  // This function is queried during the creation of the window as the
  // TabIconView we host is initialized, so we need to NULL check the selected
  // WebContents because in this condition there is not yet a selected tab.
  content::WebContents* current_tab = browser_view()->GetSelectedWebContents();
  return current_tab ? current_tab->IsLoading() : false;
}

SkBitmap BrowserNonClientFrameViewAura::GetFaviconForTabIconView() {
  views::WidgetDelegate* delegate = frame()->widget_delegate();
  if (!delegate)
    return SkBitmap();
  return delegate->GetWindowIcon();
}

///////////////////////////////////////////////////////////////////////////////
// BrowserNonClientFrameViewAura, private:


int BrowserNonClientFrameViewAura::NonClientTopBorderHeight(
    bool force_restored) const {
  if (force_restored)
    return kTabstripTopSpacingRestored;
  if (frame()->IsFullscreen())
    return 0;
  // Windows with tab strips need a smaller non-client area.
  if (browser_view()->IsTabStripVisible()) {
    if (frame()->IsMaximized())
      return kTabstripTopSpacingMaximized;
    return kTabstripTopSpacingRestored;
  }
  // For windows without a tab strip (popups, etc.) ensure we have enough space
  // to see the window caption buttons and the content separator line.
  return close_button_->bounds().bottom() + kClientEdgeThickness;
}

void BrowserNonClientFrameViewAura::LayoutAvatar() {
  DCHECK(avatar_button());
  SkBitmap incognito_icon = browser_view()->GetOTRAvatarIcon();

  int avatar_bottom = GetHorizontalTabStripVerticalOffset(false) +
      browser_view()->GetTabStripHeight() - kAvatarBottomSpacing;
  int avatar_restored_y = avatar_bottom - incognito_icon.height();
  int avatar_y = frame()->IsMaximized() ?
      NonClientTopBorderHeight(false) + kContentShadowHeight:
      avatar_restored_y;
  gfx::Rect avatar_bounds(kAvatarSideSpacing,
                          avatar_y,
                          incognito_icon.width(),
                          avatar_bottom - avatar_y);
  avatar_button()->SetBoundsRect(avatar_bounds);
}

void BrowserNonClientFrameViewAura::PaintToolbarBackground(
    gfx::Canvas* canvas) {
  gfx::Rect toolbar_bounds(browser_view()->GetToolbarBounds());
  if (toolbar_bounds.IsEmpty())
    return;
  gfx::Point toolbar_origin(toolbar_bounds.origin());
  ConvertPointToView(browser_view(), this, &toolbar_origin);
  toolbar_bounds.set_origin(toolbar_origin);

  int x = toolbar_bounds.x();
  int w = toolbar_bounds.width();
  int y = toolbar_bounds.y();
  int h = toolbar_bounds.height();

  // Gross hack: We split the toolbar images into two pieces, since sometimes
  // (popup mode) the toolbar isn't tall enough to show the whole image.  The
  // split happens between the top shadow section and the bottom gradient
  // section so that we never break the gradient.
  int split_point = kFrameShadowThickness * 2;
  int bottom_y = y + split_point;
  ui::ThemeProvider* tp = GetThemeProvider();
  int bottom_edge_height = h - split_point;

  canvas->FillRect(gfx::Rect(x, bottom_y, w, bottom_edge_height),
                   tp->GetColor(ThemeService::COLOR_TOOLBAR));

  // Paint the main toolbar image.  Since this image is also used to draw the
  // tab background, we must use the tab strip offset to compute the image
  // source y position.  If you have to debug this code use an image editor
  // to paint a diagonal line through the toolbar image and ensure it lines up
  // across the tab and toolbar.
  SkBitmap* theme_toolbar = tp->GetBitmapNamed(IDR_THEME_TOOLBAR);
  canvas->TileImageInt(
      *theme_toolbar,
      x, bottom_y - GetHorizontalTabStripVerticalOffset(false),
      x, bottom_y,
      w, theme_toolbar->height());

  // The content area line has a shadow that extends a couple of pixels above
  // the toolbar bounds.
  const int kContentShadowHeight = 2;
  SkBitmap* toolbar_top =
      tp->GetBitmapNamed(IDR_TOOLBAR_SHADE_TOP);
  canvas->TileImageInt(*toolbar_top,
                       0, 0,
                       x, y - kContentShadowHeight,
                       w, split_point + kContentShadowHeight + 1);

  // Draw the "lightening" shade line around the edges of the toolbar.
  SkBitmap* toolbar_left = tp->GetBitmapNamed(IDR_TOOLBAR_SHADE_LEFT);
  canvas->TileImageInt(*toolbar_left,
                       0, 0,
                       x + kClientEdgeThickness,
                       y + kClientEdgeThickness + kContentShadowHeight,
                       toolbar_left->width(), theme_toolbar->height());
  SkBitmap* toolbar_right = tp->GetBitmapNamed(IDR_TOOLBAR_SHADE_RIGHT);
  canvas->TileImageInt(*toolbar_right,
                       0, 0,
                       w - toolbar_right->width() - 2 * kClientEdgeThickness,
                       y + kClientEdgeThickness + kContentShadowHeight,
                       toolbar_right->width(), theme_toolbar->height());

  // Draw the content/toolbar separator.
  canvas->FillRect(gfx::Rect(x + kClientEdgeThickness,
                             toolbar_bounds.bottom() - kClientEdgeThickness,
                             w - (2 * kClientEdgeThickness),
                             kClientEdgeThickness),
      ThemeService::GetDefaultColor(ThemeService::COLOR_TOOLBAR_SEPARATOR));
}

void BrowserNonClientFrameViewAura::PaintContentEdge(gfx::Canvas* canvas) {
  canvas->FillRect(gfx::Rect(0, close_button_->bounds().bottom(),
                             width(), kClientEdgeThickness),
      ThemeService::GetDefaultColor(ThemeService::COLOR_TOOLBAR_SEPARATOR));
}

const SkBitmap* BrowserNonClientFrameViewAura::GetThemeFrameBitmap() const {
  bool is_incognito = browser_view()->IsOffTheRecord();
  int resource_id;
  if (browser_view()->IsBrowserTypeNormal()) {
    if (ShouldPaintAsActive()) {
      // Use the standard resource ids to allow users to theme the frames.
      // TODO(jamescook): If this becomes the only frame we use on Aura, define
      // the resources to use the standard ids like IDR_THEME_FRAME, etc.
      if (is_incognito) {
        return GetCustomBitmap(IDR_THEME_FRAME_INCOGNITO,
                               IDR_AURA_WINDOW_HEADER_BASE_INCOGNITO_ACTIVE);
      }
      return GetCustomBitmap(IDR_THEME_FRAME,
                             IDR_AURA_WINDOW_HEADER_BASE_ACTIVE);
    }
    if (is_incognito) {
      return GetCustomBitmap(IDR_THEME_FRAME_INCOGNITO_INACTIVE,
                             IDR_AURA_WINDOW_HEADER_BASE_INCOGNITO_INACTIVE);
    }
    return GetCustomBitmap(IDR_THEME_FRAME_INACTIVE,
                           IDR_AURA_WINDOW_HEADER_BASE_INACTIVE);
  }
  // Never theme app and popup windows.
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  if (ShouldPaintAsActive()) {
    resource_id = is_incognito ?
        IDR_AURA_WINDOW_HEADER_BASE_INCOGNITO_ACTIVE :
        IDR_AURA_WINDOW_HEADER_BASE_ACTIVE;
  } else {
    resource_id = is_incognito ?
        IDR_AURA_WINDOW_HEADER_BASE_INCOGNITO_INACTIVE :
        IDR_AURA_WINDOW_HEADER_BASE_INACTIVE;
  }
  return rb.GetBitmapNamed(resource_id);
}

const SkBitmap*
BrowserNonClientFrameViewAura::GetThemeFrameOverlayBitmap() const {
  ui::ThemeProvider* tp = GetThemeProvider();
  if (tp->HasCustomImage(IDR_THEME_FRAME_OVERLAY) &&
      browser_view()->IsBrowserTypeNormal() &&
      !browser_view()->IsOffTheRecord()) {
    return tp->GetBitmapNamed(ShouldPaintAsActive() ?
        IDR_THEME_FRAME_OVERLAY : IDR_THEME_FRAME_OVERLAY_INACTIVE);
  }
  return NULL;
}

SkBitmap* BrowserNonClientFrameViewAura::GetCustomBitmap(
    int bitmap_id,
    int fallback_bitmap_id) const {
  ui::ThemeProvider* tp = GetThemeProvider();
  if (tp->HasCustomImage(bitmap_id))
    return tp->GetBitmapNamed(bitmap_id);
  return tp->GetBitmapNamed(fallback_bitmap_id);
}
