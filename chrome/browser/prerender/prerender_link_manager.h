// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PRERENDER_PRERENDER_LINK_MANAGER_H_
#define CHROME_BROWSER_PRERENDER_PRERENDER_LINK_MANAGER_H_
#pragma once

#include <map>

#include "content/public/browser/render_view_host_observer.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebReferrerPolicy.h"

namespace content {
class RenderViewHost;
}

namespace IPC {
class Message;
}

namespace prerender {

class PrerenderManager;

// Launch and cancel prerenders based on the LinkPrerender element events.
class PrerenderLinkManager : public content::RenderViewHostObserver {
 public:
  PrerenderLinkManager(content::RenderViewHost* render_view_host);
  virtual ~PrerenderLinkManager();

  // content::RenderViewHostObserver overrides
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;

 private:
  typedef std::map<int, GURL> LinkIdToUrlMap;
  typedef std::multimap<GURL, int> UrlToLinkIdMap;

  void OnNewLinkPrerender(
      const IPC::Message& message,
      const GURL& url,
      const GURL& referrer,
      WebKit::WebReferrerPolicy policy);
  void OnRemovedLinkPrerender(const IPC::Message& message, int id);
  void OnUnloadedLinkPrerender(const IPC::Message& message, int id);

  Profile* profile_;
  LinkIdToUrlMap id_map_;
  UrlToLinkIdMap url_map_;

  DISALLOW_COPY_AND_ASSIGN(PrerenderLinkManager);
};

}  // namespace prerender

#endif // CHROME_BROWSER_PRERENDER_PRERENDER_LINK_MANAGER_H_
