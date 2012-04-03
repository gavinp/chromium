// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PRERENDER_PRERENDER_LINK_MANAGER_H_
#define CHROME_BROWSER_PRERENDER_PRERENDER_LINK_MANAGER_H_
#pragma once

#include <map>
#include <utility>

#include "base/basictypes.h"
#include "googleurl/src/gurl.h"

class Profile;

namespace content {
class Referrer;
}

namespace gfx {
class Size;
}

namespace prerender {

class PrerenderManager;

// Launch and cancel prerenders based on the LinkPrerender element events.
class PrerenderLinkManager {
 public:
  explicit PrerenderLinkManager(PrerenderManager* manager);
  virtual ~PrerenderLinkManager();

  static void OnAddPrerender(
      Profile* profile,
      int prerender_id,
      int child_id,
      const GURL& url,
      const content::Referrer& referrer,
      const gfx::Size& size,
      int render_view_route_id);
  static void OnCancelPrerender(Profile* profile,
                                int prerender_id,
                                int child_id);
  static void OnAbandonPrerender(Profile* profile,
                                 int prerender_id,
                                 int child_id);

 private:
  typedef std::pair<int, int> ChildAndPrerenderIdPair;
  typedef std::map<ChildAndPrerenderIdPair, GURL> PrerenderIdToUrlMap;
  typedef std::multimap<GURL, ChildAndPrerenderIdPair> UrlToPrerenderIdMap;

  void OnAddPrerenderImpl(
      int prerender_id,
      int child_id,
      const GURL& url,
      const content::Referrer& referrer,
      const gfx::Size& size,
      int render_view_route_id);
  void OnCancelLinkPrerenderImpl(int prerender_id, int child_id);
  void OnAbandonLinkPrerenderImpl(int prerender_id, int child_id);

  PrerenderManager* manager_;
  PrerenderIdToUrlMap id_map_;
  UrlToPrerenderIdMap url_map_;

  DISALLOW_COPY_AND_ASSIGN(PrerenderLinkManager);
};

}  // namespace prerender

#endif  // CHROME_BROWSER_PRERENDER_PRERENDER_LINK_MANAGER_H_
