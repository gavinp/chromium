// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROME_TO_MOBILE_SERVICE_H_
#define CHROME_BROWSER_CHROME_TO_MOBILE_SERVICE_H_
#pragma once

#include <map>
#include <string>
#include <vector>

#include "base/file_util.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_temp_dir.h"
#include "base/string16.h"
#include "base/timer.h"
#include "chrome/browser/profiles/profile_keyed_service.h"
#include "chrome/common/net/gaia/oauth2_access_token_consumer.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/common/url_fetcher_delegate.h"
#include "googleurl/src/gurl.h"

class OAuth2AccessTokenFetcher;
class CloudPrintURL;
class MockChromeToMobileService;
class Profile;

namespace base {
class DictionaryValue;
}

// ChromeToMobileService connects to the cloud print service to enumerate
// compatible mobiles owned by its profile and send URLs and MHTML snapshots.
// The mobile list updates regularly, and explicitly by RequestMobileListUpdate.
class ChromeToMobileService : public ProfileKeyedService,
                              public content::URLFetcherDelegate,
                              public content::NotificationObserver,
                              public OAuth2AccessTokenConsumer {
 public:
  class Observer {
   public:
    virtual ~Observer();

    // Called on generation of the page's MHTML snapshot.
    virtual void SnapshotGenerated(const FilePath& path, int64 bytes) = 0;

    // Called after URLFetcher responses from sending the URL (and snapshot).
    virtual void OnSendComplete(bool success) = 0;
  };

  // The URLFetcher request types.
  enum RequestType {
    SEARCH,
    URL,
    DELAYED_SNAPSHOT,
    SNAPSHOT,
  };

  // The aggregated URLFetcher submission data.
  struct RequestData {
    RequestData();
    ~RequestData();

    string16 mobile_id;
    GURL url;
    string16 title;
    FilePath snapshot_path;
    std::string snapshot_id;
    RequestType type;
  };

  // Returns whether Chrome To Mobile is enabled. Check for the 'disable' or
  // 'enable' command line switches, otherwise relay the default enabled state.
  static bool IsChromeToMobileEnabled();

  explicit ChromeToMobileService(Profile* profile);
  virtual ~ChromeToMobileService();

  // Get the list of mobile devices.
  const std::vector<base::DictionaryValue*>& mobiles() { return mobiles_; }

  // Request an updated mobile device list, request auth first if needed.
  void RequestMobileListUpdate();

  // Callback with an MHTML snapshot of the profile's selected WebContents.
  // Virtual for unit test mocking.
  virtual void GenerateSnapshot(base::WeakPtr<Observer> observer);

  // Send the profile's selected WebContents to the specified mobile device.
  // Virtual for unit test mocking.
  virtual void SendToMobile(const string16& mobile_id,
                            const FilePath& snapshot,
                            base::WeakPtr<Observer> observer);

  // content::URLFetcherDelegate method.
  virtual void OnURLFetchComplete(const content::URLFetcher* source) OVERRIDE;

  // content::NotificationObserver method.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // OAuth2AccessTokenConsumer methods.
  virtual void OnGetTokenSuccess(const std::string& access_token) OVERRIDE;
  virtual void OnGetTokenFailure(const GoogleServiceAuthError& error) OVERRIDE;

 private:
  friend class MockChromeToMobileService;

  // Utility function to initialize the ScopedTempDir.
  void CreateUniqueTempDir();

  // Utility function to create URLFetcher requests.
  content::URLFetcher* CreateRequest(const RequestData& data);

  // Send the OAuth2AccessTokenFetcher request.
  // Virtual for unit test mocking.
  virtual void RefreshAccessToken();

  // Send the cloud print URLFetcher search request.
  void RequestSearch();

  void HandleSearchResponse();
  void HandleSubmitResponse(const content::URLFetcher* source);

  Profile* profile_;

  // Used to recieve TokenService notifications for GaiaOAuth2LoginRefreshToken.
  content::NotificationRegistrar registrar_;

  // Cloud print helper class and auth token.
  scoped_ptr<CloudPrintURL> cloud_print_url_;
  std::string access_token_;

  // The list of mobile devices retrieved from the cloud print service.
  std::vector<base::DictionaryValue*> mobiles_;

  // The temporary directory for MHTML snapshot files.
  ScopedTempDir temp_dir_;

  // Map URLFetchers to observers for reporting OnSendComplete.
  typedef std::map<const content::URLFetcher*, base::WeakPtr<Observer> >
      RequestObserverMap;
  RequestObserverMap request_observer_map_;

  // The pending OAuth access token request and a timer for retrying on failure.
  scoped_ptr<OAuth2AccessTokenFetcher> access_token_fetcher_;
  base::OneShotTimer<ChromeToMobileService> auth_retry_timer_;

  // The pending mobile device search request; and the time of the last request.
  scoped_ptr<content::URLFetcher> search_request_;
  base::TimeTicks previous_search_time_;

  DISALLOW_COPY_AND_ASSIGN(ChromeToMobileService);
};

#endif  // CHROME_BROWSER_CHROME_TO_MOBILE_SERVICE_H_
