// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/shell_browser_main.h"

#include "base/command_line.h"
#include "base/memory/scoped_ptr.h"
#include "content/public/browser/browser_main_runner.h"
#include "content/shell/shell.h"
#include "content/shell/shell_browser_context.h"
#include "content/shell/shell_content_browser_client.h"
#include "content/shell/shell_switches.h"
#include "webkit/support/webkit_support.h"

namespace {

GURL GetURLForLayoutTest(const char* test_name) {
  std::string path_or_url = test_name;
  std::string pixel_hash;
  std::string timeout;
  std::string::size_type separator_position = path_or_url.find(' ');
  if (separator_position != std::string::npos) {
    timeout = path_or_url.substr(separator_position + 1);
    path_or_url.erase(separator_position);
    separator_position = path_or_url.find(' ');
    if (separator_position != std::string::npos) {
      pixel_hash = timeout.substr(separator_position + 1);
      timeout.erase(separator_position);
    }
  }
  // TODO(jochen): use pixel_hash and timeout.
  GURL test_url = webkit_support::CreateURLForPathOrURL(path_or_url);
  webkit_support::SetCurrentDirectoryForFileURL(test_url);
  return test_url;
}

}  // namespace

// Main routine for running as the Browser process.
int ShellBrowserMain(const content::MainFunctionParams& parameters) {
  scoped_ptr<content::BrowserMainRunner> main_runner_(
      content::BrowserMainRunner::Create());

  int exit_code = main_runner_->Initialize(parameters);
  if (exit_code >= 0)
    return exit_code;

  bool layout_test_mode =
      CommandLine::ForCurrentProcess()->HasSwitch(switches::kDumpRenderTree);

  if (layout_test_mode) {
    char test_string[2048];
    content::ShellBrowserContext* browser_context =
        static_cast<content::ShellContentBrowserClient*>(
            content::GetContentClient()->browser())->browser_context();

    while (fgets(test_string, sizeof(test_string), stdin)) {
      char *new_line_position = strchr(test_string, '\n');
      if (new_line_position)
        *new_line_position = '\0';
      if (test_string[0] == '\0')
        continue;
      if (!strcmp(test_string, "QUIT"))
        break;
      content::Shell::CreateNewWindow(browser_context,
                                      GetURLForLayoutTest(test_string),
                                      NULL,
                                      MSG_ROUTING_NONE,
                                      NULL);
      main_runner_->Run();
      // TODO(jochen): Figure out a way to close shell.
    }
    exit_code = 0;
  } else {
    exit_code = main_runner_->Run();
  }

  main_runner_->Shutdown();

  return exit_code;
}
