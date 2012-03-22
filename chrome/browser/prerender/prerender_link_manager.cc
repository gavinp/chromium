// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/prerender/prerender_link_manager.h"

#include <utility>

#include "content/common/referrer.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/prerender/prerender_manager.h"
#include "chrome/browser/prerender/prerender_manager_factory.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/site_instance.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebReferrerPolicy.h"

namespace prerender {

PrerenderLinkManager::PrerenderLinkManager(
    RenderViewHost* render_view_host)
    : content::RenderViewHostObserver(render_view_host) {
  SiteInstance* site_instance = render_view_host->GetSiteInstance();
  profile_ = Profile::FromBrowserContext(site_instance->GetBrowserContext());
}

PrerenderLinkManager::~PrerenderLinkManager() {
}

PrerenderLinkManager::OnMessageReceived(
    const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PrerenderLinkManager, message)
      IPC_MESSAGE_HANDLER(PrerenderMsg_NewLinkPrerender,
                          OnNewLinkPrerender)
      IPC_MESSAGE_HANDLER(PrerenderMsg_RemovedLinkPrerender,
                          OnRemovedLinkPrerender)
      IPC_MESSAGE_HANDLER(PrerenderMsg_UnloadedLinkPrerender,
                          OnUnloadedLinkPrerender)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void PrerenderLinkManager::OnNewLinkPrerender(
    const IPC::Message& message, const GURL& url, const GURL& referrer,
    WebKit::WebReferrerPolicy policy) {
  PrerenderManager* prerender_manager =
      PrerenderManagerFactory::GetForProfile(profile_);
  if (!prerender_manager)
    return;

  const int child_id = render_view_host()->GetProcess()->GetID();
  const int route_id = message.routing_id();

  prerender_manager->AddPrerenderFromLinkRelPrerender(
      child_id, route_id, url, content::Referrer(referrer, policy));
  DCHECK(id_map_.find(std::make_pair(id,url) == id_map_.end()));
  id_map_.insert(std::make_pair(id, url));
  url_map_.insert(std::make_pair(url, id));
}

void PrerenderLinkManager::OnRemovedLinkPrerender(
    const IPC::Message& message, int id) {
  PrerenderManager* prerender_manager =
      PrerenderManagerFactory::GetForProfile(profile_);
  if (!prerender_manager)
    return;
  
  const int child_id = render_view_host()->GetProcess()->GetID();
  const int route_id = message.routing_id();

  LinkIdToUrlMap::const_iterator id_url_iter = id_map_.find(id);
  if (id_url_iter == id_map_.end())
    return;
  
  const GURL& url = id_url_iter->second;

  // Are any other link elements prerendering this URL?
  UrlToLinkIdMap url_id_iter = url_map_.find(url);
  int remaining_prerender_link_count = 0;
  while (url_id_iter != url_map_.end() && url_id_iter->first == url) {
    if (url_id_iter->second == id) {
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
    const IPC::Message& ALLOW_UNUSED message, int ALLOW_UNUSED id) {
  // TODO(gavinp,cbentzel): Implement reasonable behaviour for
  // navigation away from launcher.
}

}  // namespace prerender
