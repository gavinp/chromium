// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/web_app.h"

#include "base/environment.h"
#include "base/logging.h"
#include "content/public/browser/browser_thread.h"

namespace web_app {
namespace internals {

void CreateShortcutTask(const FilePath& web_app_path,
                        const FilePath& profile_path,
                        const ShellIntegration::ShortcutInfo& shortcut_info) {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::FILE));

  scoped_ptr<base::Environment> env(base::Environment::Create());

  std::string shortcut_template;
  if (!ShellIntegration::GetDesktopShortcutTemplate(env.get(),
                                                    &shortcut_template)) {
    return;
  }
  ShellIntegration::CreateDesktopShortcut(shortcut_info, shortcut_template);
}

}  // namespace internals
}  // namespace web_app
