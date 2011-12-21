// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_SITE_INSTANCE_H_
#define CONTENT_BROWSER_RENDERER_HOST_SITE_INSTANCE_H_
#pragma once

#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/common/content_export.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "googleurl/src/gurl.h"

class BrowsingInstance;

namespace content {
class BrowserContext;
class RenderProcessHostFactory;
}

///////////////////////////////////////////////////////////////////////////////
//
// SiteInstance class
//
// A SiteInstance is a data structure that is associated with all pages in a
// given instance of a web site.  Here, a web site is identified by its
// registered domain name and scheme.  An instance includes all pages
// that are connected (i.e., either a user or a script navigated from one
// to the other).  We represent instances using the BrowsingInstance class.
//
// In --process-per-tab, one SiteInstance is created for each tab (i.e., in the
// TabContents constructor), unless the tab is created by script (i.e., in
// TabContents::CreateNewView).  This corresponds to one process per
// BrowsingInstance.
//
// In process-per-site-instance (the current default process model),
// SiteInstances are created (1) when the user manually creates a new tab
// (which also creates a new BrowsingInstance), and (2) when the user navigates
// across site boundaries (which uses the same BrowsingInstance).  If the user
// navigates within a site, or opens links in new tabs within a site, the same
// SiteInstance is used.
//
// In --process-per-site, we consolidate all SiteInstances for a given site,
// throughout the entire browser context.  This ensures that only one process
// will be dedicated to each site.
//
// Each NavigationEntry for a TabContents points to the SiteInstance that
// rendered it.  Each RenderViewHost also points to the SiteInstance that it is
// associated with.  A SiteInstance keeps track of the number of these
// references and deletes itself when the count goes to zero.  This means that
// a SiteInstance is only live as long as it is accessible, either from new
// tabs with no NavigationEntries or in NavigationEntries in the history.
//
///////////////////////////////////////////////////////////////////////////////
class CONTENT_EXPORT SiteInstance : public base::RefCounted<SiteInstance>,
                                    public content::NotificationObserver {
 public:
  // Returns a unique ID for this SiteInstance.
  int32 id() { return id_; }

  // Get the BrowsingInstance to which this SiteInstance belongs.
  BrowsingInstance* browsing_instance() { return browsing_instance_; }

  // Sets the factory used to create new RenderProcessHosts. This will also be
  // passed on to SiteInstances spawned by this one.
  //
  // The factory must outlive the SiteInstance; ownership is not transferred. It
  // may be NULL, in which case the default BrowserRenderProcessHost will be
  // created (this is the behavior if you don't call this function).
  void set_render_process_host_factory(
      content::RenderProcessHostFactory* rph_factory) {
    render_process_host_factory_ = rph_factory;
  }

  // Whether this SiteInstance has a running process associated with it.
  bool HasProcess() const;

  // Returns the current process being used to render pages in this
  // SiteInstance.  If the process has crashed or otherwise gone away, then
  // this method will create a new process and update our host ID accordingly.
  content::RenderProcessHost* GetProcess();

  // Set / Get the web site that this SiteInstance is rendering pages for.
  // This includes the scheme and registered domain, but not the port.  If the
  // URL does not have a valid registered domain, then the full hostname is
  // stored.
  void SetSite(const GURL& url);
  const GURL& site() const { return site_; }
  bool has_site() const { return has_site_; }

  // Returns whether there is currently a related SiteInstance (registered with
  // BrowsingInstance) for the site of the given url.  If so, we should try to
  // avoid dedicating an unused SiteInstance to it (e.g., in a new tab).
  bool HasRelatedSiteInstance(const GURL& url);

  // Gets a SiteInstance for the given URL that shares the current
  // BrowsingInstance, creating a new SiteInstance if necessary.  This ensures
  // that a BrowsingInstance only has one SiteInstance per site, so that pages
  // in a BrowsingInstance have the ability to script each other.  Callers
  // should ensure that this SiteInstance becomes ref counted, by storing it in
  // a scoped_refptr.  (By having this method, we can hide the BrowsingInstance
  // class from the rest of the codebase.)
  // TODO(creis): This may be an argument to build a pass_refptr<T> class, as
  // Darin suggests.
  SiteInstance* GetRelatedSiteInstance(const GURL& url);

  // Returns whether this SiteInstance has a process that is the wrong type for
  // the given URL.  If so, the browser should force a process swap when
  // navigating to the URL.
  bool HasWrongProcessForURL(const GURL& url) const;

  // Factory method to create a new SiteInstance.  This will create a new
  // new BrowsingInstance, so it should only be used when creating a new tab
  // from scratch (or similar circumstances).  Callers should ensure that
  // this SiteInstance becomes ref counted, by storing it in a scoped_refptr.
  //
  // The render process host factory may be NULL. See SiteInstance constructor.
  //
  // TODO(creis): This may be an argument to build a pass_refptr<T> class, as
  // Darin suggests.
  static SiteInstance* CreateSiteInstance(
      content::BrowserContext* browser_context);

  // Factory method to get the appropriate SiteInstance for the given URL, in
  // a new BrowsingInstance.  Use this instead of CreateSiteInstance when you
  // know the URL, since it allows special site grouping rules to be applied
  // (for example, to group chrome-ui pages into the same instance).
  static SiteInstance* CreateSiteInstanceForURL(
      content::BrowserContext* browser_context, const GURL& url);

  // Returns the site for the given URL, which includes only the scheme and
  // registered domain.  Returns an empty GURL if the URL has no host.
  static GURL GetSiteForURL(content::BrowserContext* context, const GURL& url);

  // Return whether both URLs are part of the same web site, for the purpose of
  // assigning them to processes accordingly.  The decision is currently based
  // on the registered domain of the URLs (google.com, bbc.co.uk), as well as
  // the scheme (https, http).  This ensures that two pages will be in
  // the same process if they can communicate with other via JavaScript.
  // (e.g., docs.google.com and mail.google.com have DOM access to each other
  // if they both set their document.domain properties to google.com.)
  static bool IsSameWebSite(content::BrowserContext* browser_context,
                            const GURL& url1, const GURL& url2);

 protected:
  friend class base::RefCounted<SiteInstance>;
  friend class BrowsingInstance;

  // Virtual to allow tests to extend it.
  virtual ~SiteInstance();

  // Create a new SiteInstance.  Protected to give access to BrowsingInstance
  // and tests; most callers should use CreateSiteInstance or
  // GetRelatedSiteInstance instead.
  explicit SiteInstance(BrowsingInstance* browsing_instance);

  // Get the effective URL for the given actual URL.
  static GURL GetEffectiveURL(content::BrowserContext* browser_context,
                              const GURL& url);

 private:
  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // Used to restrict a process' origin access rights.
  void LockToOrigin();

  // The next available SiteInstance ID.
  static int32 next_site_instance_id_;

  // A unique ID for this SiteInstance.
  int32 id_;

  content::NotificationRegistrar registrar_;

  // BrowsingInstance to which this SiteInstance belongs.
  scoped_refptr<BrowsingInstance> browsing_instance_;

  // Factory for new RenderProcessHosts, not owned by this class. NULL indiactes
  // that the default BrowserRenderProcessHost should be created.
  const content::RenderProcessHostFactory* render_process_host_factory_;

  // Current RenderProcessHost that is rendering pages for this SiteInstance.
  // This pointer will only change once the RenderProcessHost is destructed.  It
  // will still remain the same even if the process crashes, since in that
  // scenario the RenderProcessHost remains the same.
  content::RenderProcessHost* process_;

  // The web site that this SiteInstance is rendering pages for.
  GURL site_;

  // Whether SetSite has been called.
  bool has_site_;

  DISALLOW_COPY_AND_ASSIGN(SiteInstance);
};

#endif  // CONTENT_BROWSER_RENDERER_HOST_SITE_INSTANCE_H_
