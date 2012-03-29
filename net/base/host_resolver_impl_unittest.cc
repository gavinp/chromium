// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/host_resolver_impl.h"

#include <string>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_vector.h"
#include "base/message_loop.h"
#include "base/stl_util.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "base/test/test_timeouts.h"
#include "base/time.h"
#include "net/base/address_list.h"
#include "net/base/completion_callback.h"
#include "net/base/host_cache.h"
#include "net/base/mock_host_resolver.h"
#include "net/base/net_errors.h"
#include "net/base/net_log_unittest.h"
#include "net/base/net_util.h"
#include "net/base/sys_addrinfo.h"
#include "net/base/test_completion_callback.h"
#include "net/dns/dns_client.h"
#include "net/dns/dns_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace {

using base::TimeDelta;
using base::TimeTicks;

const size_t kMaxJobs = 10u;
const size_t kMaxRetryAttempts = 4u;

PrioritizedDispatcher::Limits DefaultLimits() {
  PrioritizedDispatcher::Limits limits(NUM_PRIORITIES, kMaxJobs);
  return limits;
}

HostResolverImpl::ProcTaskParams DefaultParams(
    HostResolverProc* resolver_proc) {
  return HostResolverImpl::ProcTaskParams(resolver_proc,
                                          kMaxRetryAttempts);
}

HostResolverImpl* CreateHostResolverImpl(HostResolverProc* resolver_proc) {
  return new HostResolverImpl(
      HostCache::CreateDefaultCache(),
      DefaultLimits(),
      DefaultParams(resolver_proc),
      scoped_ptr<DnsConfigService>(NULL),
      NULL);
}

HostResolverImpl* CreateHostResolverImplWithDnsConfig(
    HostResolverProc* resolver_proc,
    scoped_ptr<DnsConfigService> config_service) {
  return new HostResolverImpl(
      HostCache::CreateDefaultCache(),
      DefaultLimits(),
      DefaultParams(resolver_proc),
      config_service.Pass(),
      NULL);
}

// This HostResolverImpl will only allow 1 outstanding resolve at a time.
HostResolverImpl* CreateSerialHostResolverImpl(
    HostResolverProc* resolver_proc) {
  HostResolverImpl::ProcTaskParams params = DefaultParams(resolver_proc);
  params.max_retry_attempts = 0u;

  PrioritizedDispatcher::Limits limits(NUM_PRIORITIES, 1);

  return new HostResolverImpl(HostCache::CreateDefaultCache(),
                              limits,
                              params,
                              scoped_ptr<DnsConfigService>(NULL),
                              NULL);
}

// Helper to create a HostResolver::RequestInfo.
HostResolver::RequestInfo CreateResolverRequest(
    const std::string& hostname,
    RequestPriority priority) {
  HostResolver::RequestInfo info(HostPortPair(hostname, 80));
  info.set_priority(priority);
  return info;
}

// Helper to create a HostResolver::RequestInfo.
HostResolver::RequestInfo CreateResolverRequestForAddressFamily(
    const std::string& hostname,
    RequestPriority priority,
    AddressFamily address_family) {
  HostResolver::RequestInfo info(HostPortPair(hostname, 80));
  info.set_priority(priority);
  info.set_address_family(address_family);
  return info;
}

// Using WaitingHostResolverProc you can simulate very long lookups.
class WaitingHostResolverProc : public HostResolverProc {
 public:
  explicit WaitingHostResolverProc(HostResolverProc* previous)
      : HostResolverProc(previous),
        is_waiting_(false, false),
        is_signaled_(false, false) {}

  // Waits until a call to |Resolve| is blocked. It is recommended to always
  // |Wait| before |Signal|, and required if issuing a series of two or more
  // calls to |Signal|, because |WaitableEvent| does not count the number of
  // signals.
  void Wait() {
    is_waiting_.Wait();
  }

  // Signals a waiting call to |Resolve|.
  void Signal() {
    is_signaled_.Signal();
  }

  // HostResolverProc methods:
  virtual int Resolve(const std::string& host,
                      AddressFamily address_family,
                      HostResolverFlags host_resolver_flags,
                      AddressList* addrlist,
                      int* os_error) OVERRIDE {
    is_waiting_.Signal();
    is_signaled_.Wait();
    return ResolveUsingPrevious(host, address_family, host_resolver_flags,
                                addrlist, os_error);
  }

 private:
  virtual ~WaitingHostResolverProc() {}
  base::WaitableEvent is_waiting_;
  base::WaitableEvent is_signaled_;
};

// A variant of WaitingHostResolverProc that pushes each host mapped into a
// list.
// (and uses a manual-reset event rather than auto-reset).
class CapturingHostResolverProc : public HostResolverProc {
 public:
  struct CaptureEntry {
    CaptureEntry(const std::string& hostname, AddressFamily address_family)
        : hostname(hostname), address_family(address_family) {}
    std::string hostname;
    AddressFamily address_family;
  };

  typedef std::vector<CaptureEntry> CaptureList;

  explicit CapturingHostResolverProc(HostResolverProc* previous)
      : HostResolverProc(previous), event_(true, false) {
  }

  void Signal() {
    event_.Signal();
  }

  virtual int Resolve(const std::string& hostname,
                      AddressFamily address_family,
                      HostResolverFlags host_resolver_flags,
                      AddressList* addrlist,
                      int* os_error) OVERRIDE {
    event_.Wait();
    {
      base::AutoLock l(lock_);
      capture_list_.push_back(CaptureEntry(hostname, address_family));
    }
    return ResolveUsingPrevious(hostname, address_family,
                                host_resolver_flags, addrlist, os_error);
  }

  CaptureList GetCaptureList() const {
    CaptureList copy;
    {
      base::AutoLock l(lock_);
      copy = capture_list_;
    }
    return copy;
  }

 private:
  ~CapturingHostResolverProc() {}

  CaptureList capture_list_;
  mutable base::Lock lock_;
  base::WaitableEvent event_;
};

// A variant of WaitingHostResolverProc which waits for a specific number of
// requests.
class CountingHostResolverProc : public HostResolverProc {
 public:
  explicit CountingHostResolverProc(HostResolverProc* previous)
      : HostResolverProc(previous),
        num_requests_waiting_(0),
        num_slots_available_(0),
        requests_waiting_(&lock_),
        slots_available_(&lock_) {}

  // Waits until |count| calls to |Resolve| are blocked. Returns false when
  // timed out.
  bool WaitFor(unsigned count) {
    base::AutoLock lock(lock_);
    base::Time start_time = base::Time::Now();
    while (num_requests_waiting_ < count) {
      requests_waiting_.TimedWait(TestTimeouts::action_timeout());
      if (base::Time::Now() > start_time + TestTimeouts::action_timeout())
        return false;
    }
    return true;
  }

  // Signals |count| waiting calls to |Resolve|. First come first served.
  void SignalMultiple(unsigned count) {
    base::AutoLock lock(lock_);
    num_slots_available_ += count;
    slots_available_.Broadcast();
  }

  // Signals all waiting calls to |Resolve|. Beware of races.
  void SignalAll() {
    base::AutoLock lock(lock_);
    num_slots_available_ += num_requests_waiting_;
    slots_available_.Broadcast();
  }

  // HostResolverProc methods:
  virtual int Resolve(const std::string& host,
                      AddressFamily address_family,
                      HostResolverFlags host_resolver_flags,
                      AddressList* addrlist,
                      int* os_error) OVERRIDE {
    {
      base::AutoLock lock(lock_);
      ++num_requests_waiting_;
      requests_waiting_.Broadcast();
      while (!num_slots_available_)
        slots_available_.Wait();
      --num_slots_available_;
      --num_requests_waiting_;
    }
    return ResolveUsingPrevious(host, address_family, host_resolver_flags,
                                addrlist, os_error);
  }

 private:
  virtual ~CountingHostResolverProc() {}
  unsigned num_requests_waiting_;
  unsigned num_slots_available_;
  base::Lock lock_;
  base::ConditionVariable requests_waiting_;
  base::ConditionVariable slots_available_;
};

// This resolver function creates an IPv4 address, whose numeral value
// describes a hash of the requested hostname, and the value of the requested
// address_family.
//
// The resolved address for (hostname, address_family) will take the form:
//    192.x.y.z
//
// Where:
//   x = length of hostname
//   y = ASCII value of hostname[0]
//   z = value of address_family
//
class EchoingHostResolverProc : public HostResolverProc {
 public:
  EchoingHostResolverProc() : HostResolverProc(NULL) {}

  virtual int Resolve(const std::string& hostname,
                      AddressFamily address_family,
                      HostResolverFlags host_resolver_flags,
                      AddressList* addrlist,
                      int* os_error) OVERRIDE {
    // Encode the request's hostname and address_family in the output address.
    std::string ip_literal = base::StringPrintf("192.%d.%d.%d",
        static_cast<int>(hostname.size()),
        static_cast<int>(hostname[0]),
        static_cast<int>(address_family));

    return SystemHostResolverProc(ip_literal,
                                  ADDRESS_FAMILY_UNSPECIFIED,
                                  host_resolver_flags,
                                  addrlist, os_error);
  }
};

// Using LookupAttemptHostResolverProc simulate very long lookups, and control
// which attempt resolves the host.
class LookupAttemptHostResolverProc : public HostResolverProc {
 public:
  LookupAttemptHostResolverProc(HostResolverProc* previous,
                                int attempt_number_to_resolve,
                                int total_attempts)
      : HostResolverProc(previous),
        attempt_number_to_resolve_(attempt_number_to_resolve),
        current_attempt_number_(0),
        total_attempts_(total_attempts),
        total_attempts_resolved_(0),
        resolved_attempt_number_(0),
        all_done_(&lock_) {
  }

  // Test harness will wait for all attempts to finish before checking the
  // results.
  void WaitForAllAttemptsToFinish(const TimeDelta& wait_time) {
    TimeTicks end_time = TimeTicks::Now() + wait_time;
    {
      base::AutoLock auto_lock(lock_);
      while (total_attempts_resolved_ != total_attempts_ &&
             TimeTicks::Now() < end_time) {
        all_done_.TimedWait(end_time - TimeTicks::Now());
      }
    }
  }

  // All attempts will wait for an attempt to resolve the host.
  void WaitForAnAttemptToComplete() {
    TimeDelta wait_time = TimeDelta::FromMilliseconds(60000);
    TimeTicks end_time = TimeTicks::Now() + wait_time;
    {
      base::AutoLock auto_lock(lock_);
      while (resolved_attempt_number_ == 0 && TimeTicks::Now() < end_time)
        all_done_.TimedWait(end_time - TimeTicks::Now());
    }
    all_done_.Broadcast();  // Tell all waiting attempts to proceed.
  }

  // Returns the number of attempts that have finished the Resolve() method.
  int total_attempts_resolved() { return total_attempts_resolved_; }

  // Returns the first attempt that that has resolved the host.
  int resolved_attempt_number() { return resolved_attempt_number_; }

  // HostResolverProc methods.
  virtual int Resolve(const std::string& host,
                      AddressFamily address_family,
                      HostResolverFlags host_resolver_flags,
                      AddressList* addrlist,
                      int* os_error) OVERRIDE {
    bool wait_for_right_attempt_to_complete = true;
    {
      base::AutoLock auto_lock(lock_);
      ++current_attempt_number_;
      if (current_attempt_number_ == attempt_number_to_resolve_) {
        resolved_attempt_number_ = current_attempt_number_;
        wait_for_right_attempt_to_complete = false;
      }
    }

    if (wait_for_right_attempt_to_complete)
      // Wait for the attempt_number_to_resolve_ attempt to resolve.
      WaitForAnAttemptToComplete();

    int result = ResolveUsingPrevious(host, address_family, host_resolver_flags,
                                      addrlist, os_error);

    {
      base::AutoLock auto_lock(lock_);
      ++total_attempts_resolved_;
    }

    all_done_.Broadcast();  // Tell all attempts to proceed.

    // Since any negative number is considered a network error, with -1 having
    // special meaning (ERR_IO_PENDING). We could return the attempt that has
    // resolved the host as a negative number. For example, if attempt number 3
    // resolves the host, then this method returns -4.
    if (result == OK)
      return -1 - resolved_attempt_number_;
    else
      return result;
  }

 private:
  virtual ~LookupAttemptHostResolverProc() {}

  int attempt_number_to_resolve_;
  int current_attempt_number_;  // Incremented whenever Resolve is called.
  int total_attempts_;
  int total_attempts_resolved_;
  int resolved_attempt_number_;

  // All attempts wait for right attempt to be resolve.
  base::Lock lock_;
  base::ConditionVariable all_done_;
};

// Helper that represents a single Resolve() result, used to inspect all the
// resolve results by forwarding them to Delegate.
class ResolveRequest {
 public:
  // Delegate interface, for notification when the ResolveRequest completes.
  class Delegate {
   public:
    virtual ~Delegate() {}
    virtual void OnCompleted(ResolveRequest* resolve) = 0;
  };

  // For asynchronous resolutions.
  ResolveRequest(HostResolver* resolver,
                 const std::string& hostname,
                 int port,
                 Delegate* delegate)
      : info_(HostPortPair(hostname, port)),
        resolver_(resolver),
        delegate_(delegate)  {
    // Start the request.
    int err = resolver->Resolve(
        info_, &addrlist_,
        base::Bind(&ResolveRequest::OnLookupFinished, base::Unretained(this)),
        &req_, BoundNetLog());
    EXPECT_EQ(ERR_IO_PENDING, err);
  }

  // For asynchronous resolutions.
  ResolveRequest(HostResolver* resolver,
                 const HostResolver::RequestInfo& info,
                 Delegate* delegate)
      : info_(info), resolver_(resolver), delegate_(delegate) {
    // Start the request.
    int err = resolver->Resolve(
        info, &addrlist_,
        base::Bind(&ResolveRequest::OnLookupFinished,
                   base::Unretained(this)),
        &req_, BoundNetLog());
    EXPECT_EQ(ERR_IO_PENDING, err);
  }

  // For synchronous resolutions.
  ResolveRequest(HostResolver* resolver,
                 const std::string& hostname,
                 int port)
      : info_(HostPortPair(hostname, port)),
        resolver_(resolver),
        delegate_(NULL)  {
    // Start the request.
    result_ = resolver->Resolve(
        info_, &addrlist_,
        base::Bind(&ResolveRequest::OnLookupFinished, base::Unretained(this)),
        &req_, BoundNetLog());
    EXPECT_NE(ERR_IO_PENDING, result_);
  }

  // For synchronous resolutions.
  ResolveRequest(HostResolver* resolver,
                 const HostResolver::RequestInfo& info)
      : info_(info),
        resolver_(resolver),
        delegate_(NULL)  {
    // Start the request.
    result_ = resolver->Resolve(
        info_, &addrlist_,
        base::Bind(&ResolveRequest::OnLookupFinished, base::Unretained(this)),
        &req_, BoundNetLog());
    EXPECT_NE(ERR_IO_PENDING, result_);
  }

  void Cancel() {
    resolver_->CancelRequest(req_);
  }

  const std::string& hostname() const {
    return info_.hostname();
  }

  int port() const {
    return info_.port();
  }

  int result() const {
    return result_;
  }

  const AddressList& addrlist() const {
    return addrlist_;
  }

  HostResolver* resolver() const {
    return resolver_;
  }

 private:
  void OnLookupFinished(int result) {
    EXPECT_TRUE(delegate_ != NULL);
    if (delegate_ == NULL)
      return;
    result_ = result;
    delegate_->OnCompleted(this);
  }

  // The request details.
  HostResolver::RequestInfo info_;
  HostResolver::RequestHandle req_;

  // The result of the resolve.
  int result_;
  AddressList addrlist_;

  HostResolver* resolver_;

  Delegate* delegate_;

  DISALLOW_COPY_AND_ASSIGN(ResolveRequest);
};

// TODO(szym): Make this fixture more useful. http://crbug.com/117830
class HostResolverImplTest : public testing::Test {
 public:
  HostResolverImplTest()
      : callback_called_(false),
        ALLOW_THIS_IN_INITIALIZER_LIST(callback_(
            base::Bind(&HostResolverImplTest::OnLookupFinished,
                       base::Unretained(this)))) {
  }

 protected:
  void OnLookupFinished(int result) {
    callback_called_ = true;
    callback_result_ = result;
    MessageLoop::current()->Quit();
  }

  bool callback_called_;
  int callback_result_;
  CompletionCallback callback_;
};

// Returns the first address in |addr_list| in "host:port" form, empty string if
// the list is empty, or "UNSUPPORTED" if the address cannot be converted.
std::string FirstAddressToString(const AddressList& addr_list) {
  const struct addrinfo* ai = addr_list.head();
  if (!ai)
    return "";
  std::string out = NetAddressToStringWithPort(ai);
  if (out.empty())
    return "UNSUPPORTED";
  return out;
}

// Returns the number of addresses in |addr_list|.
unsigned NumberOfAddresses(const AddressList& addr_list) {
  unsigned count = 0;
  for (const struct addrinfo* ai = addr_list.head();
       ai != NULL;
       ai = ai->ai_next) {
    ++count;
  }
  return count;
}

}  // namespace net

TEST_F(HostResolverImplTest, AsynchronousLookup) {
  AddressList addrlist;

  scoped_refptr<RuleBasedHostResolverProc> resolver_proc(
      new RuleBasedHostResolverProc(NULL));
  resolver_proc->AddRule("just.testing", "192.168.1.42");

  scoped_ptr<HostResolver> host_resolver(
      CreateHostResolverImpl(resolver_proc));

  HostResolver::RequestInfo info(HostPortPair("just.testing", 80));
  CapturingBoundNetLog log(CapturingNetLog::kUnbounded);
  int err = host_resolver->Resolve(info, &addrlist, callback_, NULL,
                                   log.bound());
  EXPECT_EQ(ERR_IO_PENDING, err);

  CapturingNetLog::EntryList entries;
  log.GetEntries(&entries);

  EXPECT_EQ(1u, entries.size());
  EXPECT_TRUE(LogContainsBeginEvent(
      entries, 0, NetLog::TYPE_HOST_RESOLVER_IMPL));

  MessageLoop::current()->Run();

  ASSERT_TRUE(callback_called_);
  ASSERT_EQ(OK, callback_result_);

  log.GetEntries(&entries);

  EXPECT_EQ(2u, entries.size());
  EXPECT_TRUE(LogContainsEndEvent(
      entries, 1, NetLog::TYPE_HOST_RESOLVER_IMPL));

  EXPECT_EQ("192.168.1.42:80", FirstAddressToString(addrlist));
  EXPECT_EQ(1u, NumberOfAddresses(addrlist));
}

TEST_F(HostResolverImplTest, FailedAsynchronousLookup) {
  AddressList addrlist;
  scoped_refptr<RuleBasedHostResolverProc> resolver_proc(
      new RuleBasedHostResolverProc(NULL));
  resolver_proc->AddSimulatedFailure("just.testing");

  scoped_ptr<HostResolver> host_resolver(CreateHostResolverImpl(resolver_proc));

  HostResolver::RequestInfo info(HostPortPair("just.testing", 80));
  CapturingBoundNetLog log(CapturingNetLog::kUnbounded);
  int err = host_resolver->Resolve(info, &addrlist, callback_, NULL,
                                   log.bound());
  EXPECT_EQ(ERR_IO_PENDING, err);

  CapturingNetLog::EntryList entries;
  log.GetEntries(&entries);

  EXPECT_EQ(1u, entries.size());
  EXPECT_TRUE(LogContainsBeginEvent(
      entries, 0, NetLog::TYPE_HOST_RESOLVER_IMPL));

  MessageLoop::current()->Run();

  ASSERT_TRUE(callback_called_);
  ASSERT_EQ(ERR_NAME_NOT_RESOLVED, callback_result_);

  log.GetEntries(&entries);

  EXPECT_EQ(2u, entries.size());
  EXPECT_TRUE(LogContainsEndEvent(
      entries, 1, NetLog::TYPE_HOST_RESOLVER_IMPL));

  // Also test that the error is not cached!
  err = host_resolver->ResolveFromCache(info, &addrlist, log.bound());
  EXPECT_EQ(ERR_DNS_CACHE_MISS, err);
}

TEST_F(HostResolverImplTest, AbortedAsynchronousLookup) {
  scoped_refptr<WaitingHostResolverProc> resolver_proc(
      new WaitingHostResolverProc(NULL));

  CapturingNetLog net_log(CapturingNetLog::kUnbounded);
  CapturingBoundNetLog log(CapturingNetLog::kUnbounded);
  {
    // This resolver will be destroyed while a lookup is running on WorkerPool.
    scoped_ptr<HostResolver> host_resolver(
        new HostResolverImpl(HostCache::CreateDefaultCache(),
                             DefaultLimits(),
                             DefaultParams(resolver_proc),
                             scoped_ptr<DnsConfigService>(NULL),
                             &net_log));
    AddressList addrlist;
    HostResolver::RequestInfo info(HostPortPair("just.testing", 80));
    int err = host_resolver->Resolve(info, &addrlist, callback_, NULL,
                                     log.bound());
    EXPECT_EQ(ERR_IO_PENDING, err);

    resolver_proc->Wait();
  }

  resolver_proc->Signal();

  CapturingNetLog::EntryList entries;
  log.GetEntries(&entries);

  EXPECT_EQ(2u, entries.size());
  EXPECT_TRUE(LogContainsBeginEvent(
      entries, 0, NetLog::TYPE_HOST_RESOLVER_IMPL));
  EXPECT_TRUE(LogContainsEndEvent(
      entries, 1, NetLog::TYPE_HOST_RESOLVER_IMPL));

  CapturingNetLog::EntryList net_log_entries;
  net_log.GetEntries(&net_log_entries);

  int pos = ExpectLogContainsSomewhereAfter(net_log_entries, 0,
      NetLog::TYPE_HOST_RESOLVER_IMPL_REQUEST,
      NetLog::PHASE_BEGIN);
  pos = ExpectLogContainsSomewhereAfter(net_log_entries, pos + 1,
      NetLog::TYPE_HOST_RESOLVER_IMPL_JOB,
      NetLog::PHASE_BEGIN);
  pos = ExpectLogContainsSomewhereAfter(net_log_entries, pos + 1,
      NetLog::TYPE_HOST_RESOLVER_IMPL_PROC_TASK,
      NetLog::PHASE_BEGIN);

  // The Request needs to be cancelled. (The Job is "aborted".)
  // Don't care about order in which Request, Job and ProcTask end.
  ExpectLogContainsSomewhereAfter(net_log_entries, pos + 1,
      NetLog::TYPE_CANCELLED,
      NetLog::PHASE_NONE);
  ExpectLogContainsSomewhereAfter(net_log_entries, pos + 1,
      NetLog::TYPE_HOST_RESOLVER_IMPL_PROC_TASK,
      NetLog::PHASE_END);
  ExpectLogContainsSomewhereAfter(net_log_entries, pos + 1,
      NetLog::TYPE_HOST_RESOLVER_IMPL_REQUEST,
      NetLog::PHASE_END);
  ExpectLogContainsSomewhereAfter(net_log_entries, pos + 1,
      NetLog::TYPE_HOST_RESOLVER_IMPL_JOB,
      NetLog::PHASE_END);

  EXPECT_FALSE(callback_called_);
}

TEST_F(HostResolverImplTest, NumericIPv4Address) {
  // Stevens says dotted quads with AI_UNSPEC resolve to a single sockaddr_in.

  scoped_refptr<RuleBasedHostResolverProc> resolver_proc(
      new RuleBasedHostResolverProc(NULL));
  resolver_proc->AllowDirectLookup("*");

  scoped_ptr<HostResolver> host_resolver(
      CreateHostResolverImpl(resolver_proc));
  AddressList addrlist;
  TestCompletionCallback callback;
  HostResolver::RequestInfo info(HostPortPair("127.1.2.3", 5555));
  int err = host_resolver->Resolve(info, &addrlist, callback.callback(), NULL,
                                   BoundNetLog());
  EXPECT_EQ(OK, err);

  EXPECT_EQ("127.1.2.3:5555", FirstAddressToString(addrlist));
  EXPECT_EQ(1u, NumberOfAddresses(addrlist));
}

TEST_F(HostResolverImplTest, NumericIPv6Address) {
  scoped_refptr<RuleBasedHostResolverProc> resolver_proc(
      new RuleBasedHostResolverProc(NULL));
  resolver_proc->AllowDirectLookup("*");

  // Resolve a plain IPv6 address.  Don't worry about [brackets], because
  // the caller should have removed them.
  scoped_ptr<HostResolver> host_resolver(
      CreateHostResolverImpl(resolver_proc));
  AddressList addrlist;
  TestCompletionCallback callback;
  HostResolver::RequestInfo info(HostPortPair("2001:db8::1", 5555));
  int err = host_resolver->Resolve(info, &addrlist, callback.callback(), NULL,
                                   BoundNetLog());
  EXPECT_EQ(OK, err);

  std::string ipv6string = FirstAddressToString(addrlist);
  if (ipv6string != "UNSUPPORTED")
    EXPECT_EQ("[2001:db8::1]:5555", ipv6string);
  EXPECT_EQ(1u, NumberOfAddresses(addrlist));
}

TEST_F(HostResolverImplTest, EmptyHost) {
  scoped_refptr<RuleBasedHostResolverProc> resolver_proc(
      new RuleBasedHostResolverProc(NULL));
  resolver_proc->AllowDirectLookup("*");

  scoped_ptr<HostResolver> host_resolver(
      CreateHostResolverImpl(resolver_proc));
  AddressList addrlist;
  TestCompletionCallback callback;
  HostResolver::RequestInfo info(HostPortPair("", 5555));
  int err = host_resolver->Resolve(info, &addrlist, callback.callback(), NULL,
                                   BoundNetLog());
  EXPECT_EQ(ERR_NAME_NOT_RESOLVED, err);
}

TEST_F(HostResolverImplTest, LongHost) {
  scoped_refptr<RuleBasedHostResolverProc> resolver_proc(
      new RuleBasedHostResolverProc(NULL));
  resolver_proc->AllowDirectLookup("*");

  scoped_ptr<HostResolver> host_resolver(
      CreateHostResolverImpl(resolver_proc));
  AddressList addrlist;
  std::string hostname(4097, 'a');
  TestCompletionCallback callback;
  HostResolver::RequestInfo info(HostPortPair(hostname, 5555));
  int err = host_resolver->Resolve(info, &addrlist, callback.callback(), NULL,
                                   BoundNetLog());
  EXPECT_EQ(ERR_NAME_NOT_RESOLVED, err);
}

// Helper class used by HostResolverImplTest.DeDupeRequests. It receives request
// completion notifications for all the resolves, so it can tally up and
// determine when we are done.
class DeDupeRequestsVerifier : public ResolveRequest::Delegate {
 public:
  explicit DeDupeRequestsVerifier(CapturingHostResolverProc* resolver_proc)
      : count_a_(0), count_b_(0), resolver_proc_(resolver_proc) {}

  // The test does 5 resolves (which can complete in any order).
  virtual void OnCompleted(ResolveRequest* resolve) OVERRIDE {
    // Tally up how many requests we have seen.
    if (resolve->hostname() == "a") {
      count_a_++;
    } else if (resolve->hostname() == "b") {
      count_b_++;
    } else {
      FAIL() << "Unexpected hostname: " << resolve->hostname();
    }

    // Check that the port was set correctly.
    EXPECT_EQ(resolve->port(), resolve->addrlist().GetPort());

    // Check whether all the requests have finished yet.
    int total_completions = count_a_ + count_b_;
    if (total_completions == 5) {
      EXPECT_EQ(2, count_a_);
      EXPECT_EQ(3, count_b_);

      // The resolver_proc should have been called only twice -- once with "a",
      // once with "b".
      CapturingHostResolverProc::CaptureList capture_list =
          resolver_proc_->GetCaptureList();
      EXPECT_EQ(2U, capture_list.size());

      // End this test, we are done.
      MessageLoop::current()->Quit();
    }
  }

 private:
  int count_a_;
  int count_b_;
  CapturingHostResolverProc* resolver_proc_;

  DISALLOW_COPY_AND_ASSIGN(DeDupeRequestsVerifier);
};

TEST_F(HostResolverImplTest, DeDupeRequests) {
  // Use a capturing resolver_proc, since the verifier needs to know what calls
  // reached Resolve().  Also, the capturing resolver_proc is initially blocked.
  scoped_refptr<CapturingHostResolverProc> resolver_proc(
      new CapturingHostResolverProc(NULL));

  scoped_ptr<HostResolver> host_resolver(
      CreateHostResolverImpl(resolver_proc));

  // The class will receive callbacks for when each resolve completes. It
  // checks that the right things happened.
  DeDupeRequestsVerifier verifier(resolver_proc.get());

  // Start 5 requests, duplicating hosts "a" and "b". Since the resolver_proc is
  // blocked, these should all pile up until we signal it.

  ResolveRequest req1(host_resolver.get(), "a", 80, &verifier);
  ResolveRequest req2(host_resolver.get(), "b", 80, &verifier);
  ResolveRequest req3(host_resolver.get(), "b", 81, &verifier);
  ResolveRequest req4(host_resolver.get(), "a", 82, &verifier);
  ResolveRequest req5(host_resolver.get(), "b", 83, &verifier);

  // Ready, Set, GO!!!
  resolver_proc->Signal();

  // |verifier| will send quit message once all the requests have finished.
  MessageLoop::current()->Run();
}

// Helper class used by HostResolverImplTest.CancelMultipleRequests.
class CancelMultipleRequestsVerifier : public ResolveRequest::Delegate {
 public:
  CancelMultipleRequestsVerifier() {}

  // The cancels kill all but one request.
  virtual void OnCompleted(ResolveRequest* resolve) OVERRIDE {
    EXPECT_EQ("a", resolve->hostname());
    EXPECT_EQ(82, resolve->port());

    // Check that the port was set correctly.
    EXPECT_EQ(resolve->port(), resolve->addrlist().GetPort());

    // End this test, we are done.
    MessageLoop::current()->Quit();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(CancelMultipleRequestsVerifier);
};

TEST_F(HostResolverImplTest, CancelMultipleRequests) {
  // Use a capturing resolver_proc, since the verifier needs to know what calls
  // reached Resolver().  Also, the capturing resolver_proc is initially
  // blocked.
  scoped_refptr<CapturingHostResolverProc> resolver_proc(
      new CapturingHostResolverProc(NULL));

  scoped_ptr<HostResolver> host_resolver(
      CreateHostResolverImpl(resolver_proc));

  // The class will receive callbacks for when each resolve completes. It
  // checks that the right things happened.
  CancelMultipleRequestsVerifier verifier;

  // Start 5 requests, duplicating hosts "a" and "b". Since the resolver_proc is
  // blocked, these should all pile up until we signal it.

  ResolveRequest req1(host_resolver.get(), "a", 80, &verifier);
  ResolveRequest req2(host_resolver.get(), "b", 80, &verifier);
  ResolveRequest req3(host_resolver.get(), "b", 81, &verifier);
  ResolveRequest req4(host_resolver.get(), "a", 82, &verifier);
  ResolveRequest req5(host_resolver.get(), "b", 83, &verifier);

  // Cancel everything except request 4.
  req1.Cancel();
  req2.Cancel();
  req3.Cancel();
  req5.Cancel();

  // Ready, Set, GO!!!
  resolver_proc->Signal();

  // |verifier| will send quit message once all the requests have finished.
  MessageLoop::current()->Run();
}

// Delegate which allows to wait for specific number of requests to complete.
// Used by HostResolverImplTest.CanceledRequestsReleaseJobSlots and .DnsTask.
class CountingDelegate : public ResolveRequest::Delegate {
 public:
  CountingDelegate() : num_completions_(0), awaited_num_completions_(0) {}

  virtual void OnCompleted(ResolveRequest* resolve) OVERRIDE {
    ++num_completions_;
    if (num_completions_ == awaited_num_completions_)
      MessageLoop::current()->Quit();
  }

  unsigned num_completions() const { return num_completions_; }

  void WaitForCompletions(unsigned completions) {
    ASSERT_LT(num_completions_, completions);
    awaited_num_completions_ = completions;
    MessageLoop::current()->Run();
    EXPECT_EQ(completions, num_completions_);
  }

 private:
  unsigned num_completions_;
  unsigned awaited_num_completions_;
};

TEST_F(HostResolverImplTest, CanceledRequestsReleaseJobSlots) {
  scoped_refptr<CountingHostResolverProc> resolver_proc(
      new CountingHostResolverProc(NULL));

  scoped_ptr<HostResolverImpl> host_resolver(
      CreateHostResolverImpl(resolver_proc));

  CountingDelegate delegate;
  ScopedVector<ResolveRequest> requests;

  // Fill up the dispatcher and queue.
  for (unsigned i = 0; i < kMaxJobs + 1; ++i) {
    std::string hostname = "a_";
    hostname[1] = 'a' + i;
    requests.push_back(new ResolveRequest(host_resolver.get(), hostname, 80,
                                          &delegate));
    requests.push_back(new ResolveRequest(host_resolver.get(), hostname, 81,
                                          &delegate));
  }

  EXPECT_TRUE(resolver_proc->WaitFor(kMaxJobs));

  // Cancel all but last two.
  for (unsigned i = 0; i < requests.size() - 2; ++i) {
    requests[i]->Cancel();
  }

  EXPECT_TRUE(resolver_proc->WaitFor(kMaxJobs + 1));
  EXPECT_EQ(0u, delegate.num_completions());

  resolver_proc->SignalAll();

  delegate.WaitForCompletions(2);

  EXPECT_EQ(0u, host_resolver->num_running_jobs_for_tests());
}

// Helper class used by HostResolverImplTest.CancelWithinCallback.
class CancelWithinCallbackVerifier : public ResolveRequest::Delegate {
 public:
  CancelWithinCallbackVerifier()
      : req_to_cancel1_(NULL), req_to_cancel2_(NULL), num_completions_(0) {
  }

  virtual void OnCompleted(ResolveRequest* resolve) OVERRIDE {
    num_completions_++;

    // Port 80 is the first request that the callback will be invoked for.
    // While we are executing within that callback, cancel the other requests
    // in the job and start another request.
    if (80 == resolve->port()) {
      EXPECT_EQ("a", resolve->hostname());

      req_to_cancel1_->Cancel();
      req_to_cancel2_->Cancel();

      // Start a request (so we can make sure the canceled requests don't
      // complete before "finalrequest" finishes.
      final_request_.reset(new ResolveRequest(
          resolve->resolver(), "finalrequest", 70, this));

    } else if (83 == resolve->port()) {
      EXPECT_EQ("a", resolve->hostname());
    } else if (resolve->hostname() == "finalrequest") {
      EXPECT_EQ(70, resolve->addrlist().GetPort());

      // End this test, we are done.
      MessageLoop::current()->Quit();
    } else {
      FAIL() << "Unexpected completion: " << resolve->hostname() << ", "
             << resolve->port();
    }
  }

  void SetRequestsToCancel(ResolveRequest* req_to_cancel1,
                           ResolveRequest* req_to_cancel2) {
    req_to_cancel1_ = req_to_cancel1;
    req_to_cancel2_ = req_to_cancel2;
  }

 private:
  scoped_ptr<ResolveRequest> final_request_;
  ResolveRequest* req_to_cancel1_;
  ResolveRequest* req_to_cancel2_;
  int num_completions_;
  DISALLOW_COPY_AND_ASSIGN(CancelWithinCallbackVerifier);
};

TEST_F(HostResolverImplTest, CancelWithinCallback) {
  // Use a capturing resolver_proc, since the verifier needs to know what calls
  // reached Resolver().  Also, the capturing resolver_proc is initially
  // blocked.
  scoped_refptr<CapturingHostResolverProc> resolver_proc(
      new CapturingHostResolverProc(NULL));

  scoped_ptr<HostResolver> host_resolver(CreateHostResolverImpl(resolver_proc));

  // The class will receive callbacks for when each resolve completes. It
  // checks that the right things happened.
  CancelWithinCallbackVerifier verifier;

  // Start 4 requests, duplicating hosts "a". Since the resolver_proc is
  // blocked, these should all pile up until we signal it.

  ResolveRequest req1(host_resolver.get(), "a", 80, &verifier);
  ResolveRequest req2(host_resolver.get(), "a", 81, &verifier);
  ResolveRequest req3(host_resolver.get(), "a", 82, &verifier);
  ResolveRequest req4(host_resolver.get(), "a", 83, &verifier);

  // Once "a:80" completes, it will cancel "a:81" and "a:82".
  verifier.SetRequestsToCancel(&req2, &req3);

  // Ready, Set, GO!!!
  resolver_proc->Signal();

  // |verifier| will send quit message once all the requests have finished.
  MessageLoop::current()->Run();
}

// Helper class used by HostResolverImplTest.DeleteWithinCallback.
class DeleteWithinCallbackVerifier : public ResolveRequest::Delegate {
 public:
  // |host_resolver| is the resolver that the the resolve requests were started
  // with.
  explicit DeleteWithinCallbackVerifier(HostResolver* host_resolver)
      : host_resolver_(host_resolver) {}

  virtual void OnCompleted(ResolveRequest* resolve) OVERRIDE {
    EXPECT_EQ("a", resolve->hostname());
    EXPECT_EQ(80, resolve->port());

    // Deletes the host resolver.
    host_resolver_.reset();

    // Quit after returning from OnCompleted (to give it a chance at
    // incorrectly running the cancelled tasks).
    MessageLoop::current()->PostTask(FROM_HERE, MessageLoop::QuitClosure());
  }

 private:
  scoped_ptr<HostResolver> host_resolver_;
  DISALLOW_COPY_AND_ASSIGN(DeleteWithinCallbackVerifier);
};

TEST_F(HostResolverImplTest, DeleteWithinCallback) {
  // Use a capturing resolver_proc, since the verifier needs to know what calls
  // reached Resolver().  Also, the capturing resolver_proc is initially
  // blocked.
  scoped_refptr<CapturingHostResolverProc> resolver_proc(
      new CapturingHostResolverProc(NULL));

  // The class will receive callbacks for when each resolve completes. It
  // checks that the right things happened. Note that the verifier holds the
  // only reference to |host_resolver|, so it can delete it within callback.
  HostResolver* host_resolver = CreateHostResolverImpl(resolver_proc);
  DeleteWithinCallbackVerifier verifier(host_resolver);

  // Start 4 requests, duplicating hosts "a". Since the resolver_proc is
  // blocked, these should all pile up until we signal it.

  ResolveRequest req1(host_resolver, "a", 80, &verifier);
  ResolveRequest req2(host_resolver, "a", 81, &verifier);
  ResolveRequest req3(host_resolver, "a", 82, &verifier);
  ResolveRequest req4(host_resolver, "a", 83, &verifier);

  // Ready, Set, GO!!!
  resolver_proc->Signal();

  // |verifier| will send quit message once all the requests have finished.
  MessageLoop::current()->Run();
}

// Helper class used by HostResolverImplTest.StartWithinCallback.
class StartWithinCallbackVerifier : public ResolveRequest::Delegate {
 public:
  StartWithinCallbackVerifier() : num_requests_(0) {}

  virtual void OnCompleted(ResolveRequest* resolve) OVERRIDE {
    EXPECT_EQ("a", resolve->hostname());

    if (80 == resolve->port()) {
      // On completing the first request, start another request for "a".
      // Since caching is disabled, this will result in another async request.
      final_request_.reset(new ResolveRequest(
        resolve->resolver(), "a", 70, this));
    }
    if (++num_requests_ == 5) {
      // Test is done.
      MessageLoop::current()->Quit();
    }
  }

 private:
  int num_requests_;
  scoped_ptr<ResolveRequest> final_request_;
  DISALLOW_COPY_AND_ASSIGN(StartWithinCallbackVerifier);
};

TEST_F(HostResolverImplTest, StartWithinCallback) {
  // Use a capturing resolver_proc, since the verifier needs to know what calls
  // reached Resolver().  Also, the capturing resolver_proc is initially
  // blocked.
  scoped_refptr<CapturingHostResolverProc> resolver_proc(
      new CapturingHostResolverProc(NULL));

  // Turn off caching for this host resolver.
  scoped_ptr<HostResolver> host_resolver(new HostResolverImpl(
      NULL,
      DefaultLimits(),
      DefaultParams(resolver_proc),
      scoped_ptr<DnsConfigService>(NULL),
      NULL));

  // The class will receive callbacks for when each resolve completes. It
  // checks that the right things happened.
  StartWithinCallbackVerifier verifier;

  // Start 4 requests, duplicating hosts "a". Since the resolver_proc is
  // blocked, these should all pile up until we signal it.

  ResolveRequest req1(host_resolver.get(), "a", 80, &verifier);
  ResolveRequest req2(host_resolver.get(), "a", 81, &verifier);
  ResolveRequest req3(host_resolver.get(), "a", 82, &verifier);
  ResolveRequest req4(host_resolver.get(), "a", 83, &verifier);

  // Ready, Set, GO!!!
  resolver_proc->Signal();

  // |verifier| will send quit message once all the requests have finished.
  MessageLoop::current()->Run();
}

// Helper class used by HostResolverImplTest.BypassCache.
class BypassCacheVerifier : public ResolveRequest::Delegate {
 public:
  BypassCacheVerifier() {}

  virtual void OnCompleted(ResolveRequest* resolve) OVERRIDE {
    EXPECT_EQ("a", resolve->hostname());
    HostResolver* resolver = resolve->resolver();

    if (80 == resolve->port()) {
      // On completing the first request, start another request for "a".
      // Since caching is enabled, this should complete synchronously.

      // Note that |junk_callback| shouldn't be used since we are going to
      // complete synchronously.
      TestCompletionCallback junk_callback;
      AddressList addrlist;

      HostResolver::RequestInfo info(HostPortPair("a", 70));
      int error = resolver->Resolve(info, &addrlist, junk_callback.callback(),
                                    NULL, BoundNetLog());
      EXPECT_EQ(OK, error);

      // Ok good. Now make sure that if we ask to bypass the cache, it can no
      // longer service the request synchronously.
      info = HostResolver::RequestInfo(HostPortPair("a", 71));
      info.set_allow_cached_response(false);
      final_request_.reset(new ResolveRequest(resolver, info, this));
    } else if (71 == resolve->port()) {
      // Test is done.
      MessageLoop::current()->Quit();
    } else {
      FAIL() << "Unexpected port number";
    }
  }

 private:
  scoped_ptr<ResolveRequest> final_request_;
  DISALLOW_COPY_AND_ASSIGN(BypassCacheVerifier);
};

TEST_F(HostResolverImplTest, BypassCache) {
  scoped_ptr<HostResolver> host_resolver(
      CreateHostResolverImpl(NULL));

  // The class will receive callbacks for when each resolve completes. It
  // checks that the right things happened.
  BypassCacheVerifier verifier;

  // Start a request.
  ResolveRequest req1(host_resolver.get(), "a", 80, &verifier);

  // |verifier| will send quit message once all the requests have finished.
  MessageLoop::current()->Run();
}

// Test that IP address changes flush the cache.
TEST_F(HostResolverImplTest, FlushCacheOnIPAddressChange) {
  scoped_ptr<HostResolver> host_resolver(
      CreateHostResolverImpl(NULL));

  AddressList addrlist;

  // Resolve "host1". Assume that ScopedDefaultHostResolverProc resolves all.
  HostResolver::RequestInfo info1(HostPortPair("host1", 70));
  TestCompletionCallback callback;
  int rv = host_resolver->Resolve(info1, &addrlist, callback.callback(), NULL,
                                  BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);
  EXPECT_EQ(OK, callback.WaitForResult());

  // Resolve "host1" again -- this time it will be served from cache, but it
  // should still notify of completion.
  rv = host_resolver->Resolve(info1, &addrlist, callback.callback(), NULL,
                              BoundNetLog());
  ASSERT_EQ(OK, rv);  // Should complete synchronously.

  // Flush cache by triggering an IP address change.
  NetworkChangeNotifier::NotifyObserversOfIPAddressChangeForTests();
  MessageLoop::current()->RunAllPending();  // Notification happens async.

  // Resolve "host1" again -- this time it won't be served from cache, so it
  // will complete asynchronously.
  rv = host_resolver->Resolve(info1, &addrlist, callback.callback(), NULL,
                              BoundNetLog());
  ASSERT_EQ(ERR_IO_PENDING, rv);  // Should complete asynchronously.
  EXPECT_EQ(OK, callback.WaitForResult());
}

// Test that IP address changes send ERR_ABORTED to pending requests.
TEST_F(HostResolverImplTest, AbortOnIPAddressChanged) {
  scoped_refptr<WaitingHostResolverProc> resolver_proc(
      new WaitingHostResolverProc(NULL));
  scoped_ptr<HostResolver> host_resolver(CreateHostResolverImpl(resolver_proc));

  // Resolve "host1".
  HostResolver::RequestInfo info(HostPortPair("host1", 70));
  TestCompletionCallback callback;
  AddressList addrlist;
  int rv = host_resolver->Resolve(info, &addrlist, callback.callback(), NULL,
                                  BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);

  resolver_proc->Wait();
  // Triggering an IP address change.
  NetworkChangeNotifier::NotifyObserversOfIPAddressChangeForTests();
  MessageLoop::current()->RunAllPending();  // Notification happens async.
  resolver_proc->Signal();

  EXPECT_EQ(ERR_ABORTED, callback.WaitForResult());
  EXPECT_EQ(0u, host_resolver->GetHostCache()->size());
}

// Obey pool constraints after IP address has changed.
TEST_F(HostResolverImplTest, ObeyPoolConstraintsAfterIPAddressChange) {
  scoped_refptr<WaitingHostResolverProc> resolver_proc(
      new WaitingHostResolverProc(CreateCatchAllHostResolverProc()));

  scoped_ptr<HostResolverImpl> host_resolver(
      CreateSerialHostResolverImpl(resolver_proc));

  // Resolve "host1".
  HostResolver::RequestInfo info(HostPortPair("host1", 70));
  TestCompletionCallback callback;
  AddressList addrlist;
  int rv = host_resolver->Resolve(info, &addrlist, callback.callback(), NULL,
                                  BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);

  // Must wait before signal to ensure that the two signals don't get merged
  // together. (Worker threads might not start until the last WaitForResult.)
  resolver_proc->Wait();
  // Triggering an IP address change.
  NetworkChangeNotifier::NotifyObserversOfIPAddressChangeForTests();
  MessageLoop::current()->RunAllPending();  // Notification happens async.
  resolver_proc->Signal();

  EXPECT_EQ(ERR_ABORTED, callback.WaitForResult());

  rv = host_resolver->Resolve(info, &addrlist, callback.callback(), NULL,
                              BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);
  resolver_proc->Wait();
  resolver_proc->Signal();
  EXPECT_EQ(OK, callback.WaitForResult());

  EXPECT_EQ(0u, host_resolver->num_running_jobs_for_tests());
}

// Helper class used by AbortOnlyExistingRequestsOnIPAddressChange.
class StartWithinAbortedCallbackVerifier : public ResolveRequest::Delegate {
 public:
  explicit StartWithinAbortedCallbackVerifier(const std::string& next_hostname)
      : next_hostname_(next_hostname) {}

  virtual void OnCompleted(ResolveRequest* resolve) OVERRIDE {
    if (request_.get() == NULL) {
      EXPECT_EQ(ERR_ABORTED, resolve->result());
      // Start new request for a different hostname to ensure that the order
      // of jobs in HostResolverImpl is not stable.
      request_.reset(new ResolveRequest(resolve->resolver(),
                                        next_hostname_,
                                        resolve->port(),
                                        this));
    } else {
      EXPECT_EQ(resolve, request_.get());
      callback_.callback().Run(resolve->result());
    }
  }

  int WaitUntilDone() {
    return callback_.WaitForResult();
  }

 private:
  std::string next_hostname_;
  scoped_ptr<ResolveRequest> request_;
  TestCompletionCallback callback_;
  DISALLOW_COPY_AND_ASSIGN(StartWithinAbortedCallbackVerifier);
};

// Tests that a new Request made from the callback of a previously aborted one
// will not be aborted.
TEST_F(HostResolverImplTest, AbortOnlyExistingRequestsOnIPAddressChange) {
  scoped_refptr<CountingHostResolverProc> resolver_proc(
      new CountingHostResolverProc(CreateCatchAllHostResolverProc()));
  scoped_ptr<HostResolverImpl> host_resolver(
      CreateHostResolverImpl(resolver_proc));

  StartWithinAbortedCallbackVerifier verifier1("zzz");
  StartWithinAbortedCallbackVerifier verifier2("aaa");
  StartWithinAbortedCallbackVerifier verifier3("eee");

  ResolveRequest req1(host_resolver.get(), "bbb", 40, &verifier1);
  ResolveRequest req2(host_resolver.get(), "eee", 80, &verifier2);
  ResolveRequest req3(host_resolver.get(), "ccc", 90, &verifier3);
  // The jobs start immediately.
  // Wait until all are blocked;
  resolver_proc->WaitFor(3u);
  // Trigger an IP address change.
  NetworkChangeNotifier::NotifyObserversOfIPAddressChangeForTests();
  // This should abort all running jobs.
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(ERR_ABORTED, req1.result());
  EXPECT_EQ(ERR_ABORTED, req2.result());
  EXPECT_EQ(ERR_ABORTED, req3.result());
  // Unblock all calls to proc.
  resolver_proc->SignalMultiple(6u);
  // Run until the re-started requests finish.
  EXPECT_EQ(OK, verifier1.WaitUntilDone());
  EXPECT_EQ(OK, verifier2.WaitUntilDone());
  EXPECT_EQ(OK, verifier3.WaitUntilDone());

  EXPECT_EQ(0u, host_resolver->num_running_jobs_for_tests());
}

// Tests that when the maximum threads is set to 1, requests are dequeued
// in order of priority.
TEST_F(HostResolverImplTest, HigherPriorityRequestsStartedFirst) {
  scoped_refptr<CapturingHostResolverProc> resolver_proc(
      new CapturingHostResolverProc(NULL));

  scoped_ptr<HostResolverImpl> host_resolver(
      CreateSerialHostResolverImpl(resolver_proc));

  // Note that at this point the CapturingHostResolverProc is blocked, so any
  // requests we make will not complete.

  HostResolver::RequestInfo req[] = {
      CreateResolverRequest("req0", LOW),
      CreateResolverRequest("req1", MEDIUM),
      CreateResolverRequest("req2", MEDIUM),
      CreateResolverRequest("req3", LOW),
      CreateResolverRequest("req4", HIGHEST),
      CreateResolverRequest("req5", LOW),
      CreateResolverRequest("req6", LOW),
      CreateResolverRequest("req5", HIGHEST),
  };

  TestCompletionCallback callback[arraysize(req)];
  AddressList addrlist[arraysize(req)];

  // Start all of the requests.
  for (size_t i = 0; i < arraysize(req); ++i) {
    int rv = host_resolver->Resolve(req[i], &addrlist[i],
                                    callback[i].callback(), NULL,
                                    BoundNetLog());
    EXPECT_EQ(ERR_IO_PENDING, rv);
  }

  // Unblock the resolver thread so the requests can run.
  resolver_proc->Signal();

  // Wait for all the requests to complete succesfully.
  for (size_t i = 0; i < arraysize(req); ++i) {
    EXPECT_EQ(OK, callback[i].WaitForResult()) << "i=" << i;
  }

  // Since we have restricted to a single concurrent thread in the jobpool,
  // the requests should complete in order of priority (with the exception
  // of the first request, which gets started right away, since there is
  // nothing outstanding).
  CapturingHostResolverProc::CaptureList capture_list =
      resolver_proc->GetCaptureList();
  ASSERT_EQ(7u, capture_list.size());

  EXPECT_EQ("req0", capture_list[0].hostname);
  EXPECT_EQ("req4", capture_list[1].hostname);
  EXPECT_EQ("req5", capture_list[2].hostname);
  EXPECT_EQ("req1", capture_list[3].hostname);
  EXPECT_EQ("req2", capture_list[4].hostname);
  EXPECT_EQ("req3", capture_list[5].hostname);
  EXPECT_EQ("req6", capture_list[6].hostname);
}

// Try cancelling a request which has not been attached to a job yet.
TEST_F(HostResolverImplTest, CancelPendingRequest) {
  scoped_refptr<CapturingHostResolverProc> resolver_proc(
      new CapturingHostResolverProc(NULL));

  scoped_ptr<HostResolverImpl> host_resolver(
      CreateSerialHostResolverImpl(resolver_proc));

  // Note that at this point the CapturingHostResolverProc is blocked, so any
  // requests we make will not complete.

  HostResolver::RequestInfo req[] = {
      CreateResolverRequest("req0", LOWEST),
      CreateResolverRequest("req1", HIGHEST),  // Will cancel.
      CreateResolverRequest("req2", MEDIUM),
      CreateResolverRequest("req3", LOW),
      CreateResolverRequest("req4", HIGHEST),  // Will cancel.
      CreateResolverRequest("req5", LOWEST),   // Will cancel.
      CreateResolverRequest("req6", MEDIUM),
  };

  TestCompletionCallback callback[arraysize(req)];
  AddressList addrlist[arraysize(req)];
  HostResolver::RequestHandle handle[arraysize(req)];

  // Start all of the requests.
  for (size_t i = 0; i < arraysize(req); ++i) {
    int rv = host_resolver->Resolve(req[i], &addrlist[i],
                                    callback[i].callback(), &handle[i],
                                    BoundNetLog());
    EXPECT_EQ(ERR_IO_PENDING, rv);
  }

  // Cancel some requests
  host_resolver->CancelRequest(handle[1]);
  host_resolver->CancelRequest(handle[4]);
  host_resolver->CancelRequest(handle[5]);
  handle[1] = handle[4] = handle[5] = NULL;

  // Unblock the resolver thread so the requests can run.
  resolver_proc->Signal();

  // Wait for all the requests to complete succesfully.
  for (size_t i = 0; i < arraysize(req); ++i) {
    if (!handle[i])
      continue;  // Don't wait for the requests we cancelled.
    EXPECT_EQ(OK, callback[i].WaitForResult());
  }

  // Verify that they called out the the resolver proc (which runs on the
  // resolver thread) in the expected order.
  CapturingHostResolverProc::CaptureList capture_list =
      resolver_proc->GetCaptureList();
  ASSERT_EQ(4u, capture_list.size());

  EXPECT_EQ("req0", capture_list[0].hostname);
  EXPECT_EQ("req2", capture_list[1].hostname);
  EXPECT_EQ("req6", capture_list[2].hostname);
  EXPECT_EQ("req3", capture_list[3].hostname);
}

// Test that when too many requests are enqueued, old ones start to be aborted.
TEST_F(HostResolverImplTest, QueueOverflow) {
  scoped_refptr<CapturingHostResolverProc> resolver_proc(
      new CapturingHostResolverProc(NULL));

  scoped_ptr<HostResolverImpl> host_resolver(
      CreateSerialHostResolverImpl(resolver_proc));

  // Allow only 3 queued jobs.
  const size_t kMaxPendingJobs = 3u;
  host_resolver->SetMaxQueuedJobs(kMaxPendingJobs);

  // Note that at this point the CapturingHostResolverProc is blocked, so any
  // requests we make will not complete.

  HostResolver::RequestInfo req[] = {
      CreateResolverRequest("req0", LOWEST),
      CreateResolverRequest("req1", HIGHEST),
      CreateResolverRequest("req2", MEDIUM),
      CreateResolverRequest("req3", MEDIUM),

      // At this point, there are 3 enqueued requests.
      // Insertion of subsequent requests will cause evictions
      // based on priority.

      CreateResolverRequest("req4", LOW),      // Evicts itself!
      CreateResolverRequest("req5", MEDIUM),   // Evicts req3
      CreateResolverRequest("req6", HIGHEST),  // Evicts req5.
      CreateResolverRequest("req7", MEDIUM),   // Evicts req2.
  };

  TestCompletionCallback callback[arraysize(req)];
  AddressList addrlist[arraysize(req)];
  HostResolver::RequestHandle handle[arraysize(req)];

  // Start all of the requests.
  for (size_t i = 0; i < arraysize(req); ++i) {
    int rv = host_resolver->Resolve(req[i], &addrlist[i],
                                    callback[i].callback(), &handle[i],
                                    BoundNetLog());
    if (i == 4u)
      EXPECT_EQ(ERR_HOST_RESOLVER_QUEUE_TOO_LARGE, rv);
    else
      EXPECT_EQ(ERR_IO_PENDING, rv) << i;
  }

  // Unblock the resolver thread so the requests can run.
  resolver_proc->Signal();

  // Requests 3, 5, 2 will have been evicted due to queue overflow.
  size_t reqs_expected_to_fail[] = { 2, 3, 5 };
  for (size_t i = 0; i < arraysize(reqs_expected_to_fail); ++i) {
    EXPECT_EQ(ERR_HOST_RESOLVER_QUEUE_TOO_LARGE,
              callback[reqs_expected_to_fail[i]].WaitForResult());
  }

  // The rest should succeed.
  size_t reqs_expected_to_succeed[] = { 0, 1, 6, 7 };
  for (size_t i = 0; i < arraysize(reqs_expected_to_succeed); ++i) {
    EXPECT_EQ(OK, callback[reqs_expected_to_succeed[i]].WaitForResult());
  }

  // Verify that they called out the the resolver proc (which runs on the
  // resolver thread) in the expected order.
  CapturingHostResolverProc::CaptureList capture_list =
      resolver_proc->GetCaptureList();
  ASSERT_EQ(4u, capture_list.size());

  EXPECT_EQ("req0", capture_list[0].hostname);
  EXPECT_EQ("req1", capture_list[1].hostname);
  EXPECT_EQ("req6", capture_list[2].hostname);
  EXPECT_EQ("req7", capture_list[3].hostname);

  // Verify that the evicted (incomplete) requests were not cached.
  EXPECT_EQ(4u, host_resolver->GetHostCache()->size());
}

// Tests that after changing the default AddressFamily to IPV4, requests
// with UNSPECIFIED address family map to IPV4.
TEST_F(HostResolverImplTest, SetDefaultAddressFamily_IPv4) {
  scoped_refptr<CapturingHostResolverProc> resolver_proc(
      new CapturingHostResolverProc(new EchoingHostResolverProc));

  // This HostResolverImpl will only allow 1 outstanding resolve at a time.
  scoped_ptr<HostResolverImpl> host_resolver(
      CreateSerialHostResolverImpl(resolver_proc));

  host_resolver->SetDefaultAddressFamily(ADDRESS_FAMILY_IPV4);

  // Note that at this point the CapturingHostResolverProc is blocked, so any
  // requests we make will not complete.

  HostResolver::RequestInfo req[] = {
      CreateResolverRequestForAddressFamily("h1", MEDIUM,
                                            ADDRESS_FAMILY_UNSPECIFIED),
      CreateResolverRequestForAddressFamily("h1", MEDIUM, ADDRESS_FAMILY_IPV4),
      CreateResolverRequestForAddressFamily("h1", MEDIUM, ADDRESS_FAMILY_IPV6),
  };

  TestCompletionCallback callback[arraysize(req)];
  AddressList addrlist[arraysize(req)];
  HostResolver::RequestHandle handle[arraysize(req)];

  // Start all of the requests.
  for (size_t i = 0; i < arraysize(req); ++i) {
    int rv = host_resolver->Resolve(req[i], &addrlist[i],
                                    callback[i].callback(), &handle[i],
                                    BoundNetLog());
    EXPECT_EQ(ERR_IO_PENDING, rv) << i;
  }

  // Unblock the resolver thread so the requests can run.
  resolver_proc->Signal();

  // Wait for all the requests to complete.
  for (size_t i = 0u; i < arraysize(req); ++i) {
    EXPECT_EQ(OK, callback[i].WaitForResult());
  }

  // Since the requests all had the same priority and we limited the thread
  // count to 1, they should have completed in the same order as they were
  // requested. Moreover, request0 and request1 will have been serviced by
  // the same job.

  CapturingHostResolverProc::CaptureList capture_list =
      resolver_proc->GetCaptureList();
  ASSERT_EQ(2u, capture_list.size());

  EXPECT_EQ("h1", capture_list[0].hostname);
  EXPECT_EQ(ADDRESS_FAMILY_IPV4, capture_list[0].address_family);

  EXPECT_EQ("h1", capture_list[1].hostname);
  EXPECT_EQ(ADDRESS_FAMILY_IPV6, capture_list[1].address_family);

  // Now check that the correct resolved IP addresses were returned.
  // Addresses take the form: 192.x.y.z
  //    x = length of hostname
  //    y = ASCII value of hostname[0]
  //    z = value of address family
  EXPECT_EQ("192.2.104.1:80", FirstAddressToString(addrlist[0]));
  EXPECT_EQ("192.2.104.1:80", FirstAddressToString(addrlist[1]));
  EXPECT_EQ("192.2.104.2:80", FirstAddressToString(addrlist[2]));
  EXPECT_EQ(1u, NumberOfAddresses(addrlist[0]));
  EXPECT_EQ(1u, NumberOfAddresses(addrlist[1]));
  EXPECT_EQ(1u, NumberOfAddresses(addrlist[2]));
}

// This is the exact same test as SetDefaultAddressFamily_IPv4, except the order
// of requests 0 and 1 is flipped, and the default is set to IPv6 in place of
// IPv4.
TEST_F(HostResolverImplTest, SetDefaultAddressFamily_IPv6) {
  scoped_refptr<CapturingHostResolverProc> resolver_proc(
      new CapturingHostResolverProc(new EchoingHostResolverProc));

  scoped_ptr<HostResolverImpl> host_resolver(
      CreateSerialHostResolverImpl(resolver_proc));

  host_resolver->SetDefaultAddressFamily(ADDRESS_FAMILY_IPV6);

  // Note that at this point the CapturingHostResolverProc is blocked, so any
  // requests we make will not complete.

  HostResolver::RequestInfo req[] = {
      CreateResolverRequestForAddressFamily("h1", MEDIUM, ADDRESS_FAMILY_IPV6),
      CreateResolverRequestForAddressFamily("h1", MEDIUM,
                                            ADDRESS_FAMILY_UNSPECIFIED),
      CreateResolverRequestForAddressFamily("h1", MEDIUM, ADDRESS_FAMILY_IPV4),
  };

  TestCompletionCallback callback[arraysize(req)];
  AddressList addrlist[arraysize(req)];
  HostResolver::RequestHandle handle[arraysize(req)];

  // Start all of the requests.
  for (size_t i = 0; i < arraysize(req); ++i) {
    int rv = host_resolver->Resolve(req[i], &addrlist[i],
                                    callback[i].callback(), &handle[i],
                                    BoundNetLog());
    EXPECT_EQ(ERR_IO_PENDING, rv) << i;
  }

  // Unblock the resolver thread so the requests can run.
  resolver_proc->Signal();

  // Wait for all the requests to complete.
  for (size_t i = 0u; i < arraysize(req); ++i) {
    EXPECT_EQ(OK, callback[i].WaitForResult());
  }

  // Since the requests all had the same priority and we limited the thread
  // count to 1, they should have completed in the same order as they were
  // requested. Moreover, request0 and request1 will have been serviced by
  // the same job.

  CapturingHostResolverProc::CaptureList capture_list =
      resolver_proc->GetCaptureList();
  ASSERT_EQ(2u, capture_list.size());

  EXPECT_EQ("h1", capture_list[0].hostname);
  EXPECT_EQ(ADDRESS_FAMILY_IPV6, capture_list[0].address_family);

  EXPECT_EQ("h1", capture_list[1].hostname);
  EXPECT_EQ(ADDRESS_FAMILY_IPV4, capture_list[1].address_family);

  // Now check that the correct resolved IP addresses were returned.
  // Addresses take the form: 192.x.y.z
  //    x = length of hostname
  //    y = ASCII value of hostname[0]
  //    z = value of address family
  EXPECT_EQ("192.2.104.2:80", FirstAddressToString(addrlist[0]));
  EXPECT_EQ("192.2.104.2:80", FirstAddressToString(addrlist[1]));
  EXPECT_EQ("192.2.104.1:80", FirstAddressToString(addrlist[2]));
  EXPECT_EQ(1u, NumberOfAddresses(addrlist[0]));
  EXPECT_EQ(1u, NumberOfAddresses(addrlist[1]));
  EXPECT_EQ(1u, NumberOfAddresses(addrlist[2]));
}

TEST_F(HostResolverImplTest, DisallowNonCachedResponses) {
  AddressList addrlist;

  scoped_refptr<RuleBasedHostResolverProc> resolver_proc(
      new RuleBasedHostResolverProc(NULL));
  resolver_proc->AddRule("just.testing", "192.168.1.42");

  scoped_ptr<HostResolver> host_resolver(
      CreateHostResolverImpl(resolver_proc));

  // First hit will miss the cache.
  HostResolver::RequestInfo info(HostPortPair("just.testing", 80));
  CapturingBoundNetLog log(CapturingNetLog::kUnbounded);
  int err = host_resolver->ResolveFromCache(info, &addrlist, log.bound());
  EXPECT_EQ(ERR_DNS_CACHE_MISS, err);

  // This time, we fetch normally.
  TestCompletionCallback callback;
  err = host_resolver->Resolve(info, &addrlist, callback.callback(), NULL,
                               log.bound());
  EXPECT_EQ(ERR_IO_PENDING, err);
  err = callback.WaitForResult();
  EXPECT_EQ(OK, err);

  // Now we should be able to fetch from the cache.
  err = host_resolver->ResolveFromCache(info, &addrlist, log.bound());
  EXPECT_EQ(OK, err);

  EXPECT_EQ("192.168.1.42:80", FirstAddressToString(addrlist));
  EXPECT_EQ(1u, NumberOfAddresses(addrlist));
}

// Test the retry attempts simulating host resolver proc that takes too long.
TEST_F(HostResolverImplTest, MultipleAttempts) {
  // Total number of attempts would be 3 and we want the 3rd attempt to resolve
  // the host. First and second attempt will be forced to sleep until they get
  // word that a resolution has completed. The 3rd resolution attempt will try
  // to get done ASAP, and won't sleep..
  int kAttemptNumberToResolve = 3;
  int kTotalAttempts = 3;

  scoped_refptr<LookupAttemptHostResolverProc> resolver_proc(
      new LookupAttemptHostResolverProc(
          NULL, kAttemptNumberToResolve, kTotalAttempts));

  HostResolverImpl::ProcTaskParams params = DefaultParams(resolver_proc.get());

  // Specify smaller interval for unresponsive_delay_ for HostResolverImpl so
  // that unit test runs faster. For example, this test finishes in 1.5 secs
  // (500ms * 3).
  params.unresponsive_delay = TimeDelta::FromMilliseconds(500);

  scoped_ptr<HostResolverImpl> host_resolver(
      new HostResolverImpl(HostCache::CreateDefaultCache(),
                           DefaultLimits(),
                           params,
                           scoped_ptr<DnsConfigService>(NULL),
                           NULL));

  // Resolve "host1".
  HostResolver::RequestInfo info(HostPortPair("host1", 70));
  TestCompletionCallback callback;
  AddressList addrlist;
  int rv = host_resolver->Resolve(info, &addrlist, callback.callback(), NULL,
                                  BoundNetLog());
  EXPECT_EQ(ERR_IO_PENDING, rv);

  // Resolve returns -4 to indicate that 3rd attempt has resolved the host.
  EXPECT_EQ(-4, callback.WaitForResult());

  resolver_proc->WaitForAllAttemptsToFinish(TimeDelta::FromMilliseconds(60000));
  MessageLoop::current()->RunAllPending();

  EXPECT_EQ(resolver_proc->total_attempts_resolved(), kTotalAttempts);
  EXPECT_EQ(resolver_proc->resolved_attempt_number(), kAttemptNumberToResolve);
}

DnsConfig CreateValidDnsConfig() {
  IPAddressNumber dns_ip;
  bool rv = ParseIPLiteralToNumber("192.168.1.0", &dns_ip);
  EXPECT_TRUE(rv);

  DnsConfig config;
  config.nameservers.push_back(IPEndPoint(dns_ip,
                                          dns_protocol::kDefaultPort));
  EXPECT_TRUE(config.IsValid());
  return config;
}

// TODO(szym): Test AbortAllInProgressJobs due to DnsConfig change.

// TODO(cbentzel): Test a mix of requests with different HostResolverFlags.

// Test successful and fallback resolutions in HostResolverImpl::DnsTask.
TEST_F(HostResolverImplTest, DnsTask) {
  scoped_refptr<RuleBasedHostResolverProc> resolver_proc(
      new RuleBasedHostResolverProc(NULL));
  scoped_ptr<HostResolverImpl> host_resolver(CreateHostResolverImpl(
      resolver_proc));

  resolver_proc->AddRule("er_succeed", "192.168.1.101");
  resolver_proc->AddRule("nx_succeed", "192.168.1.102");
  resolver_proc->AddSimulatedFailure("ok_fail");
  resolver_proc->AddSimulatedFailure("er_fail");
  resolver_proc->AddSimulatedFailure("nx_fail");

  CountingDelegate delegate;

  // Initially there is no config, so client should not be invoked.
  ResolveRequest req1(host_resolver.get(), "ok_fail", 80, &delegate);

  delegate.WaitForCompletions(1);
  EXPECT_EQ(ERR_NAME_NOT_RESOLVED, req1.result());

  host_resolver->set_dns_client_for_tests(
      CreateMockDnsClient(CreateValidDnsConfig()));

  ResolveRequest req2(host_resolver.get(), "ok_fail", 80, &delegate);
  ResolveRequest req3(host_resolver.get(), "er_fail", 80, &delegate);
  ResolveRequest req4(host_resolver.get(), "nx_fail", 80, &delegate);
  ResolveRequest req5(host_resolver.get(), "er_succeed", 80, &delegate);
  ResolveRequest req6(host_resolver.get(), "nx_succeed", 80, &delegate);

  delegate.WaitForCompletions(6);
  EXPECT_EQ(OK, req2.result());
  // Resolved by MockDnsClient.
  EXPECT_EQ("127.0.0.1:80", FirstAddressToString(req2.addrlist()));
  EXPECT_EQ(ERR_NAME_NOT_RESOLVED, req3.result());
  EXPECT_EQ(ERR_NAME_NOT_RESOLVED, req4.result());
  EXPECT_EQ(OK, req5.result());
  EXPECT_EQ("192.168.1.101:80", FirstAddressToString(req5.addrlist()));
  EXPECT_EQ(OK, req6.result());
  EXPECT_EQ("192.168.1.102:80", FirstAddressToString(req6.addrlist()));
}

TEST_F(HostResolverImplTest, ServeFromHosts) {
  scoped_refptr<RuleBasedHostResolverProc> resolver_proc(
      new RuleBasedHostResolverProc(NULL));
  MockDnsConfigService* config_service = new MockDnsConfigService();
  scoped_ptr<HostResolverImpl> host_resolver(
      CreateHostResolverImplWithDnsConfig(
          resolver_proc,
          scoped_ptr<DnsConfigService>(config_service)));

  resolver_proc->AddSimulatedFailure("*");

  DnsConfig config = CreateValidDnsConfig();
  host_resolver->set_dns_client_for_tests(CreateMockDnsClient(config));

  CountingDelegate delegate;

  ResolveRequest req1(host_resolver.get(), "er_ipv4", 80, &delegate);
  delegate.WaitForCompletions(1);
  EXPECT_EQ(ERR_NAME_NOT_RESOLVED, req1.result());

  IPAddressNumber local_ipv4, local_ipv6;
  ASSERT_TRUE(ParseIPLiteralToNumber("127.0.0.1", &local_ipv4));
  ASSERT_TRUE(ParseIPLiteralToNumber("::1", &local_ipv6));

  DnsHosts hosts;
  hosts[DnsHostsKey("er_ipv4", ADDRESS_FAMILY_IPV4)] = local_ipv4;
  hosts[DnsHostsKey("er_ipv6", ADDRESS_FAMILY_IPV6)] = local_ipv6;
  hosts[DnsHostsKey("er_both", ADDRESS_FAMILY_IPV4)] = local_ipv4;
  hosts[DnsHostsKey("er_both", ADDRESS_FAMILY_IPV6)] = local_ipv6;

  config_service->ChangeConfig(config);
  config_service->ChangeHosts(hosts);

  ResolveRequest req2(host_resolver.get(), "er_ipv4", 80);
  EXPECT_EQ(OK, req2.result());
  EXPECT_EQ("127.0.0.1:80", FirstAddressToString(req2.addrlist()));

  ResolveRequest req3(host_resolver.get(), "er_ipv6", 80);
  EXPECT_EQ(OK, req3.result());
  std::string ipv6string = FirstAddressToString(req3.addrlist());
  if (ipv6string != "UNSUPPORTED")
    EXPECT_EQ("[::1]:80", ipv6string);

  ResolveRequest req4(host_resolver.get(), "er_both", 80);
  EXPECT_EQ(OK, req4.result());
  // Either result is satisfactory. http://crbug.com/117850
  const addrinfo* addr = req4.addrlist().head();
  if (addr->ai_addrlen == sizeof(struct sockaddr_in)) {
    EXPECT_EQ("127.0.0.1", NetAddressToString(addr));
  } else {
    ipv6string = NetAddressToString(addr);
    if (ipv6string != "UNSUPPORTED")
      EXPECT_EQ("::1", NetAddressToString(addr));
  }
  EXPECT_GE(NumberOfAddresses(req4.addrlist()), 1u);

  // Requests with specified AddressFamily.
  HostResolver::RequestInfo info(HostPortPair("er_both", 80));
  info.set_address_family(ADDRESS_FAMILY_IPV4);
  ResolveRequest req5(host_resolver.get(), info);
  EXPECT_EQ(OK, req5.result());
  EXPECT_EQ("127.0.0.1:80", FirstAddressToString(req5.addrlist()));
  EXPECT_EQ(1u, NumberOfAddresses(req5.addrlist()));

  info.set_address_family(ADDRESS_FAMILY_IPV6);
  ResolveRequest req6(host_resolver.get(), info);
  EXPECT_EQ(OK, req6.result());
  ipv6string = FirstAddressToString(req6.addrlist());
  if (ipv6string != "UNSUPPORTED")
    EXPECT_EQ("[::1]:80", ipv6string);
  EXPECT_EQ(1u, NumberOfAddresses(req6.addrlist()));

  // Request with upper case.
  ResolveRequest req7(host_resolver.get(), "er_IPV4", 80);
  EXPECT_EQ(OK, req7.result());
  EXPECT_EQ("127.0.0.1:80", FirstAddressToString(req7.addrlist()));
}

}  // namespace net
