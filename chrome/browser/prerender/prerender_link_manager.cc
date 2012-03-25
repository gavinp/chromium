// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/prerender/prerender_link_manager.h"

#include <utility>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/prerender/prerender_manager.h"
#include "chrome/browser/prerender/prerender_manager_factory.h"
#include "content/common/referrer.h"

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
    const GURL& url,
    const content::Referrer& referrer,
    WebKit::WebReferrerPolicy policy,
    const gfx::Size& size) {
  prerender_manager->AddPrerenderFromLinkRelPrerender(
      child_id, route_id, url, content::Referrer(referrer, policy));
  DCHECK(id_map_.find(std::make_pair(id,url) == id_map_.end()));
  id_map_.insert(std::make_pair(prerender_id, url));
  url_map_.insert(std::make_pair(url, prerender_id));
}

void PrerenderLinkManager::OnRemovedLinkPrerender(const int prerender_id) {
  LinkIdToUrlMap::const_iterator id_url_iter = id_map_.find(prerender_id);
  if (id_url_iter == id_map_.end())
    return;
  
  const GURL& url = id_url_iter->second;

  // Are any other link elements prerendering this URL?
  UrlToLinkIdMap url_id_iter = url_map_.find(url);
  int remaining_prerender_link_count = 0;
  while (url_id_iter != url_map_.end() && url_id_iter->first == url) {
    if (url_id_iter->second == prerender_id) {
      UrlToLinkIdMap to_erase = url_id_iter;
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
  if (prerender_manager->IsPrerendering(url))
    prerender_manager->CancelAllPrerenders();
}
  
void PrerenderLinkManager::OnUnloadedLinkPrerender(
    int ALLOW_UNUSED prerender_id) {
  // TODO(gavinp,cbentzel): Implement reasonable behaviour for
  // navigation away from launcher.
}

}  // namespace prerender
