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

void PrerenderLinkManager::OnNewLinkPrerenderImpl(
    int prerender_id,
    int child_id,
    int render_view_route_id,
    const GURL& url,
    const content::Referrer& referrer,
    const gfx::Size& ALLOW_UNUSED size) {
  VLOG(1) << "OnNewLinkPrerenderImpl("
          << prerender_id << ", " << child_id << ", ..., "
          << url.spec() << ", ...)";
      
  manager_->AddPrerenderFromLinkRelPrerender(
      child_id, render_view_route_id, url, referrer);
  const ChildAndPrerenderIdPair child_and_prerender_id(child_id, prerender_id);
  DCHECK(id_map_.find(child_and_prerender_id) == id_map_.end());
  id_map_.insert(std::make_pair(child_and_prerender_id, url));
  url_map_.insert(std::make_pair(url, child_and_prerender_id));
}

void PrerenderLinkManager::OnRemovedLinkPrerenderImpl(const int prerender_id,
                                                      const int child_id) {
  
  VLOG(2) << "OnRemovedLinkPrerenderImpl(" 
          << prerender_id << ", " << child_id << ")";

  const ChildAndPrerenderIdPair child_and_prerender_id(child_id, prerender_id);

  PrerenderIdToUrlMap::iterator id_url_iter =
      id_map_.find(child_and_prerender_id);
  if (id_url_iter == id_map_.end())
    return;
  const GURL url = id_url_iter->second;
  id_map_.erase(id_url_iter);

  // Are any other link elements prerendering this URL?
  UrlToPrerenderIdMap::iterator url_id_iter = url_map_.find(url);
  int remaining_prerender_link_count = 0;
  while (url_id_iter != url_map_.end() && url_id_iter->first == url) {
    if (url_id_iter->second == child_and_prerender_id) {
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
  // this nuclear option, which assumes that only one prerender at a time
  // runs.
  if (manager_->IsPrerendering(url))
    manager_->CancelAllPrerenders();
}
  
void PrerenderLinkManager::OnUnloadedLinkPrerenderImpl(
    int ALLOW_UNUSED prerender_id,
    int ALLOW_UNUSED child_id) {
  VLOG(2) << "OnUnloadedLinkPrerenderImpl("
          << prerender_id << ", " << child_id << ")";
  // TODO(gavinp,cbentzel): Implement reasonable behaviour for
  // navigation away from launcher.
  const ChildAndPrerenderIdPair child_and_prerender_id(child_id, prerender_id);
  PrerenderIdToUrlMap::iterator id_url_iter =
      id_map_.find(child_and_prerender_id);
  if (id_url_iter == id_map_.end())
    return;
  const GURL& url = id_url_iter->second;
  DCHECK(url_map_.find(url) != url_map_.end());
  ignore_result(url_map_.erase(url));
  id_map_.erase(id_url_iter);
}

// static
void PrerenderLinkManager::OnNewLinkPrerender(
      Profile* profile,
      int prerender_id,
      int child_id,
      int render_view_route_id,
      const GURL& url,
      const content::Referrer& referrer,
      const gfx::Size& size) {
  if (prerender::PrerenderManager* prerender_manager =
      prerender::PrerenderManagerFactory::GetForProfile(profile)) {
    prerender_manager->link_manager()->OnNewLinkPrerenderImpl(
        prerender_id, child_id, render_view_route_id, 
        url, referrer, size);
  }
}

// static
void PrerenderLinkManager::OnRemovedLinkPrerender(Profile* profile,
                                                  int prerender_id,
                                                  int child_id) {
  if (prerender::PrerenderManager* prerender_manager =
      prerender::PrerenderManagerFactory::GetForProfile(profile))
    prerender_manager->link_manager()->OnRemovedLinkPrerenderImpl(prerender_id,
                                                                  child_id);
}

// static
void PrerenderLinkManager::OnUnloadedLinkPrerender(Profile* profile,
                                                   int prerender_id,
                                                   int child_id) {
  if (prerender::PrerenderManager* prerender_manager =
      prerender::PrerenderManagerFactory::GetForProfile(profile))
    prerender_manager->link_manager()->OnUnloadedLinkPrerenderImpl(prerender_id,
                                                                   child_id);
}

}  // namespace prerender
