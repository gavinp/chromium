// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/net/url_fetcher_impl.h"

#include "base/message_loop_proxy.h"
#include "content/common/net/url_fetcher_core.h"
#include "content/public/common/url_fetcher_factory.h"

static content::URLFetcherFactory* g_factory = NULL;

// static
content::URLFetcher* content::URLFetcher::Create(
    const GURL& url,
    RequestType request_type,
    content::URLFetcherDelegate* d) {
  return new URLFetcherImpl(url, request_type, d);
}

// static
content::URLFetcher* content::URLFetcher::Create(
    int id,
    const GURL& url,
    RequestType request_type,
    content::URLFetcherDelegate* d) {
  return g_factory ? g_factory->CreateURLFetcher(id, url, request_type, d) :
                     new URLFetcherImpl(url, request_type, d);
}

// static
void content::URLFetcher::CancelAll() {
  URLFetcherImpl::CancelAll();
}

// static
void content::URLFetcher::SetEnableInterceptionForTests(bool enabled) {
  URLFetcherCore::SetEnableInterceptionForTests(enabled);
}


URLFetcherImpl::URLFetcherImpl(const GURL& url,
                               RequestType request_type,
                               content::URLFetcherDelegate* d)
    : ALLOW_THIS_IN_INITIALIZER_LIST(
        core_(new content::URLFetcherCore(this, url, request_type, d))) {
}

URLFetcherImpl::~URLFetcherImpl() {
  core_->Stop();
}

void URLFetcherImpl::SetUploadData(const std::string& upload_content_type,
                                   const std::string& upload_content) {
  core_->SetUploadData(upload_content_type, upload_content);
}

void URLFetcherImpl::SetChunkedUpload(const std::string& content_type) {
  core_->SetChunkedUpload(content_type);
}

void URLFetcherImpl::AppendChunkToUpload(const std::string& data,
                                         bool is_last_chunk) {
  DCHECK(data.length());
  core_->AppendChunkToUpload(data, is_last_chunk);
}

void URLFetcherImpl::SetReferrer(const std::string& referrer) {
  core_->SetReferrer(referrer);
}

void URLFetcherImpl::SetLoadFlags(int load_flags) {
  core_->SetLoadFlags(load_flags);
}

int URLFetcherImpl::GetLoadFlags() const {
  return core_->GetLoadFlags();
}

void URLFetcherImpl::SetExtraRequestHeaders(
    const std::string& extra_request_headers) {
  core_->SetExtraRequestHeaders(extra_request_headers);
}

void URLFetcherImpl::AddExtraRequestHeader(const std::string& header_line) {
  core_->AddExtraRequestHeader(header_line);
}

void URLFetcherImpl::GetExtraRequestHeaders(
    net::HttpRequestHeaders* headers) const {
  GetExtraRequestHeaders(headers);
}

void URLFetcherImpl::SetRequestContext(
    net::URLRequestContextGetter* request_context_getter) {
  core_->SetRequestContext(request_context_getter);
}

void URLFetcherImpl::AssociateWithRenderView(
    const GURL& first_party_for_cookies,
    int render_process_id,
    int render_view_id) {
  core_->AssociateWithRenderView(
      first_party_for_cookies, render_process_id, render_view_id);
}

void URLFetcherImpl::SetAutomaticallyRetryOn5xx(bool retry) {
  core_->SetAutomaticallyRetryOn5xx(retry);
}

void URLFetcherImpl::SetMaxRetries(int max_retries) {
  core_->SetMaxRetries(max_retries);
}

int URLFetcherImpl::GetMaxRetries() const {
  return core_->GetMaxRetries();
}


base::TimeDelta URLFetcherImpl::GetBackoffDelay() const {
  return core_->GetBackoffDelay();
}

void URLFetcherImpl::SaveResponseToFileAtPath(
    const FilePath& file_path,
    scoped_refptr<base::MessageLoopProxy> file_message_loop_proxy) {
  core_->SaveResponseToFileAtPath(file_path, file_message_loop_proxy);
}

void URLFetcherImpl::SaveResponseToTemporaryFile(
    scoped_refptr<base::MessageLoopProxy> file_message_loop_proxy) {
  core_->SaveResponseToTemporaryFile(file_message_loop_proxy);
}

net::HttpResponseHeaders* URLFetcherImpl::GetResponseHeaders() const {
  return core_->GetResponseHeaders();
}

net::HostPortPair URLFetcherImpl::GetSocketAddress() const {
  return core_->GetSocketAddress();
}

bool URLFetcherImpl::WasFetchedViaProxy() const {
  return core_->WasFetchedViaProxy();
}

void URLFetcherImpl::Start() {
  core_->Start();
}

const GURL& URLFetcherImpl::GetOriginalURL() const {
  return core_->GetOriginalURL();
}

const GURL& URLFetcherImpl::GetURL() const {
  return core_->GetURL();
}

const net::URLRequestStatus& URLFetcherImpl::GetStatus() const {
  return core_->GetStatus();
}

int URLFetcherImpl::GetResponseCode() const {
  return core_->GetResponseCode();
}

const net::ResponseCookies& URLFetcherImpl::GetCookies() const {
  return core_->GetCookies();
}

bool URLFetcherImpl::FileErrorOccurred(
    base::PlatformFileError* out_error_code) const {
  return core_->FileErrorOccurred(out_error_code);
}

void URLFetcherImpl::ReceivedContentWasMalformed() {
  core_->ReceivedContentWasMalformed();
}

bool URLFetcherImpl::GetResponseAsString(
    std::string* out_response_string) const {
  return core_->GetResponseAsString(out_response_string);
}

bool URLFetcherImpl::GetResponseAsFilePath(
    bool take_ownership,
    FilePath* out_response_path) const {
  return core_->GetResponseAsFilePath(take_ownership, out_response_path);
}

// static
void URLFetcherImpl::CancelAll() {
  content::URLFetcherCore::CancelAll();
}

// static
int URLFetcherImpl::GetNumFetcherCores() {
  return content::URLFetcherCore::GetNumFetcherCores();
}

content::URLFetcherDelegate* URLFetcherImpl::delegate() const {
  return core_->delegate();
}

// static
content::URLFetcherFactory* URLFetcherImpl::factory() {
  return g_factory;
}

// static
void URLFetcherImpl::set_factory(content::URLFetcherFactory* factory) {
  g_factory = factory;
}
