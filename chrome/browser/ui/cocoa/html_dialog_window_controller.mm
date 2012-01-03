// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/html_dialog_window_controller.h"

#include "base/logging.h"
#include "base/memory/scoped_nsobject.h"
#include "base/sys_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#import "chrome/browser/ui/browser_dialogs.h"
#import "chrome/browser/ui/cocoa/browser_command_executor.h"
#import "chrome/browser/ui/cocoa/chrome_event_processing_window.h"
#include "chrome/browser/ui/dialog_style.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/browser/ui/webui/html_dialog_tab_contents_delegate.h"
#include "chrome/browser/ui/webui/html_dialog_ui.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/public/browser/native_web_keyboard_event.h"
#include "ui/base/keycodes/keyboard_codes.h"
#include "ui/gfx/size.h"

// Thin bridge that routes notifications to
// HtmlDialogWindowController's member variables.
class HtmlDialogWindowDelegateBridge : public HtmlDialogUIDelegate,
                                       public HtmlDialogTabContentsDelegate {
public:
  // All parameters must be non-NULL/non-nil.
  HtmlDialogWindowDelegateBridge(HtmlDialogWindowController* controller,
                                 Profile* profile,
                                 HtmlDialogUIDelegate* delegate);

  virtual ~HtmlDialogWindowDelegateBridge();

  // Called when the window is directly closed, e.g. from the close
  // button or from an accelerator.
  void WindowControllerClosed();

  // HtmlDialogUIDelegate declarations.
  virtual bool IsDialogModal() const OVERRIDE;
  virtual string16 GetDialogTitle() const OVERRIDE;
  virtual GURL GetDialogContentURL() const OVERRIDE;
  virtual void GetWebUIMessageHandlers(
      std::vector<WebUIMessageHandler*>* handlers) const OVERRIDE;
  virtual void GetDialogSize(gfx::Size* size) const OVERRIDE;
  virtual std::string GetDialogArgs() const OVERRIDE;
  virtual void OnDialogClosed(const std::string& json_retval) OVERRIDE;
  virtual void OnCloseContents(TabContents* source, bool* out_close_dialog)
      OVERRIDE;
  virtual void CloseContents(TabContents* source) OVERRIDE;
  virtual bool ShouldShowDialogTitle() const OVERRIDE { return true; }

  // HtmlDialogTabContentsDelegate declarations.
  virtual void MoveContents(TabContents* source, const gfx::Rect& pos);
  virtual void HandleKeyboardEvent(const NativeWebKeyboardEvent& event);

private:
  HtmlDialogWindowController* controller_;  // weak
  HtmlDialogUIDelegate* delegate_;  // weak, owned by controller_

  // Calls delegate_'s OnDialogClosed() exactly once, nulling it out
  // afterwards so that no other HtmlDialogUIDelegate calls are sent
  // to it.  Returns whether or not the OnDialogClosed() was actually
  // called on the delegate.
  bool DelegateOnDialogClosed(const std::string& json_retval);

  DISALLOW_COPY_AND_ASSIGN(HtmlDialogWindowDelegateBridge);
};

// ChromeEventProcessingWindow expects its controller to implement the
// BrowserCommandExecutor protocol.
@interface HtmlDialogWindowController (InternalAPI) <BrowserCommandExecutor>

// BrowserCommandExecutor methods.
- (void)executeCommand:(int)command;

@end

namespace browser {

gfx::NativeWindow ShowHtmlDialog(gfx::NativeWindow parent,
                                 Profile* profile,
                                 HtmlDialogUIDelegate* delegate,
                                 DialogStyle style) {
  // It's not always safe to display an html dialog with an off the record
  // profile.  If the last browser with that profile is closed it will go
  // away.
  // On most platforms we insist on the dialog being modal if we're off the
  // record to prevent that.  That wont work on the Mac since we don't have
  // modal dialogs.
  // Fall back to the old (incorrect) behavior of grabbing the original
  // profile.
  // NOTE: Use the parent parameter once we implement modal dialogs.
  return [HtmlDialogWindowController showHtmlDialog:delegate
      profile:profile->GetOriginalProfile()];
}

}  // namespace html_dialog_window_controller

HtmlDialogWindowDelegateBridge::HtmlDialogWindowDelegateBridge(
    HtmlDialogWindowController* controller, Profile* profile,
    HtmlDialogUIDelegate* delegate)
    : HtmlDialogTabContentsDelegate(profile),
      controller_(controller), delegate_(delegate) {
  DCHECK(controller_);
  DCHECK(delegate_);
}

HtmlDialogWindowDelegateBridge::~HtmlDialogWindowDelegateBridge() {}

void HtmlDialogWindowDelegateBridge::WindowControllerClosed() {
  Detach();
  controller_ = nil;
  DelegateOnDialogClosed("");
}

bool HtmlDialogWindowDelegateBridge::DelegateOnDialogClosed(
    const std::string& json_retval) {
  if (delegate_) {
    HtmlDialogUIDelegate* real_delegate = delegate_;
    delegate_ = NULL;
    real_delegate->OnDialogClosed(json_retval);
    return true;
  }
  return false;
}

// HtmlDialogUIDelegate definitions.

// All of these functions check for NULL first since delegate_ is set
// to NULL when the window is closed.

bool HtmlDialogWindowDelegateBridge::IsDialogModal() const {
  // TODO(akalin): Support modal dialog boxes.
  if (delegate_ && delegate_->IsDialogModal()) {
    LOG(WARNING) << "Modal HTML dialogs are not supported yet";
  }
  return false;
}

string16 HtmlDialogWindowDelegateBridge::GetDialogTitle() const {
  return delegate_ ? delegate_->GetDialogTitle() : string16();
}

GURL HtmlDialogWindowDelegateBridge::GetDialogContentURL() const {
  return delegate_ ? delegate_->GetDialogContentURL() : GURL();
}

void HtmlDialogWindowDelegateBridge::GetWebUIMessageHandlers(
    std::vector<WebUIMessageHandler*>* handlers) const {
  if (delegate_) {
    delegate_->GetWebUIMessageHandlers(handlers);
  } else {
    // TODO(akalin): Add this clause in the windows version.  Also
    // make sure that everything expects handlers to be non-NULL and
    // document it.
    handlers->clear();
  }
}

void HtmlDialogWindowDelegateBridge::GetDialogSize(gfx::Size* size) const {
  if (delegate_) {
    delegate_->GetDialogSize(size);
  } else {
    *size = gfx::Size();
  }
}

std::string HtmlDialogWindowDelegateBridge::GetDialogArgs() const {
  return delegate_ ? delegate_->GetDialogArgs() : "";
}

void HtmlDialogWindowDelegateBridge::OnDialogClosed(
    const std::string& json_retval) {
  Detach();
  // [controller_ close] should be called at most once, too.
  if (DelegateOnDialogClosed(json_retval)) {
    [controller_ close];
  }
  controller_ = nil;
}

void HtmlDialogWindowDelegateBridge::OnCloseContents(TabContents* source,
                                                     bool* out_close_dialog) {
  if (out_close_dialog)
    *out_close_dialog = true;
}

void HtmlDialogWindowDelegateBridge::CloseContents(TabContents* source) {
  bool close_dialog = false;
  OnCloseContents(source, &close_dialog);
  if (close_dialog)
    OnDialogClosed(std::string());
}

void HtmlDialogWindowDelegateBridge::MoveContents(TabContents* source,
                                                  const gfx::Rect& pos) {
  // TODO(akalin): Actually set the window bounds.
}

// A simplified version of BrowserWindowCocoa::HandleKeyboardEvent().
// We don't handle global keyboard shortcuts here, but that's fine since
// they're all browser-specific. (This may change in the future.)
void HtmlDialogWindowDelegateBridge::HandleKeyboardEvent(
    const NativeWebKeyboardEvent& event) {
  if (event.skip_in_browser || event.type == NativeWebKeyboardEvent::Char)
    return;

  // Close ourselves if the user hits Esc or Command-. .  The normal
  // way to do this is to implement (void)cancel:(int)sender, but
  // since we handle keyboard events ourselves we can't do that.
  //
  // According to experiments, hitting Esc works regardless of the
  // presence of other modifiers (as long as it's not an app-level
  // shortcut, e.g. Commmand-Esc for Front Row) but no other modifiers
  // can be present for Command-. to work.
  //
  // TODO(thakis): It would be nice to get cancel: to work somehow.
  // Bug: http://code.google.com/p/chromium/issues/detail?id=32828 .
  if (event.type == NativeWebKeyboardEvent::RawKeyDown &&
      ((event.windowsKeyCode == ui::VKEY_ESCAPE) ||
       (event.windowsKeyCode == ui::VKEY_OEM_PERIOD &&
        event.modifiers == NativeWebKeyboardEvent::MetaKey))) {
    [controller_ close];
    return;
  }

  ChromeEventProcessingWindow* event_window =
      static_cast<ChromeEventProcessingWindow*>([controller_ window]);
  DCHECK([event_window isKindOfClass:[ChromeEventProcessingWindow class]]);
  [event_window redispatchKeyEvent:event.os_event];
}

@implementation HtmlDialogWindowController (InternalAPI)

// This gets called whenever a chrome-specific keyboard shortcut is performed
// in the HTML dialog window.  We simply swallow all those events.
- (void)executeCommand:(int)command {}

@end

@implementation HtmlDialogWindowController

// NOTE(akalin): We'll probably have to add the parentWindow parameter back
// in once we implement modal dialogs.

+ (NSWindow*)showHtmlDialog:(HtmlDialogUIDelegate*)delegate
                    profile:(Profile*)profile {
  HtmlDialogWindowController* htmlDialogWindowController =
    [[HtmlDialogWindowController alloc] initWithDelegate:delegate
                                                 profile:profile];
  [htmlDialogWindowController loadDialogContents];
  [htmlDialogWindowController showWindow:nil];
  return [htmlDialogWindowController window];
}

- (id)initWithDelegate:(HtmlDialogUIDelegate*)delegate
               profile:(Profile*)profile {
  DCHECK(delegate);
  DCHECK(profile);

  gfx::Size dialogSize;
  delegate->GetDialogSize(&dialogSize);
  NSRect dialogRect = NSMakeRect(0, 0, dialogSize.width(), dialogSize.height());
  NSUInteger style = NSTitledWindowMask | NSClosableWindowMask |
      NSResizableWindowMask;
  scoped_nsobject<ChromeEventProcessingWindow> window(
      [[ChromeEventProcessingWindow alloc]
           initWithContentRect:dialogRect
                     styleMask:style
                       backing:NSBackingStoreBuffered
                         defer:YES]);
  if (!window.get()) {
    return nil;
  }
  self = [super initWithWindow:window];
  if (!self) {
    return nil;
  }
  [window setWindowController:self];
  [window setDelegate:self];
  [window setTitle:base::SysUTF16ToNSString(delegate->GetDialogTitle())];
  [window setMinSize:dialogRect.size];
  [window center];
  delegate_.reset(new HtmlDialogWindowDelegateBridge(self, profile, delegate));
  return self;
}

- (void)loadDialogContents {
  contentsWrapper_.reset(new TabContentsWrapper(new TabContents(
      delegate_->profile(), NULL, MSG_ROUTING_NONE, NULL, NULL)));
  [[self window]
      setContentView:contentsWrapper_->tab_contents()->GetNativeView()];
  contentsWrapper_->tab_contents()->set_delegate(delegate_.get());

  // This must be done before loading the page; see the comments in
  // HtmlDialogUI.
  HtmlDialogUI::GetPropertyAccessor().SetProperty(
      contentsWrapper_->tab_contents()->property_bag(), delegate_.get());

  contentsWrapper_->tab_contents()->controller().LoadURL(
                                      delegate_->GetDialogContentURL(),
                                      content::Referrer(),
                                      content::PAGE_TRANSITION_START_PAGE,
                                      std::string());

  // TODO(akalin): add accelerator for ESC to close the dialog box.
  //
  // TODO(akalin): Figure out why implementing (void)cancel:(id)sender
  // to do the above doesn't work.
}

- (void)windowWillClose:(NSNotification*)notification {
  delegate_->WindowControllerClosed();
  [self autorelease];
}

@end
