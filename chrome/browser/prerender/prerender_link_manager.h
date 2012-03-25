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
class Referrer;
}

namespace ui {
class Size;
}

namespace prerender {

class PrerenderManager;

// Launch and cancel prerenders based on the LinkPrerender element events.
class PrerenderLinkManager {
 public:
  PrerenderLinkManager(PrerenderManager* manager);
  virtual ~PrerenderLinkManager();

  void OnNewLinkPrerender(
      int prerender_id,
      int child_id,
      const GURL& url,
      const content::Referrer& referrer,
      WebKit::WebReferrerPolicy policy,
      const gfx::Size& size);
  void OnRemovedLinkPrerender(int prerender_id);
  void OnUnloadedLinkPrerender(int prerender_id);

 private:
  typedef std::map<int, GURL> LinkIdToUrlMap;
  typedef std::multimap<GURL, int> UrlToLinkIdMap;

  PrerenderManager* manager_;
  LinkIdToUrlMap id_map_;
  UrlToLinkIdMap url_map_;

  DISALLOW_COPY_AND_ASSIGN(PrerenderLinkManager);
};

}  // namespace prerender

#endif // CHROME_BROWSER_PRERENDER_PRERENDER_LINK_MANAGER_H_
