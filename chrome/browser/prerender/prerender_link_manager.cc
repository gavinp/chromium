// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/prerender/prerender_link_manager.h"

#include <utility>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/prerender/prerender_manager.h"
#include "chrome/browser/prerender/prerender_manager_factory.h"
#include "content/public/common/referrer.h"
#include "ui/gfx/size.h"

namespace prerender {

PrerenderLinkManager::PrerenderLinkManager(
    PrerenderManager* manager)
    : manager_(manager) {
}

PrerenderLinkManager::~PrerenderLinkManager() {
}

void PrerenderLinkManager::OnNewLinkPrerender(
    int prerender_id,
    int child_id,
    int render_view_route_id,
    const GURL& url,
    const content::Referrer& referrer,
    const gfx::Size& ALLOW_UNUSED size) {
  manager_->AddPrerenderFromLinkRelPrerender(
      child_id, render_view_route_id, url, referrer);
  DCHECK(id_map_.find(prerender_id) == id_map_.end());
  id_map_.insert(std::make_pair(prerender_id, url));
  url_map_.insert(std::make_pair(url, prerender_id));
}

void PrerenderLinkManager::OnRemovedLinkPrerender(const int prerender_id) {
  PrerenderIdToUrlMap::const_iterator id_url_iter = id_map_.find(prerender_id);
  if (id_url_iter == id_map_.end())
    return;
  
  const GURL& url = id_url_iter->second;

  // Are any other link elements prerendering this URL?
  UrlToPrerenderIdMap::iterator url_id_iter = url_map_.find(url);
  int remaining_prerender_link_count = 0;
  while (url_id_iter != url_map_.end() && url_id_iter->first == url) {
    if (url_id_iter->second == prerender_id) {
      UrlToPrerenderIdMap::iterator to_erase = url_id_iter;
      ++url_id_iter;
      url_map_.erase(to_erase);
      continue;
    }
    ++remaining_prerender_link_count;
    ++url_id_iter;
  }
  if (!remaining_prerender_link_count)
    return;

  // TODO(gavinp): Track down the correct prerender and stop it, rather than
  // this nuclear option.
  if (manager_->IsPrerendering(url))
    manager_->CancelAllPrerenders();
}
  
void PrerenderLinkManager::OnUnloadedLinkPrerender(
    int ALLOW_UNUSED prerender_id) {
  // TODO(gavinp,cbentzel): Implement reasonable behaviour for
  // navigation away from launcher.
}

}  // namespace prerender
