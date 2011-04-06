// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_RENDER_THREAD_H_
#define CHROME_RENDERER_RENDER_THREAD_H_
#pragma once

#include <set>
#include <string>
#include <vector>

#include "base/observer_list.h"
#include "base/shared_memory.h"
#include "base/time.h"
#include "base/timer.h"
#include "build/build_config.h"
#include "chrome/renderer/chrome_content_renderer_client.h"
#include "chrome/renderer/visitedlink_slave.h"
#include "content/common/child_thread.h"
#include "content/common/css_colors.h"
#include "content/common/gpu_process_launch_causes.h"
#include "ipc/ipc_channel_proxy.h"
#include "ipc/ipc_platform_file.h"
#include "ui/gfx/native_widget_types.h"

class AppCacheDispatcher;
class CookieMessageFilter;
class DBMessageFilter;
class DevToolsAgentFilter;
class FilePath;
class GpuChannelHost;
class IndexedDBDispatcher;
class RendererHistogram;
class RendererHistogramSnapshots;
class RenderProcessObserver;
class RendererNetPredictor;
class RendererWebKitClientImpl;
class SpellCheck;
class SkBitmap;
class WebDatabaseObserverImpl;

struct ContentSettings;
struct RendererPreferences;
struct DOMStorageMsg_Event_Params;
struct GPUInfo;
struct ViewMsg_New_Params;
struct WebPreferences;

namespace base {
class MessageLoopProxy;
class Thread;
}

namespace IPC {
struct ChannelHandle;
}

namespace WebKit {
class WebStorageEventDispatcher;
}

namespace v8 {
class Extension;
}

// The RenderThreadBase is the minimal interface that a RenderView/Widget
// expects from a render thread. The interface basically abstracts a way to send
// and receive messages.
//
// TODO(brettw): This has two different and opposing usage patterns which
// make it confusing.
//
// In the first mode, callers call RenderThread::current() to get the one and
// only global RenderThread (bug 10837: this should be renamed get()). Then
// they access it. Since RenderThread is a concrete class, this can be NULL
// during unit tests. Callers need to NULL check this every time. Some callers
// don't happen to get called during unit tests and don't do the NULL checks,
// which is also confusing since it's not clear if you need to or not.
//
// In the second mode, the abstract base class RenderThreadBase is passed to
// RenderView and RenderWidget. Normally, this points to
// RenderThread::current() so it's quite confusing which accessing mode should
// be used. However, during unit testing, this class is replaced with a mock
// to support testing functions, and is guaranteed non-NULL.
//
// It might be nice not to have the ::current() call and put all of the
// functions on the abstract class so they can be mocked. However, there are
// some standalone functions like in ChromiumBridge that are not associated
// with a view that need to access the current thread to send messages to the
// browser process. These need the ::current() paradigm. So instead, we should
// probably remove the render_thread_ parameter to RenderView/Widget in
// preference to just getting the global singleton. We can make it easier to
// understand by moving everything to the abstract interface and saying that
// there should never be a NULL RenderThread::current(). Tests would be
// responsible for setting up the mock one.
class RenderThreadBase {
 public:
  virtual ~RenderThreadBase() {}

  virtual bool Send(IPC::Message* msg) = 0;

  // Called to add or remove a listener for a particular message routing ID.
  // These methods normally get delegated to a MessageRouter.
  virtual void AddRoute(int32 routing_id, IPC::Channel::Listener* listener) = 0;
  virtual void RemoveRoute(int32 routing_id) = 0;

  virtual void AddFilter(IPC::ChannelProxy::MessageFilter* filter) = 0;
  virtual void RemoveFilter(IPC::ChannelProxy::MessageFilter* filter) = 0;

  // Called by a RenderWidget when it is hidden or restored.
  virtual void WidgetHidden() = 0;
  virtual void WidgetRestored() = 0;

  // True if this process is running in an incognito profile.
  virtual bool IsIncognitoProcess() const = 0;
};

// The RenderThread class represents a background thread where RenderView
// instances live.  The RenderThread supports an API that is used by its
// consumer to talk indirectly to the RenderViews and supporting objects.
// Likewise, it provides an API for the RenderViews to talk back to the main
// process (i.e., their corresponding TabContents).
//
// Most of the communication occurs in the form of IPC messages.  They are
// routed to the RenderThread according to the routing IDs of the messages.
// The routing IDs correspond to RenderView instances.
class RenderThread : public RenderThreadBase,
                     public ChildThread {
 public:
  // Grabs the IPC channel name from the command line.
  RenderThread();
  // Constructor that's used when running in single process mode.
  explicit RenderThread(const std::string& channel_name);
  virtual ~RenderThread();

  // Returns the one render thread for this process.  Note that this should only
  // be accessed when running on the render thread itself
  //
  // TODO(brettw) this should be on the abstract base class instead of here,
  // and return the base class' interface instead. Currently this causes
  // problems with testing. See the comment above RenderThreadBase above.
  static RenderThread* current();

  // Returns the routing ID of the RenderWidget containing the current script
  // execution context (corresponding to WebFrame::frameForCurrentContext).
  static int32 RoutingIDForCurrentContext();

  // Overridden from RenderThreadBase.
  virtual bool Send(IPC::Message* msg);
  virtual void AddRoute(int32 routing_id, IPC::Channel::Listener* listener);
  virtual void RemoveRoute(int32 routing_id);
  virtual void AddFilter(IPC::ChannelProxy::MessageFilter* filter);
  virtual void RemoveFilter(IPC::ChannelProxy::MessageFilter* filter);
  virtual void WidgetHidden();
  virtual void WidgetRestored();
  virtual bool IsIncognitoProcess() const;

  void AddObserver(RenderProcessObserver* observer);
  void RemoveObserver(RenderProcessObserver* observer);

  // These methods modify how the next message is sent.  Normally, when sending
  // a synchronous message that runs a nested message loop, we need to suspend
  // callbacks into WebKit.  This involves disabling timers and deferring
  // resource loads.  However, there are exceptions when we need to customize
  // the behavior.
  void DoNotSuspendWebKitSharedTimer();
  void DoNotNotifyWebKitOfModalLoop();

  VisitedLinkSlave* visited_link_slave() const {
    return visited_link_slave_.get();
  }

  AppCacheDispatcher* appcache_dispatcher() const {
    return appcache_dispatcher_.get();
  }

  IndexedDBDispatcher* indexed_db_dispatcher() const {
    return indexed_db_dispatcher_.get();
  }

  SpellCheck* spellchecker() const {
    return spellchecker_.get();
  }

  bool plugin_refresh_allowed() const { return plugin_refresh_allowed_; }

  double idle_notification_delay_in_s() const {
    return idle_notification_delay_in_s_;
  }
  void set_idle_notification_delay_in_s(double idle_notification_delay_in_s) {
    idle_notification_delay_in_s_ = idle_notification_delay_in_s;
  }

  // Do DNS prefetch resolution of a hostname.
  void Resolve(const char* name, size_t length);

  // Send all the Histogram data to browser.
  void SendHistograms(int sequence_number);

  // Invokes InformHostOfCacheStats after a short delay.  Used to move this
  // bookkeeping operation off the critical latency path.
  void InformHostOfCacheStatsLater();

  // Sends a message to the browser to close all connections.
  void CloseCurrentConnections();

  // Sends a message to the browser to enable or disable the disk cache.
  void SetCacheMode(bool enabled);

  // Sends a message to the browser to clear the disk cache.
  // |preserve_ssl_host_info| is a flag indicating if the cache should purge
  // entries related to cached SSL information.
  void ClearCache(bool preserve_ssl_host_info);

  // Sends a message to the browser to clear thed host cache.
  void ClearHostResolverCache();

  // Sends a message to the browser to clear the predictor cache.
  void ClearPredictorCache();

  // Sends a message to the browser to enable/disable spdy.
  void EnableSpdy(bool enable);

  // Asynchronously establish a channel to the GPU plugin if not previously
  // established or if it has been lost (for example if the GPU plugin crashed).
  // Use GetGpuChannel() to determine when the channel is ready for use.
  void EstablishGpuChannel(content::CauseForGpuLaunch);

  // Synchronously establish a channel to the GPU plugin if not previously
  // established or if it has been lost (for example if the GPU plugin crashed).
  // If there is a pending asynchronous request, it will be completed by the
  // time this routine returns.
  GpuChannelHost* EstablishGpuChannelSync(content::CauseForGpuLaunch);

  // Get the GPU channel. Returns NULL if the channel is not established or
  // has been lost.
  GpuChannelHost* GetGpuChannel();

  // Returns a MessageLoopProxy instance corresponding to the message loop
  // of the thread on which file operations should be run. Must be called
  // on the renderer's main thread.
  scoped_refptr<base::MessageLoopProxy> GetFileThreadMessageLoopProxy();

  // This function is called for every registered V8 extension each time a new
  // script context is created. Returns true if the given V8 extension is
  // allowed to run on the given URL and extension group.
  bool AllowScriptExtension(const std::string& v8_extension_name,
                            const GURL& url,
                            int extension_group);

  // Hack for http://crbug.com/71735.
  // TODO(jamesr): remove once http://crbug.com/72007 is fixed.
  RendererWebKitClientImpl* GetWebKitClientImpl() const {
    return webkit_client_.get();
  }

  // Schedule a call to IdleHandler with the given initial delay.
  void ScheduleIdleHandler(double initial_delay_s);

  // A task we invoke periodically to assist with idle cleanup.
  void IdleHandler();

  // Registers the given V8 extension with WebKit.
  void RegisterExtension(v8::Extension* extension);

 private:
  virtual bool OnControlMessageReceived(const IPC::Message& msg);

  void Init();

  void OnUpdateVisitedLinks(base::SharedMemoryHandle table);
  void OnAddVisitedLinks(const VisitedLinkSlave::Fingerprints& fingerprints);
  void OnResetVisitedLinks();
  void OnSetZoomLevelForCurrentURL(const GURL& url, double zoom_level);
  void OnSetContentSettingsForCurrentURL(
      const GURL& url, const ContentSettings& content_settings);
  void OnDOMStorageEvent(const DOMStorageMsg_Event_Params& params);
  void OnSetNextPageID(int32 next_page_id);
  void OnSetIsIncognitoProcess(bool is_incognito_process);
  void OnSetCSSColors(const std::vector<CSSColors::CSSColorMapping>& colors);
  void OnCreateNewView(const ViewMsg_New_Params& params);
  void OnTransferBitmap(const SkBitmap& bitmap, int resource_id);
  void OnSetCacheCapacities(size_t min_dead_capacity,
                            size_t max_dead_capacity,
                            size_t capacity);
  void OnClearCache();
  void OnGetCacheResourceStats();

  // Send all histograms to browser.
  void OnGetRendererHistograms(int sequence_number);

  // Send tcmalloc info to browser.
  void OnGetRendererTcmalloc();
  void OnGetV8HeapStats();

  void OnPurgeMemory();
  void OnPurgePluginListCache(bool reload_pages);

  void OnInitSpellChecker(IPC::PlatformFileForTransit bdict_file,
                          const std::vector<std::string>& custom_words,
                          const std::string& language,
                          bool auto_spell_correct);
  void OnSpellCheckWordAdded(const std::string& word);
  void OnSpellCheckEnableAutoSpellCorrect(bool enable);

  void OnGpuChannelEstablished(const IPC::ChannelHandle& channel_handle,
                               base::ProcessHandle renderer_process_for_gpu,
                               const GPUInfo& gpu_info);

  void OnSetPhishingModel(IPC::PlatformFileForTransit model_file);

  void OnGetAccessibilityTree();

  // Gather usage statistics from the in-memory cache and inform our host.
  // These functions should be call periodically so that the host can make
  // decisions about how to allocation resources using current information.
  void InformHostOfCacheStats();

  // We initialize WebKit as late as possible.
  void EnsureWebKitInitialized();

  // These objects live solely on the render thread.
  scoped_ptr<ScopedRunnableMethodFactory<RenderThread> > task_factory_;
  scoped_ptr<VisitedLinkSlave> visited_link_slave_;
  scoped_ptr<RendererNetPredictor> renderer_net_predictor_;
  scoped_ptr<AppCacheDispatcher> appcache_dispatcher_;
  scoped_ptr<IndexedDBDispatcher> indexed_db_dispatcher_;
  scoped_refptr<DevToolsAgentFilter> devtools_agent_filter_;
  scoped_ptr<RendererHistogramSnapshots> histogram_snapshots_;
  scoped_ptr<RendererWebKitClientImpl> webkit_client_;
  scoped_ptr<WebKit::WebStorageEventDispatcher> dom_storage_event_dispatcher_;
  scoped_ptr<SpellCheck> spellchecker_;

  // Used on the renderer and IPC threads.
  scoped_refptr<DBMessageFilter> db_message_filter_;
  scoped_refptr<CookieMessageFilter> cookie_message_filter_;

  // Used on multiple script execution context threads.
  scoped_ptr<WebDatabaseObserverImpl> web_database_observer_impl_;

#if defined(OS_POSIX)
  scoped_refptr<IPC::ChannelProxy::MessageFilter>
      suicide_on_channel_error_filter_;
#endif

  // If true, then a GetPlugins call is allowed to rescan the disk.
  bool plugin_refresh_allowed_;

  // Is there a pending task for doing CacheStats.
  bool cache_stats_task_pending_;

  // The count of RenderWidgets running through this thread.
  int widget_count_;

  // The count of hidden RenderWidgets running through this thread.
  int hidden_widget_count_;

  // The current value of the idle notification timer delay.
  double idle_notification_delay_in_s_;

  // True if this renderer is incognito.
  bool is_incognito_process_;

  bool suspend_webkit_shared_timer_;
  bool notify_webkit_of_modal_loop_;

  // Timer that periodically calls IdleHandler.
  base::RepeatingTimer<RenderThread> idle_timer_;

  // The channel from the renderer process to the GPU process.
  scoped_refptr<GpuChannelHost> gpu_channel_;

  // A lazily initiated thread on which file operations are run.
  scoped_ptr<base::Thread> file_thread_;

  // Map of registered v8 extensions. The key is the extension name.
  std::set<std::string> v8_extensions_;

  chrome::ChromeContentRendererClient renderer_client_;

  ObserverList<RenderProcessObserver> observers_;

  DISALLOW_COPY_AND_ASSIGN(RenderThread);
};

#endif  // CHROME_RENDERER_RENDER_THREAD_H_
