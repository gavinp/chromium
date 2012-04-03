// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/cocoa/extensions/shell_window_cocoa.h"

#include "base/sys_string_conversions.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/ui/cocoa/browser_window_utils.h"
#include "chrome/browser/ui/cocoa/extensions/extension_view_mac.h"
#include "chrome/common/extensions/extension.h"

@implementation ShellWindowController

@synthesize shellWindow = shellWindow_;

- (void)windowWillClose:(NSNotification*)notification {
  if (shellWindow_)
    shellWindow_->WindowWillClose();
}

@end

ShellWindowCocoa::ShellWindowCocoa(ExtensionHost* host)
    : ShellWindow(host),
      attention_request_id_(0) {
  // TOOD(mihaip): Restore prior window dimensions and positions on relaunch.
  NSRect rect = NSZeroRect;
  rect.size.width = host_->extension()->launch_width();
  rect.size.height = host_->extension()->launch_height();
  NSUInteger styleMask = NSTitledWindowMask | NSClosableWindowMask |
                         NSMiniaturizableWindowMask | NSResizableWindowMask;
  scoped_nsobject<NSWindow> window(
      [[NSWindow alloc] initWithContentRect:rect
                                  styleMask:styleMask
                                    backing:NSBackingStoreBuffered
                                      defer:NO]);
  [window setTitle:base::SysUTF8ToNSString(host->extension()->name())];
  [window setContentMinSize:
      NSMakeSize(host_->extension()->launch_min_width(),
                 host_->extension()->launch_min_height())];

  NSView* view = host->view()->native_view();
  [view setFrame:rect];
  [view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [[window contentView] addSubview:view];

  window_controller_.reset(
      [[ShellWindowController alloc] initWithWindow:window.release()]);
  [[window_controller_ window] setDelegate:window_controller_];
  [window_controller_ setShellWindow:this];
  [window_controller_ showWindow:nil];
}

bool ShellWindowCocoa::IsActive() const {
  return [window() isKeyWindow];
}

bool ShellWindowCocoa::IsMaximized() const {
  return [window() isZoomed];
}

bool ShellWindowCocoa::IsMinimized() const {
  return [window() isMiniaturized];
}

gfx::Rect ShellWindowCocoa::GetRestoredBounds() const {
  // Flip coordinates based on the primary screen.
  NSScreen* screen = [[NSScreen screens] objectAtIndex:0];
  NSRect frame = [window() frame];
  gfx::Rect bounds(frame.origin.x, 0, frame.size.width, frame.size.height);
  bounds.set_y([screen frame].size.height - frame.origin.y - frame.size.height);
  return bounds;
}

gfx::Rect ShellWindowCocoa::GetBounds() const {
  return GetRestoredBounds();
}

void ShellWindowCocoa::Show() {
  [window() makeKeyAndOrderFront:window_controller_];
}

void ShellWindowCocoa::ShowInactive() {
  [window() orderFront:window_controller_];
}

void ShellWindowCocoa::Close() {
  [window() performClose:nil];
}

void ShellWindowCocoa::Activate() {
  [BrowserWindowUtils activateWindowForController:window_controller_];
}

void ShellWindowCocoa::Deactivate() {
  // TODO(jcivelli): http://crbug.com/51364 Implement me.
  NOTIMPLEMENTED();
}

void ShellWindowCocoa::Maximize() {
  // Zoom toggles so only call if not already maximized.
  if (!IsMaximized())
    [window() zoom:window_controller_];
}

void ShellWindowCocoa::Minimize() {
  [window() miniaturize:window_controller_];
}

void ShellWindowCocoa::Restore() {
  if (IsMaximized())
    [window() zoom:window_controller_];  // Toggles zoom mode.
  else if (IsMinimized())
    [window() deminiaturize:window_controller_];
}

void ShellWindowCocoa::SetBounds(const gfx::Rect& bounds) {
  // Enforce minimum bounds.
  gfx::Rect checked_bounds = bounds;

  NSSize min_size = [window() minSize];
  if (bounds.width() < min_size.width)
      checked_bounds.set_width(min_size.width);
  if (bounds.height() < min_size.height)
      checked_bounds.set_height(min_size.height);

  NSRect cocoa_bounds = NSMakeRect(checked_bounds.x(), 0,
                                   checked_bounds.width(),
                                   checked_bounds.height());
  // Flip coordinates based on the primary screen.
  NSScreen* screen = [[NSScreen screens] objectAtIndex:0];
  cocoa_bounds.origin.y =
      [screen frame].size.height - checked_bounds.height() - checked_bounds.y();

  [window() setFrame:cocoa_bounds display:YES];
}

void ShellWindowCocoa::SetDraggableRegion(SkRegion* region) {
  // TODO: implement
}

void ShellWindowCocoa::FlashFrame(bool flash) {
  if (flash) {
    attention_request_id_ = [NSApp requestUserAttention:NSInformationalRequest];
  } else {
    [NSApp cancelUserAttentionRequest:attention_request_id_];
    attention_request_id_ = 0;
  }
}

bool ShellWindowCocoa::IsAlwaysOnTop() const {
  return false;
}

void ShellWindowCocoa::WindowWillClose() {
  [window_controller_ setShellWindow:NULL];
  delete this;
}

ShellWindowCocoa::~ShellWindowCocoa() {
}

NSWindow* ShellWindowCocoa::window() const {
  return [window_controller_ window];
}

// static
ShellWindow* ShellWindow::CreateShellWindow(ExtensionHost* host) {
  return new ShellWindowCocoa(host);
}
