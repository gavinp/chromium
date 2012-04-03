// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_SHELL_BROWSER_CONTEXT_H_
#define CONTENT_SHELL_SHELL_BROWSER_CONTEXT_H_
#pragma once

#include "base/compiler_specific.h"
#include "base/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"
#include "content/public/browser/browser_context.h"

class DownloadManager;

namespace content {

class DownloadManagerDelegate;
class ResourceContext;
class ShellDownloadManagerDelegate;

class ShellBrowserContext : public BrowserContext {
 public:
  static ShellBrowserContext* GetInstance();

  // BrowserContext implementation.
  virtual FilePath GetPath() OVERRIDE;
  virtual bool IsOffTheRecord() const OVERRIDE;
  virtual DownloadManager* GetDownloadManager() OVERRIDE;
  virtual net::URLRequestContextGetter* GetRequestContext() OVERRIDE;
  virtual net::URLRequestContextGetter* GetRequestContextForRenderProcess(
      int renderer_child_id) OVERRIDE;
  virtual net::URLRequestContextGetter* GetRequestContextForMedia() OVERRIDE;
  virtual ResourceContext* GetResourceContext() OVERRIDE;
  virtual GeolocationPermissionContext*
      GetGeolocationPermissionContext() OVERRIDE;
  virtual SpeechRecognitionPreferences*
      GetSpeechRecognitionPreferences() OVERRIDE;
  virtual bool DidLastSessionExitCleanly() OVERRIDE;
  virtual quota::SpecialStoragePolicy* GetSpecialStoragePolicy() OVERRIDE;

 private:
  ShellBrowserContext();
  virtual ~ShellBrowserContext();
  friend struct DefaultSingletonTraits<ShellBrowserContext>;

  // Performs initialization of the ShellBrowserContext while IO is still
  // allowed on the current thread.
  void InitWhileIOAllowed();

  FilePath path_;
  scoped_ptr<ResourceContext> resource_context_;
  scoped_refptr<ShellDownloadManagerDelegate> download_manager_delegate_;
  scoped_refptr<DownloadManager> download_manager_;
  scoped_refptr<net::URLRequestContextGetter> url_request_getter_;
  scoped_refptr<GeolocationPermissionContext> geolocation_permission_context_;
  scoped_refptr<SpeechRecognitionPreferences> speech_recognition_preferences_;

  DISALLOW_COPY_AND_ASSIGN(ShellBrowserContext);
};

}  // namespace content

#endif  // CONTENT_SHELL_SHELL_BROWSER_CONTEXT_H_
