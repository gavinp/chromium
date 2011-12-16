// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/app_notify_channel_setup.h"

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/json/json_reader.h"
#include "base/stringprintf.h"
#include "chrome/browser/net/gaia/token_service.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/net/gaia/gaia_constants.h"
#include "chrome/common/net/gaia/gaia_urls.h"
#include "chrome/common/net/http_return.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/common/url_fetcher.h"
#include "net/base/escape.h"
#include "net/base/load_flags.h"
#include "net/http/http_request_headers.h"
#include "net/url_request/url_request_status.h"

using base::StringPrintf;
using content::BrowserThread;
using content::URLFetcher;

namespace {

static const char kChannelSetupAuthError[] = "unauthorized";
static const char kChannelSetupInternalError[] = "internal_error";
static const char kChannelSetupCanceledByUser[] = "canceled_by_user";
static const char kAuthorizationHeaderFormat[] =
    "Authorization: Bearer %s";
static const char kOAuth2IssueTokenURL[] =
    "https://www.googleapis.com/oauth2/v2/IssueToken";
static const char kOAuth2IssueTokenBodyFormat[] =
    "force=true"
    "&response_type=token"
    "&scope=%s"
    "&client_id=%s"
    "&origin=%s";
static const char kOAuth2IssueTokenScope[] =
    "https://www.googleapis.com/auth/chromewebstore.notification";
static const char kCWSChannelServiceURL[] =
    "https://www.googleapis.com/chromewebstore/v1.1/channels/id";

static AppNotifyChannelSetup::InterceptorForTests* g_interceptor_for_tests =
    NULL;

}  // namespace.

// static
void AppNotifyChannelSetup::SetInterceptorForTests(
    AppNotifyChannelSetup::InterceptorForTests* interceptor) {
  // Only one interceptor at a time, please.
  CHECK(g_interceptor_for_tests == NULL);
  g_interceptor_for_tests = interceptor;
}

AppNotifyChannelSetup::AppNotifyChannelSetup(
    Profile* profile,
    const std::string& extension_id,
    const std::string& client_id,
    const GURL& requestor_url,
    int return_route_id,
    int callback_id,
    AppNotifyChannelUI* ui,
    base::WeakPtr<AppNotifyChannelSetup::Delegate> delegate)
    : profile_(profile),
      extension_id_(extension_id),
      client_id_(client_id),
      requestor_url_(requestor_url),
      return_route_id_(return_route_id),
      callback_id_(callback_id),
      delegate_(delegate),
      ui_(ui),
      state_(INITIAL) {}

AppNotifyChannelSetup::~AppNotifyChannelSetup() {}

void AppNotifyChannelSetup::Start() {
  if (g_interceptor_for_tests) {
    std::string channel_id;
    std::string error;
    g_interceptor_for_tests->DoIntercept(this, &channel_id, &error);
    delegate_->AppNotifyChannelSetupComplete(channel_id, error, this);
    return;
  }
  AddRef();  // Balanced in ReportResult.
  BeginLogin();
}

void AppNotifyChannelSetup::OnGetTokenSuccess(
    const std::string& access_token) {
  oauth2_access_token_ = access_token;
  EndGetAccessToken(true);
}
void AppNotifyChannelSetup::OnGetTokenFailure(
    const GoogleServiceAuthError& error) {
  EndGetAccessToken(false);
}

void AppNotifyChannelSetup::OnSyncSetupResult(bool enabled) {
  EndLogin(enabled);
}

void AppNotifyChannelSetup::OnURLFetchComplete(const URLFetcher* source) {
  CHECK(source);
  switch (state_) {
    case RECORD_GRANT_STARTED:
      EndRecordGrant(source);
      break;
    case CHANNEL_ID_SETUP_STARTED:
      EndGetChannelId(source);
      break;
    default:
      CHECK(false) << "Wrong state: " << state_;
      break;
  }
}

// The contents of |body| should be URL-encoded as appropriate.
URLFetcher* AppNotifyChannelSetup::CreateURLFetcher(
    const GURL& url, const std::string& body, const std::string& auth_token) {
  CHECK(url.is_valid());
  URLFetcher::RequestType type =
      body.empty() ? URLFetcher::GET : URLFetcher::POST;
  URLFetcher* fetcher = URLFetcher::Create(0, url, type, this);
  fetcher->SetRequestContext(profile_->GetRequestContext());
  // Always set flags to neither send nor save cookies.
  fetcher->SetLoadFlags(
      net::LOAD_DO_NOT_SEND_COOKIES | net::LOAD_DO_NOT_SAVE_COOKIES);
  fetcher->SetExtraRequestHeaders(MakeAuthorizationHeader(auth_token));
  if (!body.empty()) {
    fetcher->SetUploadData("application/x-www-form-urlencoded", body);
  }
  return fetcher;
}

bool AppNotifyChannelSetup::ShouldPromptForLogin() const {
  std::string username = profile_->GetPrefs()->GetString(
      prefs::kGoogleServicesUsername);
  // Prompt for login if either the user has not logged in at all or
  // if the user is logged in but there is no OAuth2 login token.
  // The latter happens for users who are already logged in before the
  // code to generate OAuth2 login token is released.
  return username.empty() ||
         !profile_->GetTokenService()->HasOAuthLoginToken();
}

void AppNotifyChannelSetup::BeginLogin() {
  CHECK_EQ(INITIAL, state_);
  state_ = LOGIN_STARTED;
  if (ShouldPromptForLogin()) {
    ui_->PromptSyncSetup(this);
    // We'll get called back in OnSyncSetupResult
  } else {
    EndLogin(true);
  }
}

void AppNotifyChannelSetup::EndLogin(bool success) {
  CHECK_EQ(LOGIN_STARTED, state_);
  if (success) {
    state_ = LOGIN_DONE;
    BeginGetAccessToken();
  } else {
    state_ = ERROR_STATE;
    ReportResult("", kChannelSetupCanceledByUser);
  }
}

void AppNotifyChannelSetup::BeginGetAccessToken() {
  CHECK_EQ(LOGIN_DONE, state_);
  state_ = FETCH_ACCESS_TOKEN_STARTED;

  oauth2_fetcher_.reset(new OAuth2AccessTokenFetcher(
      this, profile_->GetRequestContext()));
  std::vector<std::string> scopes;
  scopes.push_back(GaiaUrls::GetInstance()->oauth1_login_scope());
  scopes.push_back(kOAuth2IssueTokenScope);
  oauth2_fetcher_->Start(
      GaiaUrls::GetInstance()->oauth2_chrome_client_id(),
      GaiaUrls::GetInstance()->oauth2_chrome_client_secret(),
      profile_->GetTokenService()->GetOAuth2LoginRefreshToken(),
      scopes);
}

void AppNotifyChannelSetup::EndGetAccessToken(bool success) {
  CHECK_EQ(FETCH_ACCESS_TOKEN_STARTED, state_);
  if (success) {
    state_ = FETCH_ACCESS_TOKEN_DONE;
    BeginRecordGrant();
  } else {
    state_ = ERROR_STATE;
    ReportResult("", kChannelSetupInternalError);
  }
}

void AppNotifyChannelSetup::BeginRecordGrant() {
  CHECK_EQ(FETCH_ACCESS_TOKEN_DONE, state_);
  state_ = RECORD_GRANT_STARTED;

  GURL url = GetOAuth2IssueTokenURL();
  std::string body = MakeOAuth2IssueTokenBody(client_id_, extension_id_);

  url_fetcher_.reset(CreateURLFetcher(url, body, oauth2_access_token_));
  url_fetcher_->Start();
}

void AppNotifyChannelSetup::EndRecordGrant(const URLFetcher* source) {
  CHECK_EQ(RECORD_GRANT_STARTED, state_);

  net::URLRequestStatus status = source->GetStatus();

  if (status.status() == net::URLRequestStatus::SUCCESS) {
    if (source->GetResponseCode() == RC_REQUEST_OK) {
      state_ = RECORD_GRANT_DONE;
      BeginGetChannelId();
    } else {
      // Successfully done with HTTP request, but got an explicit error.
      state_ = ERROR_STATE;
      ReportResult("", kChannelSetupAuthError);
    }
  } else {
    // Could not do HTTP request.
    state_ = ERROR_STATE;
    ReportResult("", kChannelSetupInternalError);
  }
}

void AppNotifyChannelSetup::BeginGetChannelId() {
  CHECK_EQ(RECORD_GRANT_DONE, state_);
  state_ = CHANNEL_ID_SETUP_STARTED;

  GURL url = GetCWSChannelServiceURL();

  url_fetcher_.reset(CreateURLFetcher(url, "", oauth2_access_token_));
  url_fetcher_->Start();
}

void AppNotifyChannelSetup::EndGetChannelId(const URLFetcher* source) {
  CHECK_EQ(CHANNEL_ID_SETUP_STARTED, state_);
  net::URLRequestStatus status = source->GetStatus();

  if (status.status() == net::URLRequestStatus::SUCCESS) {
    if (source->GetResponseCode() == RC_REQUEST_OK) {
      std::string data;
      source->GetResponseAsString(&data);
      std::string channel_id;
      bool result = ParseCWSChannelServiceResponse(data, &channel_id);
      if (result) {
        state_ = CHANNEL_ID_SETUP_DONE;
        ReportResult(channel_id, "");
      } else {
        state_ = ERROR_STATE;
        ReportResult("", kChannelSetupInternalError);
      }
    } else {
      // Successfully done with HTTP request, but got an explicit error.
      state_ = ERROR_STATE;
      ReportResult("", kChannelSetupAuthError);
    }
  } else {
    // Could not do HTTP request.
    state_ = ERROR_STATE;
    ReportResult("", kChannelSetupInternalError);
  }
}

void AppNotifyChannelSetup::ReportResult(
    const std::string& channel_id,
    const std::string& error) {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  CHECK(state_ == CHANNEL_ID_SETUP_DONE || state_ == ERROR_STATE);

  if (delegate_.get()) {
    delegate_->AppNotifyChannelSetupComplete(channel_id, error, this);
  }
  Release(); // Matches AddRef in Start.
}

// static
GURL AppNotifyChannelSetup::GetCWSChannelServiceURL() {
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kAppNotifyChannelServerURL)) {
    std::string switch_value = command_line->GetSwitchValueASCII(
        switches::kAppNotifyChannelServerURL);
    GURL result(switch_value);
    if (result.is_valid()) {
      return result;
    } else {
      LOG(ERROR) << "Invalid value for " <<
          switches::kAppNotifyChannelServerURL;
    }
  }
  return GURL(kCWSChannelServiceURL);
}

// static
GURL AppNotifyChannelSetup::GetOAuth2IssueTokenURL() {
  return GURL(kOAuth2IssueTokenURL);
}

// static
std::string AppNotifyChannelSetup::MakeOAuth2IssueTokenBody(
    const std::string& oauth_client_id,
    const std::string& extension_id) {
  return StringPrintf(kOAuth2IssueTokenBodyFormat,
      kOAuth2IssueTokenScope,
      net::EscapeUrlEncodedData(oauth_client_id, true).c_str(),
      net::EscapeUrlEncodedData(extension_id, true).c_str());
}

// static
std::string AppNotifyChannelSetup::MakeAuthorizationHeader(
    const std::string& auth_token) {
  return StringPrintf(kAuthorizationHeaderFormat, auth_token.c_str());
}

// static
bool AppNotifyChannelSetup::ParseCWSChannelServiceResponse(
    const std::string& data, std::string* result) {
  base::JSONReader reader;
  scoped_ptr<base::Value> value(reader.Read(data, false));
  if (!value.get() || value->GetType() != base::Value::TYPE_DICTIONARY)
    return false;

  Value* channel_id_value;
  DictionaryValue* dict = static_cast<DictionaryValue*>(value.get());
  if (!dict->Get("id", &channel_id_value))
    return false;
  if (channel_id_value->GetType() != base::Value::TYPE_STRING)
    return false;

  StringValue* channel_id = static_cast<StringValue*>(channel_id_value);
  channel_id->GetAsString(result);
  return true;
}
