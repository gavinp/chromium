// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/shell.h"

#include "base/command_line.h"
#include "base/message_loop.h"
#include "base/path_service.h"
#include "base/string_util.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/shell/shell_javascript_dialog_creator.h"
#include "content/shell/shell_messages.h"
#include "content/shell/shell_switches.h"
#include "ui/gfx/size.h"

// Content area size for newly created windows.
static const int kTestWindowWidth = 800;
static const int kTestWindowHeight = 600;

namespace content {

std::vector<Shell*> Shell::windows_;

Shell::Shell(WebContents* web_contents)
    : WebContentsObserver(web_contents),
      wait_until_done_(false),
      window_(NULL),
      url_edit_view_(NULL)
#if defined(OS_WIN)
      , default_edit_wnd_proc_(0)
#endif
  {
    windows_.push_back(this);
}

Shell::~Shell() {
  PlatformCleanUp();

  for (size_t i = 0; i < windows_.size(); ++i) {
    if (windows_[i] == this) {
      windows_.erase(windows_.begin() + i);
      break;
    }
  }
}

Shell* Shell::CreateShell(WebContents* web_contents) {
  Shell* shell = new Shell(web_contents);
  shell->PlatformCreateWindow(kTestWindowWidth, kTestWindowHeight);

  shell->web_contents_.reset(web_contents);
  web_contents->SetDelegate(shell);

  shell->PlatformSetContents();

  shell->PlatformResizeSubViews();
  return shell;
}

Shell* Shell::FromRenderViewHost(RenderViewHost* rvh) {
  for (size_t i = 0; i < windows_.size(); ++i) {
    if (windows_[i]->web_contents() &&
        windows_[i]->web_contents()->GetRenderViewHost() == rvh) {
      return windows_[i];
    }
  }
  return NULL;
}

Shell* Shell::CreateNewWindow(content::BrowserContext* browser_context,
                              const GURL& url,
                              SiteInstance* site_instance,
                              int routing_id,
                              WebContents* base_web_contents) {
  WebContents* web_contents = WebContents::Create(
      browser_context,
      site_instance,
      routing_id,
      base_web_contents,
      NULL);
  Shell* shell = CreateShell(web_contents);
  if (!url.is_empty())
    shell->LoadURL(url);
  return shell;
}

void Shell::LoadURL(const GURL& url) {
  web_contents_->GetController().LoadURL(
      url,
      content::Referrer(),
      content::PAGE_TRANSITION_TYPED,
      std::string());
  web_contents_->Focus();
}

void Shell::GoBackOrForward(int offset) {
  web_contents_->GetController().GoToOffset(offset);
  web_contents_->Focus();
}

void Shell::Reload() {
  web_contents_->GetController().Reload(false);
  web_contents_->Focus();
}

void Shell::Stop() {
  web_contents_->Stop();
  web_contents_->Focus();
}

void Shell::UpdateNavigationControls() {
  int current_index = web_contents_->GetController().GetCurrentEntryIndex();
  int max_index = web_contents_->GetController().GetEntryCount() - 1;

  PlatformEnableUIControl(BACK_BUTTON, current_index > 0);
  PlatformEnableUIControl(FORWARD_BUTTON, current_index < max_index);
  PlatformEnableUIControl(STOP_BUTTON, web_contents_->IsLoading());
}

gfx::NativeView Shell::GetContentView() {
  if (!web_contents_.get())
    return NULL;
  return web_contents_->GetNativeView();
}

void Shell::LoadingStateChanged(WebContents* source) {
  UpdateNavigationControls();
  PlatformSetIsLoading(source->IsLoading());
}

void Shell::WebContentsCreated(WebContents* source_contents,
                               int64 source_frame_id,
                               const GURL& target_url,
                               WebContents* new_contents) {
  CreateShell(new_contents);
}

void Shell::DidNavigateMainFramePostCommit(WebContents* tab) {
  PlatformSetAddressBarURL(tab->GetURL());
}

JavaScriptDialogCreator* Shell::GetJavaScriptDialogCreator() {
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kDumpRenderTree))
    return NULL;

  if (!dialog_creator_.get())
    dialog_creator_.reset(new ShellJavaScriptDialogCreator());
  return dialog_creator_.get();
}

void Shell::DidFinishLoad(int64 frame_id,
                          const GURL& validated_url,
                          bool is_main_frame) {
  if (!is_main_frame || wait_until_done_)
    return;
  if (!CommandLine::ForCurrentProcess()->HasSwitch(switches::kDumpRenderTree))
    return;
  RenderViewHost* render_view_host = web_contents_->GetRenderViewHost();
  render_view_host->Send(
      new ShellViewMsg_CaptureTextDump(render_view_host->GetRoutingID(),
                                       false));
}

}  // namespace content
