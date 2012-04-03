// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/net/url_fetcher_core.h"

#include "base/bind.h"
#include "base/file_util_proxy.h"
#include "base/logging.h"
#include "base/message_loop_proxy.h"
#include "base/metrics/histogram.h"
#include "base/stl_util.h"
#include "base/tracked_objects.h"
#include "content/common/net/url_request_user_data.h"
#include "content/public/common/url_fetcher_delegate.h"
#include "net/base/io_buffer.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/url_request/url_request_context_getter.h"
#include "net/url_request/url_request_throttler_manager.h"

namespace content {

static const int kBufferSize = 4096;
static const int kUploadProgressTimerInterval = 100;

URLFetcherCore::Registry::Registry() {}
URLFetcherCore::Registry::~Registry() {}

void URLFetcherCore::Registry::AddURLFetcherCore(URLFetcherCore* core) {
  DCHECK(!ContainsKey(fetchers_, core));
  fetchers_.insert(core);
}

void URLFetcherCore::Registry::RemoveURLFetcherCore(URLFetcherCore* core) {
  DCHECK(ContainsKey(fetchers_, core));
  fetchers_.erase(core);
}

void URLFetcherCore::Registry::CancelAll() {
  while (!fetchers_.empty())
    (*fetchers_.begin())->CancelURLRequest();
}

// static
base::LazyInstance<URLFetcherCore::Registry>
    URLFetcherCore::g_registry = LAZY_INSTANCE_INITIALIZER;

URLFetcherCore::FileWriter::FileWriter(
    URLFetcherCore* core,
    scoped_refptr<base::MessageLoopProxy> file_message_loop_proxy)
    : core_(core),
      error_code_(base::PLATFORM_FILE_OK),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_factory_(this)),
      file_message_loop_proxy_(file_message_loop_proxy),
      file_handle_(base::kInvalidPlatformFileValue) {
}

URLFetcherCore::FileWriter::~FileWriter() {
  RemoveFile();
}

void URLFetcherCore::FileWriter::CreateFileAtPath(
    const FilePath& file_path) {
  DCHECK(core_->io_message_loop_proxy_->BelongsToCurrentThread());
  DCHECK(file_message_loop_proxy_.get());
  base::FileUtilProxy::CreateOrOpen(
      file_message_loop_proxy_,
      file_path,
      base::PLATFORM_FILE_CREATE_ALWAYS | base::PLATFORM_FILE_WRITE,
      base::Bind(&URLFetcherCore::FileWriter::DidCreateFile,
                 weak_factory_.GetWeakPtr(),
                 file_path));
}

void URLFetcherCore::FileWriter::CreateTempFile() {
  DCHECK(core_->io_message_loop_proxy_->BelongsToCurrentThread());
  DCHECK(file_message_loop_proxy_.get());
  base::FileUtilProxy::CreateTemporary(
      file_message_loop_proxy_,
      0,  // No additional file flags.
      base::Bind(&URLFetcherCore::FileWriter::DidCreateTempFile,
                 weak_factory_.GetWeakPtr()));
}

void URLFetcherCore::FileWriter::WriteBuffer(int num_bytes) {
  DCHECK(core_->io_message_loop_proxy_->BelongsToCurrentThread());

  // Start writing to the file by setting the initial state
  // of |pending_bytes_| and |buffer_offset_| to indicate that the
  // entire buffer has not yet been written.
  pending_bytes_ = num_bytes;
  buffer_offset_ = 0;
  ContinueWrite(base::PLATFORM_FILE_OK, 0);
}

void URLFetcherCore::FileWriter::ContinueWrite(
    base::PlatformFileError error_code,
    int bytes_written) {
  DCHECK(core_->io_message_loop_proxy_->BelongsToCurrentThread());

  if (file_handle_ == base::kInvalidPlatformFileValue) {
    // While a write was being done on the file thread, a request
    // to close or disown the file occured on the IO thread.  At
    // this point a request to close the file is pending on the
    // file thread.
    return;
  }

  // Every code path that resets |core_->request_| should reset
  // |core->file_writer_| or cause the file writer to disown the file.  In the
  // former case, this callback can not be called, because the weak pointer to
  // |this| will be NULL. In the latter case, the check of |file_handle_| at the
  // start of this method ensures that we can not reach this point.
  CHECK(core_->request_.get());

  if (base::PLATFORM_FILE_OK != error_code) {
    error_code_ = error_code;
    RemoveFile();
    core_->delegate_loop_proxy_->PostTask(
        FROM_HERE,
        base::Bind(&URLFetcherCore::InformDelegateFetchIsComplete, core_));
    return;
  }

  total_bytes_written_ += bytes_written;
  buffer_offset_ += bytes_written;
  pending_bytes_ -= bytes_written;

  if (pending_bytes_ > 0) {
    base::FileUtilProxy::Write(
        file_message_loop_proxy_, file_handle_,
        total_bytes_written_,  // Append to the end
        (core_->buffer_->data() + buffer_offset_), pending_bytes_,
        base::Bind(&URLFetcherCore::FileWriter::ContinueWrite,
                   weak_factory_.GetWeakPtr()));
  } else {
    // Finished writing core_->buffer_ to the file. Read some more.
    core_->ReadResponse();
  }
}

void URLFetcherCore::FileWriter::DisownFile() {
  DCHECK(core_->io_message_loop_proxy_->BelongsToCurrentThread());

  // Disowning is done by the delegate's OnURLFetchComplete method.
  // The file should be closed by the time that method is called.
  DCHECK(file_handle_ == base::kInvalidPlatformFileValue);

  // Forget about any file by reseting the path.
  file_path_.clear();
}

void URLFetcherCore::FileWriter::CloseFileAndCompleteRequest() {
  DCHECK(core_->io_message_loop_proxy_->BelongsToCurrentThread());

  if (file_handle_ != base::kInvalidPlatformFileValue) {
    base::FileUtilProxy::Close(
        file_message_loop_proxy_, file_handle_,
        base::Bind(&URLFetcherCore::FileWriter::DidCloseFile,
                   weak_factory_.GetWeakPtr()));
    file_handle_ = base::kInvalidPlatformFileValue;
  }
}

void URLFetcherCore::FileWriter::DidCreateFile(
    const FilePath& file_path,
    base::PlatformFileError error_code,
    base::PassPlatformFile file_handle,
    bool created) {
  DidCreateFileInternal(file_path, error_code, file_handle);
}

void URLFetcherCore::FileWriter::DidCreateTempFile(
    base::PlatformFileError error_code,
    base::PassPlatformFile file_handle,
    const FilePath& file_path) {
  DidCreateFileInternal(file_path, error_code, file_handle);
}

void URLFetcherCore::FileWriter::DidCreateFileInternal(
    const FilePath& file_path,
    base::PlatformFileError error_code,
    base::PassPlatformFile file_handle) {
  DCHECK(core_->io_message_loop_proxy_->BelongsToCurrentThread());

  if (base::PLATFORM_FILE_OK != error_code) {
    error_code_ = error_code;
    RemoveFile();
    core_->delegate_loop_proxy_->PostTask(
        FROM_HERE,
        base::Bind(&URLFetcherCore::InformDelegateFetchIsComplete, core_));
    return;
  }

  file_path_ = file_path;
  file_handle_ = file_handle.ReleaseValue();
  total_bytes_written_ = 0;

  core_->io_message_loop_proxy_->PostTask(
      FROM_HERE,
      base::Bind(&URLFetcherCore::StartURLRequestWhenAppropriate, core_));
}

void URLFetcherCore::FileWriter::DidCloseFile(
    base::PlatformFileError error_code) {
  DCHECK(core_->io_message_loop_proxy_->BelongsToCurrentThread());

  if (base::PLATFORM_FILE_OK != error_code) {
    error_code_ = error_code;
    RemoveFile();
    core_->delegate_loop_proxy_->PostTask(
        FROM_HERE,
        base::Bind(&URLFetcherCore::InformDelegateFetchIsComplete, core_));
    return;
  }

  // If the file was successfully closed, then the URL request is complete.
  core_->RetryOrCompleteUrlFetch();
}

void URLFetcherCore::FileWriter::RemoveFile() {
  DCHECK(core_->io_message_loop_proxy_->BelongsToCurrentThread());

  // Close the file if it is open.
  if (file_handle_ != base::kInvalidPlatformFileValue) {
    base::FileUtilProxy::Close(
        file_message_loop_proxy_, file_handle_,
        base::FileUtilProxy::StatusCallback());  // No callback: Ignore errors.
    file_handle_ = base::kInvalidPlatformFileValue;
  }

  if (!file_path_.empty()) {
    base::FileUtilProxy::Delete(
        file_message_loop_proxy_, file_path_,
        false,  // No need to recurse, as the path is to a file.
        base::FileUtilProxy::StatusCallback());  // No callback: Ignore errors.
    DisownFile();
  }
}

static bool g_interception_enabled = false;

URLFetcherCore::URLFetcherCore(URLFetcher* fetcher,
                               const GURL& original_url,
                               URLFetcher::RequestType request_type,
                               content::URLFetcherDelegate* d)
    : fetcher_(fetcher),
      original_url_(original_url),
      request_type_(request_type),
      delegate_(d),
      delegate_loop_proxy_(
          base::MessageLoopProxy::current()),
      request_(NULL),
      load_flags_(net::LOAD_NORMAL),
      response_code_(URLFetcher::RESPONSE_CODE_INVALID),
      buffer_(new net::IOBuffer(kBufferSize)),
      render_process_id_(-1),
      render_view_id_(-1),
      was_fetched_via_proxy_(false),
      is_chunked_upload_(false),
      num_retries_(0),
      was_cancelled_(false),
      response_destination_(STRING),
      automatically_retry_on_5xx_(true),
      max_retries_(0),
      current_upload_bytes_(-1),
      current_response_bytes_(0),
      total_response_bytes_(-1) {
}

URLFetcherCore::~URLFetcherCore() {
  // |request_| should be NULL.  If not, it's unsafe to delete it here since we
  // may not be on the IO thread.
  DCHECK(!request_.get());
}

void URLFetcherCore::Start() {
  DCHECK(delegate_loop_proxy_);
  DCHECK(request_context_getter_) << "We need an URLRequestContext!";
  if (io_message_loop_proxy_) {
    DCHECK_EQ(io_message_loop_proxy_,
              request_context_getter_->GetIOMessageLoopProxy());
  } else {
    io_message_loop_proxy_ = request_context_getter_->GetIOMessageLoopProxy();
  }
  DCHECK(io_message_loop_proxy_.get()) << "We need an IO message loop proxy";

  io_message_loop_proxy_->PostTask(
      FROM_HERE, base::Bind(&URLFetcherCore::StartOnIOThread, this));
}

void URLFetcherCore::StartOnIOThread() {
  DCHECK(io_message_loop_proxy_->BelongsToCurrentThread());

  switch (response_destination_) {
    case STRING:
      StartURLRequestWhenAppropriate();
      break;

    case PERMANENT_FILE:
    case TEMP_FILE:
      DCHECK(file_message_loop_proxy_.get())
          << "Need to set the file message loop proxy.";

      file_writer_.reset(new FileWriter(this, file_message_loop_proxy_));

      // If the file is successfully created,
      // URLFetcherCore::StartURLRequestWhenAppropriate() will be called.
      switch (response_destination_) {
        case PERMANENT_FILE:
          file_writer_->CreateFileAtPath(response_destination_file_path_);
          break;
        case TEMP_FILE:
          file_writer_->CreateTempFile();
          break;
        default:
          NOTREACHED();
      }
      break;

    default:
      NOTREACHED();
  }
}

void URLFetcherCore::Stop() {
  if (delegate_loop_proxy_)  // May be NULL in tests.
    DCHECK(delegate_loop_proxy_->BelongsToCurrentThread());

  delegate_ = NULL;
  fetcher_ = NULL;
  if (io_message_loop_proxy_.get()) {
    io_message_loop_proxy_->PostTask(
        FROM_HERE, base::Bind(&URLFetcherCore::CancelURLRequest, this));
  }
}

void URLFetcherCore::SetUploadData(const std::string& upload_content_type,
                                   const std::string& upload_content) {
  DCHECK(!is_chunked_upload_);
  upload_content_type_ = upload_content_type;
  upload_content_ = upload_content;
}

void URLFetcherCore::SetChunkedUpload(const std::string& content_type) {
  DCHECK(is_chunked_upload_ ||
         (upload_content_type_.empty() &&
          upload_content_.empty()));
  upload_content_type_ = content_type;
  upload_content_.clear();
  is_chunked_upload_ = true;
}

void URLFetcherCore::AppendChunkToUpload(const std::string& content,
                                         bool is_last_chunk) {
  DCHECK(delegate_loop_proxy_);
  DCHECK(io_message_loop_proxy_.get());
  io_message_loop_proxy_->PostTask(
      FROM_HERE,
      base::Bind(&URLFetcherCore::CompleteAddingUploadDataChunk, this, content,
                 is_last_chunk));
}

void URLFetcherCore::SetLoadFlags(int load_flags) {
  load_flags_ = load_flags;
}

int URLFetcherCore::GetLoadFlags() const {
  return load_flags_;
}

void URLFetcherCore::SetReferrer(const std::string& referrer) {
  referrer_ = referrer;
}

void URLFetcherCore::SetExtraRequestHeaders(
    const std::string& extra_request_headers) {
  extra_request_headers_.Clear();
  extra_request_headers_.AddHeadersFromString(extra_request_headers);
}

void URLFetcherCore::AddExtraRequestHeader(const std::string& header_line) {
  extra_request_headers_.AddHeaderFromString(header_line);
}

void URLFetcherCore::GetExtraRequestHeaders(
    net::HttpRequestHeaders* headers) const {
  headers->CopyFrom(extra_request_headers_);
}

void URLFetcherCore::SetRequestContext(
    net::URLRequestContextGetter* request_context_getter) {
  DCHECK(!request_context_getter_);
  request_context_getter_ = request_context_getter;
}

void URLFetcherCore::AssociateWithRenderView(
    const GURL& first_party_for_cookies,
    int render_process_id,
    int render_view_id) {
  DCHECK(first_party_for_cookies_.is_empty());
  DCHECK_EQ(render_process_id_, -1);
  DCHECK_EQ(render_view_id_, -1);
  DCHECK_GE(render_process_id, 0);
  DCHECK_GE(render_view_id, 0);
  first_party_for_cookies_ = first_party_for_cookies;
  render_process_id_ = render_process_id;
  render_view_id_ = render_view_id;
}

void URLFetcherCore::SetAutomaticallyRetryOn5xx(bool retry) {
  automatically_retry_on_5xx_ = retry;
}

void URLFetcherCore::SetMaxRetries(int max_retries) {
  max_retries_ = max_retries;
}

int URLFetcherCore::GetMaxRetries() const {
  return max_retries_;
}


base::TimeDelta URLFetcherCore::GetBackoffDelay() const {
  return backoff_delay_;
}

void URLFetcherCore::SaveResponseToFileAtPath(
    const FilePath& file_path,
    scoped_refptr<base::MessageLoopProxy> file_message_loop_proxy) {
  DCHECK(delegate_loop_proxy_->BelongsToCurrentThread());
  file_message_loop_proxy_ = file_message_loop_proxy;
  response_destination_ = content::URLFetcherCore::PERMANENT_FILE;
  response_destination_file_path_ = file_path;
}

void URLFetcherCore::SaveResponseToTemporaryFile(
    scoped_refptr<base::MessageLoopProxy> file_message_loop_proxy) {
  DCHECK(delegate_loop_proxy_->BelongsToCurrentThread());
  file_message_loop_proxy_ = file_message_loop_proxy;
  response_destination_ = content::URLFetcherCore::TEMP_FILE;
}

net::HttpResponseHeaders* URLFetcherCore::GetResponseHeaders() const {
  return response_headers_;
}

// TODO(panayiotis): socket_address_ is written in the IO thread,
// if this is accessed in the UI thread, this could result in a race.
// Same for response_headers_ above and was_fetched_via_proxy_ below.
net::HostPortPair URLFetcherCore::GetSocketAddress() const {
  return socket_address_;
}

bool URLFetcherCore::WasFetchedViaProxy() const {
  return was_fetched_via_proxy_;
}

const GURL& URLFetcherCore::GetOriginalURL() const {
  return original_url_;
}

const GURL& URLFetcherCore::GetURL() const {
  return url_;
}

const net::URLRequestStatus& URLFetcherCore::GetStatus() const {
  return status_;
}

int URLFetcherCore::GetResponseCode() const {
  return response_code_;
}

const net::ResponseCookies& URLFetcherCore::GetCookies() const {
  return cookies_;
}

bool URLFetcherCore::FileErrorOccurred(
    base::PlatformFileError* out_error_code) const {

  // Can't have a file error if no file is being created or written to.
  if (!file_writer_.get())
    return false;

  base::PlatformFileError error_code = file_writer_->error_code();
  if (error_code == base::PLATFORM_FILE_OK)
    return false;

  *out_error_code = error_code;
  return true;
}

void URLFetcherCore::ReceivedContentWasMalformed() {
  DCHECK(delegate_loop_proxy_->BelongsToCurrentThread());
  if (io_message_loop_proxy_.get()) {
    io_message_loop_proxy_->PostTask(
        FROM_HERE, base::Bind(&URLFetcherCore::NotifyMalformedContent, this));
  }
}

bool URLFetcherCore::GetResponseAsString(
    std::string* out_response_string) const {
  if (response_destination_ != content::URLFetcherCore::STRING)
    return false;

  *out_response_string = data_;
  UMA_HISTOGRAM_MEMORY_KB("UrlFetcher.StringResponseSize",
                          (data_.length() / 1024));

  return true;
}

bool URLFetcherCore::GetResponseAsFilePath(bool take_ownership,
                                           FilePath* out_response_path) {
  DCHECK(delegate_loop_proxy_->BelongsToCurrentThread());
  const bool destination_is_file =
      response_destination_ == content::URLFetcherCore::TEMP_FILE ||
      response_destination_ == content::URLFetcherCore::PERMANENT_FILE;
  if (!destination_is_file || !file_writer_.get())
    return false;

  *out_response_path = file_writer_->file_path();

  if (take_ownership) {
    io_message_loop_proxy_->PostTask(
        FROM_HERE,
        base::Bind(&content::URLFetcherCore::DisownFile, this));
  }
  return true;
}

void URLFetcherCore::CancelAll() {
  g_registry.Get().CancelAll();
}

int URLFetcherCore::GetNumFetcherCores() {
  return g_registry.Get().size();
}

void URLFetcherCore::SetEnableInterceptionForTests(bool enabled) {
  g_interception_enabled = enabled;
}

void URLFetcherCore::OnResponseStarted(net::URLRequest* request) {
  DCHECK_EQ(request, request_.get());
  DCHECK(io_message_loop_proxy_->BelongsToCurrentThread());
  if (request_->status().is_success()) {
    response_code_ = request_->GetResponseCode();
    response_headers_ = request_->response_headers();
    socket_address_ = request_->GetSocketAddress();
    was_fetched_via_proxy_ = request_->was_fetched_via_proxy();
    total_response_bytes_ = request_->GetExpectedContentSize();
  }

  ReadResponse();
}

void URLFetcherCore::CompleteAddingUploadDataChunk(
    const std::string& content, bool is_last_chunk) {
  DCHECK(is_chunked_upload_);
  DCHECK(request_.get());
  DCHECK(!content.empty());
  request_->AppendChunkToUpload(content.data(),
                                static_cast<int>(content.length()),
                                is_last_chunk);
}

// Return true if the write was done and reading may continue.
// Return false if the write is pending, and the next read will
// be done later.
bool URLFetcherCore::WriteBuffer(int num_bytes) {
  bool write_complete = false;
  switch (response_destination_) {
    case STRING:
      data_.append(buffer_->data(), num_bytes);
      write_complete = true;
      break;

    case PERMANENT_FILE:
    case TEMP_FILE:
      file_writer_->WriteBuffer(num_bytes);
      // WriteBuffer() sends a request the file thread.
      // The write is not done yet.
      write_complete = false;
      break;

    default:
      NOTREACHED();
  }
  return write_complete;
}

void URLFetcherCore::OnReadCompleted(net::URLRequest* request,
                                     int bytes_read) {
  DCHECK(request == request_);
  DCHECK(io_message_loop_proxy_->BelongsToCurrentThread());

  url_ = request->url();
  url_throttler_entry_ =
      net::URLRequestThrottlerManager::GetInstance()->RegisterRequestUrl(url_);

  bool waiting_on_write = false;
  do {
    if (!request_->status().is_success() || bytes_read <= 0)
      break;

    current_response_bytes_ += bytes_read;
    InformDelegateDownloadProgress();

    if (!WriteBuffer(bytes_read)) {
      // If WriteBuffer() returns false, we have a pending write to
      // wait on before reading further.
      waiting_on_write = true;
      break;
    }
  } while (request_->Read(buffer_, kBufferSize, &bytes_read));

  const net::URLRequestStatus status = request_->status();

  if (status.is_success())
    request_->GetResponseCookies(&cookies_);

  // See comments re: HEAD requests in ReadResponse().
  if ((!status.is_io_pending() && !waiting_on_write) ||
      (request_type_ == URLFetcher::HEAD)) {
    status_ = status;
    ReleaseRequest();

    // If a file is open, close it.
    if (file_writer_.get()) {
      // If the file is open, close it.  After closing the file,
      // RetryOrCompleteUrlFetch() will be called.
      file_writer_->CloseFileAndCompleteRequest();
    } else {
      // Otherwise, complete or retry the URL request directly.
      RetryOrCompleteUrlFetch();
    }
  }
}

void URLFetcherCore::RetryOrCompleteUrlFetch() {
  DCHECK(io_message_loop_proxy_->BelongsToCurrentThread());
  base::TimeDelta backoff_delay;

  // Checks the response from server.
  if (response_code_ >= 500 ||
      status_.error() == net::ERR_TEMPORARILY_THROTTLED) {
    // When encountering a server error, we will send the request again
    // after backoff time.
    ++num_retries_;

    // Note that backoff_delay may be 0 because (a) the URLRequestThrottler
    // code does not necessarily back off on the first error, and (b) it
    // only backs off on some of the 5xx status codes.
    base::TimeTicks backoff_release_time = GetBackoffReleaseTime();
    backoff_delay = backoff_release_time - base::TimeTicks::Now();
    if (backoff_delay < base::TimeDelta())
      backoff_delay = base::TimeDelta();

    if (automatically_retry_on_5xx_ && num_retries_ <= max_retries_) {
      StartOnIOThread();
      return;
    }
  } else {
    backoff_delay = base::TimeDelta();
  }
  request_context_getter_ = NULL;
  render_process_id_ = -1;
  render_view_id_ = -1;
  first_party_for_cookies_ = GURL();
  bool posted = delegate_loop_proxy_->PostTask(
      FROM_HERE,
      base::Bind(&URLFetcherCore::OnCompletedURLRequest, this, backoff_delay));

  // If the delegate message loop does not exist any more, then the delegate
  // should be gone too.
  DCHECK(posted || !delegate_);
}

void URLFetcherCore::ReadResponse() {
  // Some servers may treat HEAD requests as GET requests.  To free up the
  // network connection as soon as possible, signal that the request has
  // completed immediately, without trying to read any data back (all we care
  // about is the response code and headers, which we already have).
  int bytes_read = 0;
  if (request_->status().is_success() && (request_type_ != URLFetcher::HEAD))
    request_->Read(buffer_, kBufferSize, &bytes_read);
  OnReadCompleted(request_.get(), bytes_read);
}

void URLFetcherCore::DisownFile() {
  file_writer_->DisownFile();
}

void URLFetcherCore::StartURLRequest() {
  DCHECK(io_message_loop_proxy_->BelongsToCurrentThread());

  if (was_cancelled_) {
    // Since StartURLRequest() is posted as a *delayed* task, it may
    // run after the URLFetcher was already stopped.
    return;
  }

  DCHECK(request_context_getter_);
  DCHECK(!request_.get());

  g_registry.Get().AddURLFetcherCore(this);
  current_response_bytes_ = 0;
  request_.reset(new net::URLRequest(original_url_, this));
  int flags = request_->load_flags() | load_flags_;
  if (!g_interception_enabled)
    flags = flags | net::LOAD_DISABLE_INTERCEPT;

  if (is_chunked_upload_)
    request_->EnableChunkedUpload();
  request_->set_load_flags(flags);
  request_->set_context(request_context_getter_->GetURLRequestContext());
  request_->set_referrer(referrer_);
  request_->set_first_party_for_cookies(first_party_for_cookies_.is_empty() ?
      original_url_ : first_party_for_cookies_);
  if (render_process_id_ != -1 && render_view_id_ != -1) {
    request_->SetUserData(
        URLRequestUserData::kUserDataKey,
        new URLRequestUserData(render_process_id_, render_view_id_));
  }

  switch (request_type_) {
    case URLFetcher::GET:
      break;

    case URLFetcher::POST:
    case URLFetcher::PUT:
      DCHECK(!upload_content_.empty() || is_chunked_upload_);
      DCHECK(!upload_content_type_.empty());

      request_->set_method(request_type_ == URLFetcher::POST ? "POST" : "PUT");
      extra_request_headers_.SetHeader(net::HttpRequestHeaders::kContentType,
                                       upload_content_type_);
      if (!upload_content_.empty()) {
        request_->AppendBytesToUpload(
            upload_content_.data(), static_cast<int>(upload_content_.length()));
      }

      current_upload_bytes_ = -1;
      // TODO(kinaba): http://crbug.com/118103. Implement upload callback in the
      // net:: layer and avoid using timer here.
      upload_progress_checker_timer_.reset(
          new base::RepeatingTimer<URLFetcherCore>());
      upload_progress_checker_timer_->Start(
          FROM_HERE,
          base::TimeDelta::FromMilliseconds(kUploadProgressTimerInterval),
          this,
          &URLFetcherCore::InformDelegateUploadProgress);
      break;

    case URLFetcher::HEAD:
      request_->set_method("HEAD");
      break;

    case URLFetcher::DELETE_REQUEST:
      request_->set_method("DELETE");
      break;

    default:
      NOTREACHED();
  }

  if (!extra_request_headers_.IsEmpty())
    request_->SetExtraRequestHeaders(extra_request_headers_);

  // There might be data left over from a previous request attempt.
  data_.clear();

  // If we are writing the response to a file, the only caller
  // of this function should have created it and not written yet.
  DCHECK(!file_writer_.get() || file_writer_->total_bytes_written() == 0);

  request_->Start();
}

void URLFetcherCore::StartURLRequestWhenAppropriate() {
  DCHECK(io_message_loop_proxy_->BelongsToCurrentThread());

  if (was_cancelled_)
    return;

  if (original_url_throttler_entry_ == NULL) {
    original_url_throttler_entry_ =
        net::URLRequestThrottlerManager::GetInstance()->RegisterRequestUrl(
            original_url_);
  }

  int64 delay = original_url_throttler_entry_->ReserveSendingTimeForNextRequest(
      GetBackoffReleaseTime());
  if (delay == 0) {
    StartURLRequest();
  } else {
    MessageLoop::current()->PostDelayedTask(
        FROM_HERE, base::Bind(&URLFetcherCore::StartURLRequest, this),
        base::TimeDelta::FromMilliseconds(delay));
  }
}

void URLFetcherCore::CancelURLRequest() {
  DCHECK(io_message_loop_proxy_->BelongsToCurrentThread());

  if (request_.get()) {
    request_->Cancel();
    ReleaseRequest();
  }
  // Release the reference to the request context. There could be multiple
  // references to URLFetcher::Core at this point so it may take a while to
  // delete the object, but we cannot delay the destruction of the request
  // context.
  request_context_getter_ = NULL;
  render_process_id_ = -1;
  render_view_id_ = -1;
  first_party_for_cookies_ = GURL();
  was_cancelled_ = true;
  file_writer_.reset();
}

void URLFetcherCore::OnCompletedURLRequest(
    base::TimeDelta backoff_delay) {
  DCHECK(delegate_loop_proxy_->BelongsToCurrentThread());

  // Save the status and backoff_delay so that delegates can read it.
  if (delegate_) {
    backoff_delay_ = backoff_delay;
    InformDelegateFetchIsComplete();
  }
}

void URLFetcherCore::InformDelegateUploadProgress() {
  DCHECK(io_message_loop_proxy_->BelongsToCurrentThread());
  if (request_.get()) {
    int64 current = request_->GetUploadProgress();
    if (current_upload_bytes_ != current) {
      current_upload_bytes_ = current;
      int64 total = -1;
      if (!is_chunked_upload_)
        total = static_cast<int64>(upload_content_.size());
      delegate_loop_proxy_->PostTask(
          FROM_HERE,
          base::Bind(
              &URLFetcherCore::InformDelegateUploadProgressInDelegateThread,
              this, current, total));
    }
  }
}

void URLFetcherCore::InformDelegateUploadProgressInDelegateThread(
    int64 current, int64 total) {
  DCHECK(delegate_loop_proxy_->BelongsToCurrentThread());
  if (delegate_)
    delegate_->OnURLFetchUploadProgress(fetcher_, current, total);
}

void URLFetcherCore::InformDelegateDownloadProgress() {
  DCHECK(io_message_loop_proxy_->BelongsToCurrentThread());
  delegate_loop_proxy_->PostTask(
      FROM_HERE,
      base::Bind(
          &URLFetcherCore::InformDelegateDownloadProgressInDelegateThread,
          this, current_response_bytes_, total_response_bytes_));
}

void URLFetcherCore::InformDelegateDownloadProgressInDelegateThread(
    int64 current, int64 total) {
  DCHECK(delegate_loop_proxy_->BelongsToCurrentThread());
  if (delegate_)
    delegate_->OnURLFetchDownloadProgress(fetcher_, current, total);
}

void URLFetcherCore::InformDelegateFetchIsComplete() {
  DCHECK(delegate_loop_proxy_->BelongsToCurrentThread());
  if (delegate_)
    delegate_->OnURLFetchComplete(fetcher_);
}

void URLFetcherCore::NotifyMalformedContent() {
  DCHECK(io_message_loop_proxy_->BelongsToCurrentThread());
  if (url_throttler_entry_ != NULL) {
    int status_code = response_code_;
    if (status_code == URLFetcher::RESPONSE_CODE_INVALID) {
      // The status code will generally be known by the time clients
      // call the |ReceivedContentWasMalformed()| function (which ends up
      // calling the current function) but if it's not, we need to assume
      // the response was successful so that the total failure count
      // used to calculate exponential back-off goes up.
      status_code = 200;
    }
    url_throttler_entry_->ReceivedContentWasMalformed(status_code);
  }
}

void URLFetcherCore::ReleaseRequest() {
  upload_progress_checker_timer_.reset();
  request_.reset();
  g_registry.Get().RemoveURLFetcherCore(this);
}

base::TimeTicks URLFetcherCore::GetBackoffReleaseTime() {
  DCHECK(io_message_loop_proxy_->BelongsToCurrentThread());
  DCHECK(original_url_throttler_entry_ != NULL);

  base::TimeTicks original_url_backoff =
      original_url_throttler_entry_->GetExponentialBackoffReleaseTime();
  base::TimeTicks destination_url_backoff;
  if (url_throttler_entry_ != NULL &&
      original_url_throttler_entry_ != url_throttler_entry_) {
    destination_url_backoff =
        url_throttler_entry_->GetExponentialBackoffReleaseTime();
  }

  return original_url_backoff > destination_url_backoff ?
      original_url_backoff : destination_url_backoff;
}

}  // namespace content
