// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_NET_URL_FETCHER_CORE_H_
#define CONTENT_COMMON_NET_URL_FETCHER_CORE_H_
#pragma once

#include <set>
#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/file_path.h"
#include "base/lazy_instance.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/platform_file.h"
#include "base/timer.h"
#include "content/public/common/url_fetcher.h"
#include "googleurl/src/gurl.h"
#include "net/base/host_port_pair.h"
#include "net/http/http_request_headers.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_status.h"

namespace base {
class MessageLoopProxy;
}  // namespace base

namespace net {
class HttpResponseHeaders;
class IOBuffer;
class URLRequestContextGetter;
class URLRequestThrottlerEntryInterface;
}  // namespace net

namespace content {

class URLFetcherDelegate;

class URLFetcherCore
    : public base::RefCountedThreadSafe<URLFetcherCore>,
      public net::URLRequest::Delegate {
 public:
  URLFetcherCore(URLFetcher* fetcher,
                 const GURL& original_url,
                 URLFetcher::RequestType request_type,
                 URLFetcherDelegate* d);

  // Starts the load.  It's important that this not happen in the constructor
  // because it causes the IO thread to begin AddRef()ing and Release()ing
  // us.  If our caller hasn't had time to fully construct us and take a
  // reference, the IO thread could interrupt things, run a task, Release()
  // us, and destroy us, leaving the caller with an already-destroyed object
  // when construction finishes.
  void Start();

  // Stops any in-progress load and ensures no callback will happen.  It is
  // safe to call this multiple times.
  void Stop();

  // URLFetcher-like functions.

  // For POST requests, set |content_type| to the MIME type of the
  // content and set |content| to the data to upload.
  void SetUploadData(const std::string& upload_content_type,
                     const std::string& upload_content);
  void SetChunkedUpload(const std::string& upload_content_type);
  // Adds a block of data to be uploaded in a POST body. This can only be
  // called after Start().
  void AppendChunkToUpload(const std::string& data, bool is_last_chunk);
  // |flags| are flags to apply to the load operation--these should be
  // one or more of the LOAD_* flags defined in net/base/load_flags.h.
  void SetLoadFlags(int load_flags);
  int GetLoadFlags() const;
  void SetReferrer(const std::string& referrer);
  void SetExtraRequestHeaders(const std::string& extra_request_headers);
  void AddExtraRequestHeader(const std::string& header_line);
  void GetExtraRequestHeaders(net::HttpRequestHeaders* headers) const;
  void SetRequestContext(net::URLRequestContextGetter* request_context_getter);
  // TODO(akalin): When we move this class to net/, this has to stay
  // in content/ somehow.
  void AssociateWithRenderView(const GURL& first_party_for_cookies,
                               int render_process_id,
                               int render_view_id);
  void SetAutomaticallyRetryOn5xx(bool retry);
  void SetMaxRetries(int max_retries);
  int GetMaxRetries() const;
  base::TimeDelta GetBackoffDelay() const;
  void SaveResponseToFileAtPath(
      const FilePath& file_path,
      scoped_refptr<base::MessageLoopProxy> file_message_loop_proxy);
  void SaveResponseToTemporaryFile(
      scoped_refptr<base::MessageLoopProxy> file_message_loop_proxy);
  net::HttpResponseHeaders* GetResponseHeaders() const;
  net::HostPortPair GetSocketAddress() const;
  bool WasFetchedViaProxy() const;
  const GURL& GetOriginalURL() const;
  const GURL& GetURL() const;
  const net::URLRequestStatus& GetStatus() const;
  int GetResponseCode() const;
  const net::ResponseCookies& GetCookies() const;
  bool FileErrorOccurred(base::PlatformFileError* out_error_code) const;
  // Reports that the received content was malformed (i.e. failed parsing
  // or validation).  This makes the throttling logic that does exponential
  // back-off when servers are having problems treat the current request as
  // a failure.  Your call to this method will be ignored if your request is
  // already considered a failure based on the HTTP response code or response
  // headers.
  void ReceivedContentWasMalformed();
  bool GetResponseAsString(std::string* out_response_string) const;
  bool GetResponseAsFilePath(bool take_ownership,
                             FilePath* out_response_path);

  // Overridden from net::URLRequest::Delegate:
  virtual void OnResponseStarted(
      net::URLRequest* request) OVERRIDE;
  virtual void OnReadCompleted(
      net::URLRequest* request, int bytes_read) OVERRIDE;

  URLFetcherDelegate* delegate() const { return delegate_; }
  static void CancelAll();
  static int GetNumFetcherCores();
  static void SetEnableInterceptionForTests(bool enabled);

 private:
  friend class base::RefCountedThreadSafe<URLFetcherCore>;

  // How should the response be stored?
  enum ResponseDestinationType {
    STRING,  // Default: In a std::string
    PERMANENT_FILE,  // Write to a permanent file.
    TEMP_FILE,  // Write to a temporary file.
  };

  class Registry {
   public:
    Registry();
    ~Registry();

    void AddURLFetcherCore(URLFetcherCore* core);
    void RemoveURLFetcherCore(URLFetcherCore* core);

    void CancelAll();

    int size() const {
      return fetchers_.size();
    }

   private:
    std::set<URLFetcherCore*> fetchers_;

    DISALLOW_COPY_AND_ASSIGN(Registry);
  };

  // Class FileWriter encapsulates all state involved in writing
  // response bytes to a file. It is only used if
  // |URLFetcherCore::response_destination_| == TEMP_FILE ||
  // |URLFetcherCore::response_destination_| == PERMANENT_FILE.  Each
  // instance of FileWriter is owned by a URLFetcherCore, which
  // manages its lifetime and never transfers ownership.  While
  // writing to a file, all function calls happen on the IO thread.
  class FileWriter {
   public:
    FileWriter(URLFetcherCore* core,
               scoped_refptr<base::MessageLoopProxy> file_message_loop_proxy);

    ~FileWriter();
    void CreateFileAtPath(const FilePath& file_path);
    void CreateTempFile();

    // Record |num_bytes_| response bytes in |core_->buffer_| to the file.
    void WriteBuffer(int num_bytes);

    // Called when a write has been done.  Continues writing if there are
    // any more bytes to write.  Otherwise, initiates a read in core_.
    void ContinueWrite(base::PlatformFileError error_code, int bytes_written);

    // Drop ownership of the file at |file_path_|.
    // This class will not delete it or write to it again.
    void DisownFile();

    // Close the file if it is open.
    void CloseFileAndCompleteRequest();

    // Remove the file if we have created one.
    void RemoveFile();

    const FilePath& file_path() const { return file_path_; }
    int64 total_bytes_written() { return total_bytes_written_; }
    base::PlatformFileError error_code() const { return error_code_; }

   private:
    // Callback which gets the result of a permanent file creation.
    void DidCreateFile(const FilePath& file_path,
                       base::PlatformFileError error_code,
                       base::PassPlatformFile file_handle,
                       bool created);
    // Callback which gets the result of a temporary file creation.
    void DidCreateTempFile(base::PlatformFileError error_code,
                           base::PassPlatformFile file_handle,
                           const FilePath& file_path);
    // This method is used to implement DidCreateFile and DidCreateTempFile.
    void DidCreateFileInternal(const FilePath& file_path,
                               base::PlatformFileError error_code,
                               base::PassPlatformFile file_handle);

    // Callback which gets the result of closing the file.
    void DidCloseFile(base::PlatformFileError error);

    // The URLFetcherCore which instantiated this class.
    URLFetcherCore* core_;

    // The last error encountered on a file operation.  base::PLATFORM_FILE_OK
    // if no error occurred.
    base::PlatformFileError error_code_;

    // Callbacks are created for use with base::FileUtilProxy.
    base::WeakPtrFactory<URLFetcherCore::FileWriter> weak_factory_;

    // Message loop on which file operations should happen.
    scoped_refptr<base::MessageLoopProxy> file_message_loop_proxy_;

    // Path to the file.  This path is empty when there is no file.
    FilePath file_path_;

    // Handle to the file.
    base::PlatformFile file_handle_;

    // We always append to the file.  Track the total number of bytes
    // written, so that writes know the offset to give.
    int64 total_bytes_written_;

    // How many bytes did the last Write() try to write?  Needed so
    // that if not all the bytes get written on a Write(), we can
    // call Write() again with the rest.
    int pending_bytes_;

    // When writing, how many bytes from the buffer have been successfully
    // written so far?
    int buffer_offset_;
  };

  virtual ~URLFetcherCore();

  // Wrapper functions that allow us to ensure actions happen on the right
  // thread.
  void StartOnIOThread();
  void StartURLRequest();
  void StartURLRequestWhenAppropriate();
  void CancelURLRequest();
  void OnCompletedURLRequest(base::TimeDelta backoff_delay);
  void InformDelegateFetchIsComplete();
  void NotifyMalformedContent();
  void RetryOrCompleteUrlFetch();

  // Deletes the request, removes it from the registry, and removes the
  // destruction observer.
  void ReleaseRequest();

  // Returns the max value of exponential back-off release time for
  // |original_url_| and |url_|.
  base::TimeTicks GetBackoffReleaseTime();

  void CompleteAddingUploadDataChunk(const std::string& data,
                                     bool is_last_chunk);

  // Store the response bytes in |buffer_| in the container indicated by
  // |response_destination_|. Return true if the write has been
  // done, and another read can overwrite |buffer_|.  If this function
  // returns false, it will post a task that will read more bytes once the
  // write is complete.
  bool WriteBuffer(int num_bytes);

  // Read response bytes from the request.
  void ReadResponse();

  // Drop ownership of any file managed by |file_path_|.
  void DisownFile();

  // Notify Delegate about the progress of upload/download.
  void InformDelegateUploadProgress();
  void InformDelegateUploadProgressInDelegateThread(int64 current, int64 total);
  void InformDelegateDownloadProgress();
  void InformDelegateDownloadProgressInDelegateThread(int64 current,
                                                      int64 total);

  URLFetcher* fetcher_;              // Corresponding fetcher object
  GURL original_url_;                // The URL we were asked to fetch
  GURL url_;                         // The URL we eventually wound up at
  URLFetcher::RequestType request_type_;  // What type of request is this?
  net::URLRequestStatus status_;     // Status of the request
  URLFetcherDelegate* delegate_;     // Object to notify on completion
  scoped_refptr<base::MessageLoopProxy> delegate_loop_proxy_;
                                     // Message loop proxy of the creating
                                     // thread.
  scoped_refptr<base::MessageLoopProxy> io_message_loop_proxy_;
                                     // The message loop proxy for the thread
                                     // on which the request IO happens.
  scoped_refptr<base::MessageLoopProxy> file_message_loop_proxy_;
                                     // The message loop proxy for the thread
                                     // on which file access happens.
  scoped_ptr<net::URLRequest> request_;   // The actual request this wraps
  int load_flags_;                   // Flags for the load operation
  int response_code_;                // HTTP status code for the request
  std::string data_;                 // Results of the request, when we are
                                     // storing the response as a string.
  scoped_refptr<net::IOBuffer> buffer_;
                                     // Read buffer
  scoped_refptr<net::URLRequestContextGetter> request_context_getter_;
                                     // Cookie/cache info for the request
  int render_process_id_;            // The RenderView associated with the
  int render_view_id_;               // request
  GURL first_party_for_cookies_;     // The first party URL for the request
  net::ResponseCookies cookies_;     // Response cookies
  net::HttpRequestHeaders extra_request_headers_;
  scoped_refptr<net::HttpResponseHeaders> response_headers_;
  bool was_fetched_via_proxy_;
  net::HostPortPair socket_address_;

  std::string upload_content_;       // HTTP POST payload
  std::string upload_content_type_;  // MIME type of POST payload
  std::string referrer_;             // HTTP Referer header value
  bool is_chunked_upload_;           // True if using chunked transfer encoding

  // Used to determine how long to wait before making a request or doing a
  // retry.
  // Both of them can only be accessed on the IO thread.
  // We need not only the throttler entry for |original_URL|, but also the one
  // for |url|. For example, consider the case that URL A redirects to URL B,
  // for which the server returns a 500 response. In this case, the exponential
  // back-off release time of URL A won't increase. If we retry without
  // considering the back-off constraint of URL B, we may send out too many
  // requests for URL A in a short period of time.
  scoped_refptr<net::URLRequestThrottlerEntryInterface>
      original_url_throttler_entry_;
  scoped_refptr<net::URLRequestThrottlerEntryInterface> url_throttler_entry_;

  // |num_retries_| indicates how many times we've failed to successfully
  // fetch this URL.  Once this value exceeds the maximum number of retries
  // specified by the owner URLFetcher instance, we'll give up.
  int num_retries_;

  // True if the URLFetcher has been cancelled.
  bool was_cancelled_;

  // If writing results to a file, |file_writer_| will manage creation,
  // writing, and destruction of that file.
  scoped_ptr<FileWriter> file_writer_;

  // Where should responses be saved?
  ResponseDestinationType response_destination_;

  // Path to the file where the response is written.
  FilePath response_destination_file_path_;

  // If |automatically_retry_on_5xx_| is false, 5xx responses will be
  // propagated to the observer, if it is true URLFetcher will automatically
  // re-execute the request, after the back-off delay has expired.
  // true by default.
  bool automatically_retry_on_5xx_;
  // Maximum retries allowed.
  int max_retries_;
  // Back-off time delay. 0 by default.
  base::TimeDelta backoff_delay_;

  // Timer to poll the progress of uploading for POST and PUT requests.
  // When crbug.com/119629 is fixed, scoped_ptr is not necessary here.
  scoped_ptr<base::RepeatingTimer<URLFetcherCore> >
      upload_progress_checker_timer_;
  // Number of bytes sent so far.
  int64 current_upload_bytes_;
  // Number of bytes received so far.
  int64 current_response_bytes_;
  // Total expected bytes to receive (-1 if it cannot be determined).
  int64 total_response_bytes_;

  static base::LazyInstance<Registry> g_registry;

  DISALLOW_COPY_AND_ASSIGN(URLFetcherCore);
};

}  // namespace content

#endif  // CONTENT_COMMON_NET_URL_FETCHER_CORE_H_
