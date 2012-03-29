// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/dns_config_service_win.h"

#include <algorithm>
#include <string>

#include "base/bind.h"
#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/file_path.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "base/synchronization/lock.h"
#include "base/threading/non_thread_safe.h"
#include "base/threading/thread_restrictions.h"
#include "base/utf_string_conversions.h"
#include "base/win/object_watcher.h"
#include "base/win/registry.h"
#include "base/win/windows_version.h"
#include "googleurl/src/url_canon.h"
#include "net/base/net_util.h"
#include "net/base/network_change_notifier.h"
#include "net/dns/dns_protocol.h"
#include "net/dns/file_path_watcher_wrapper.h"
#include "net/dns/serial_worker.h"

#pragma comment(lib, "iphlpapi.lib")

namespace net {

namespace internal {

namespace {

// Registry key paths.
const wchar_t* const kTcpipPath =
    L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters";
const wchar_t* const kTcpip6Path =
    L"SYSTEM\\CurrentControlSet\\Services\\Tcpip6\\Parameters";
const wchar_t* const kDnscachePath =
    L"SYSTEM\\CurrentControlSet\\Services\\Dnscache\\Parameters";
const wchar_t* const kPolicyPath =
    L"SOFTWARE\\Policies\\Microsoft\\Windows NT\\DNSClient";

// Convenience for reading values using RegKey.
class RegistryReader : public base::NonThreadSafe {
 public:
  explicit RegistryReader(const wchar_t* key) {
    // Ignoring the result. |key_.Valid()| will catch failures.
    key_.Open(HKEY_LOCAL_MACHINE, key, KEY_QUERY_VALUE);
  }

  bool ReadString(const wchar_t* name,
                  DnsSystemSettings::RegString* out) const {
    DCHECK(CalledOnValidThread());
    out->set = false;
    if (!key_.Valid()) {
      // Assume that if the |key_| is invalid then the key is missing.
      return true;
    }
    LONG result = key_.ReadValue(name, &out->value);
    if (result == ERROR_SUCCESS) {
      out->set = true;
      return true;
    }
    return (result == ERROR_FILE_NOT_FOUND);
  }

  bool ReadDword(const wchar_t* name,
                 DnsSystemSettings::RegDword* out) const {
    DCHECK(CalledOnValidThread());
    out->set = false;
    if (!key_.Valid()) {
      // Assume that if the |key_| is invalid then the key is missing.
      return true;
    }
    LONG result = key_.ReadValueDW(name, &out->value);
    if (result == ERROR_SUCCESS) {
      out->set = true;
      return true;
    }
    return (result == ERROR_FILE_NOT_FOUND);
  }

 private:
  base::win::RegKey key_;

  DISALLOW_COPY_AND_ASSIGN(RegistryReader);
};


// Watches a single registry key for changes.
class RegistryWatcher : public base::win::ObjectWatcher::Delegate,
                        public base::NonThreadSafe {
 public:
  typedef base::Callback<void(bool succeeded)> CallbackType;
  RegistryWatcher() {}

  bool Watch(const wchar_t* key, const CallbackType& callback) {
    DCHECK(CalledOnValidThread());
    DCHECK(!callback.is_null());
    Cancel();
    if (key_.Open(HKEY_LOCAL_MACHINE, key, KEY_NOTIFY) != ERROR_SUCCESS)
      return false;
    if (key_.StartWatching() != ERROR_SUCCESS)
      return false;
    if (!watcher_.StartWatching(key_.watch_event(), this))
      return false;
    callback_ = callback;
    return true;
  }

  bool IsWatching() const {
    DCHECK(CalledOnValidThread());
    return !callback_.is_null();
  }

  void Cancel() {
    DCHECK(CalledOnValidThread());
    callback_.Reset();
    if (key_.Valid()) {
      watcher_.StopWatching();
      key_.StopWatching();
      key_.Close();
    }
  }

  virtual void OnObjectSignaled(HANDLE object) OVERRIDE {
    DCHECK(CalledOnValidThread());
    bool succeeded = (key_.StartWatching() == ERROR_SUCCESS) &&
                      watcher_.StartWatching(key_.watch_event(), this);
    CallbackType callback = callback_;
    if (!succeeded)
      Cancel();
    if (!callback.is_null())
      callback.Run(succeeded);
  }

 private:
  CallbackType callback_;
  base::win::RegKey key_;
  base::win::ObjectWatcher watcher_;

  DISALLOW_COPY_AND_ASSIGN(RegistryWatcher);
};

// Returns NULL if failed.
scoped_ptr_malloc<IP_ADAPTER_ADDRESSES> ReadIpHelper(ULONG flags) {
  base::ThreadRestrictions::AssertIOAllowed();

  scoped_ptr_malloc<IP_ADAPTER_ADDRESSES> out;
  ULONG len = 15000;  // As recommended by MSDN for GetAdaptersAddresses.
  UINT rv = ERROR_BUFFER_OVERFLOW;
  // Try up to three times.
  for (unsigned tries = 0; (tries < 3) && (rv == ERROR_BUFFER_OVERFLOW);
       tries++) {
    out.reset(reinterpret_cast<PIP_ADAPTER_ADDRESSES>(malloc(len)));
    rv = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, out.get(), &len);
  }
  if (rv != NO_ERROR)
    out.reset();
  return out.Pass();
}

// Converts a string16 domain name to ASCII, possibly using punycode.
// Returns true if the conversion succeeds and output is not empty. In case of
// failure, |domain| might become dirty.
bool ParseDomainASCII(const string16& widestr, std::string* domain) {
  DCHECK(domain);
  if (widestr.empty())
    return false;

  // Check if already ASCII.
  if (IsStringASCII(widestr)) {
    *domain = UTF16ToASCII(widestr);
    return true;
  }

  // Otherwise try to convert it from IDN to punycode.
  const int kInitialBufferSize = 256;
  url_canon::RawCanonOutputT<char16, kInitialBufferSize> punycode;
  if (!url_canon::IDNToASCII(widestr.data(), widestr.length(), &punycode))
    return false;

  // |punycode_output| should now be ASCII; convert it to a std::string.
  // (We could use UTF16ToASCII() instead, but that requires an extra string
  // copy. Since ASCII is a subset of UTF8 the following is equivalent).
  bool success = UTF16ToUTF8(punycode.data(), punycode.length(), domain);
  DCHECK(success);
  DCHECK(IsStringASCII(*domain));
  return success && !domain->empty();
}

}  // namespace

bool ParseSearchList(const string16& value, std::vector<std::string>* output) {
  DCHECK(output);
  if (value.empty())
    return false;

  output->clear();

  // If the list includes an empty hostname (",," or ", ,"), it is terminated.
  // Although nslookup and network connection property tab ignore such
  // fragments ("a,b,,c" becomes ["a", "b", "c"]), our reference is getaddrinfo
  // (which sees ["a", "b"]). WMI queries also return a matching search list.
  std::vector<string16> woutput;
  base::SplitString(value, ',', &woutput);
  for (size_t i = 0; i < woutput.size(); ++i) {
    // Convert non-ASCII to punycode, although getaddrinfo does not properly
    // handle such suffixes.
    const string16& t = woutput[i];
    std::string parsed;
    if (!ParseDomainASCII(t, &parsed))
      break;
    output->push_back(parsed);
  }
  return !output->empty();
}

bool ConvertSettingsToDnsConfig(const DnsSystemSettings& settings,
                                DnsConfig* config) {
  *config = DnsConfig();

  // Use GetAdapterAddresses to get effective DNS server order and
  // connection-specific DNS suffix. Ignore disconnected and loopback adapters.
  // The order of adapters is the network binding order, so stick to the
  // first good adapter.
  for (const IP_ADAPTER_ADDRESSES* adapter = settings.addresses.get();
       adapter != NULL && config->nameservers.empty();
       adapter = adapter->Next) {
    if (adapter->OperStatus != IfOperStatusUp)
      continue;
    if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
      continue;

    for (const IP_ADAPTER_DNS_SERVER_ADDRESS* address =
             adapter->FirstDnsServerAddress;
         address != NULL;
         address = address->Next) {
      IPEndPoint ipe;
      if (ipe.FromSockAddr(address->Address.lpSockaddr,
                           address->Address.iSockaddrLength)) {
        // Override unset port.
        if (!ipe.port())
          ipe = IPEndPoint(ipe.address(), dns_protocol::kDefaultPort);
        config->nameservers.push_back(ipe);
      } else {
        return false;
      }
    }

    // IP_ADAPTER_ADDRESSES in Vista+ has a search list at |FirstDnsSuffix|,
    // but it came up empty in all trials.
    // |DnsSuffix| stores the effective connection-specific suffix, which is
    // obtained via DHCP (regkey: Tcpip\Parameters\Interfaces\{XXX}\DhcpDomain)
    // or specified by the user (regkey: Tcpip\Parameters\Domain).
    std::string dns_suffix;
    if (ParseDomainASCII(adapter->DnsSuffix, &dns_suffix))
      config->search.push_back(dns_suffix);
  }

  if (config->nameservers.empty())
    return false;  // No point continuing.

  // Windows always tries a multi-label name "as is" before using suffixes.
  config->ndots = 1;

  if (!settings.append_to_multi_label_name.set) {
    // The default setting is true for XP, false for Vista+.
    if (base::win::GetVersion() >= base::win::VERSION_VISTA) {
      config->append_to_multi_label_name = false;
    } else {
      config->append_to_multi_label_name = true;
    }
  } else {
    config->append_to_multi_label_name =
        (settings.append_to_multi_label_name.value != 0);
  }

  // SearchList takes precedence, so check it first.
  if (settings.policy_search_list.set) {
    std::vector<std::string> search;
    if (ParseSearchList(settings.policy_search_list.value, &search)) {
      config->search.swap(search);
      return true;
    }
    // Even if invalid, the policy disables the user-specified setting below.
  } else if (settings.tcpip_search_list.set) {
    std::vector<std::string> search;
    if (ParseSearchList(settings.tcpip_search_list.value, &search)) {
      config->search.swap(search);
      return true;
    }
  }

  if (!settings.tcpip_domain.set)
    return true;

  std::string primary_suffix;
  if (!ParseDomainASCII(settings.tcpip_domain.value, &primary_suffix))
    return true;  // No primary suffix, hence no devolution.

  // Primary suffix goes in front.
  config->search.insert(config->search.begin(), primary_suffix);

  // Devolution is determined by precedence: policy > dnscache > tcpip.
  // |enabled|: UseDomainNameDevolution and |level|: DomainNameDevolutionLevel
  // are overridden independently.
  DnsSystemSettings::DevolutionSetting devolution = settings.policy_devolution;

  if (!devolution.enabled.set)
    devolution.enabled = settings.dnscache_devolution.enabled;
  if (!devolution.enabled.set)
    devolution.enabled = settings.tcpip_devolution.enabled;
  if (devolution.enabled.set && (devolution.enabled.value == 0))
    return true;  // Devolution disabled.

  // By default devolution is enabled.

  if (!devolution.level.set)
    devolution.level = settings.dnscache_devolution.level;
  if (!devolution.level.set)
    devolution.level = settings.tcpip_devolution.level;

  // After the recent update, Windows will try to determine a safe default
  // value by comparing the forest root domain (FRD) to the primary suffix.
  // See http://support.microsoft.com/kb/957579 for details.
  // For now, if the level is not set, we disable devolution, assuming that
  // we will fallback to the system getaddrinfo anyway. This might cause
  // performance loss for resolutions which depend on the system default
  // devolution setting.
  //
  // If the level is explicitly set below 2, devolution is disabled.
  if (!devolution.level.set || devolution.level.value < 2)
    return true;  // Devolution disabled.

  // Devolve the primary suffix. This naive logic matches the observed
  // behavior (see also ParseSearchList). If a suffix is not valid, it will be
  // discarded when the fully-qualified name is converted to DNS format.

  unsigned num_dots = std::count(primary_suffix.begin(),
                                 primary_suffix.end(), '.');

  for (size_t offset = 0; num_dots >= devolution.level.value; --num_dots) {
    offset = primary_suffix.find('.', offset + 1);
    config->search.push_back(primary_suffix.substr(offset + 1));
  }

  return true;
}

// Watches registry for changes and reads config from registry and IP helper.
// Reading and opening of reg keys is always performed on WorkerPool. Setting
// up watches requires IO loop.
class DnsConfigServiceWin::ConfigReader : public SerialWorker {
 public:
  explicit ConfigReader(DnsConfigServiceWin* service)
      : service_(service),
        success_(false) {}

  bool Watch() {
    DCHECK(loop()->BelongsToCurrentThread());

    RegistryWatcher::CallbackType callback =
        base::Bind(&ConfigReader::OnChange, base::Unretained(this));

    // The Tcpip key must be present.
    if (!tcpip_watcher_.Watch(kTcpipPath, callback))
      return false;

    // Watch for IPv6 nameservers.
    tcpip6_watcher_.Watch(kTcpip6Path, callback);

    // DNS suffix search list and devolution can be configured via group
    // policy which sets this registry key. If the key is missing, the policy
    // does not apply, and the DNS client uses Tcpip and Dnscache settings.
    // If a policy is installed, DnsConfigService will need to be restarted.
    // BUG=99509

    dnscache_watcher_.Watch(kDnscachePath, callback);
    policy_watcher_.Watch(kPolicyPath, callback);

    WorkNow();
    return true;
  }

  void Cancel() {
    DCHECK(loop()->BelongsToCurrentThread());
    SerialWorker::Cancel();
    policy_watcher_.Cancel();
    dnscache_watcher_.Cancel();
    tcpip6_watcher_.Cancel();
    tcpip_watcher_.Cancel();
  }

 private:
  virtual ~ConfigReader() {
    DCHECK(IsCancelled());
  }

  void OnChange(bool succeeded) {
    DCHECK(loop()->BelongsToCurrentThread());
    if (!IsCancelled())
      service_->InvalidateConfig();
    // We don't trust a config that we cannot watch in the future.
    // TODO(szym): re-start watcher if that makes sense. http://crbug.com/116139
    if (succeeded)
      WorkNow();
    else
      LOG(ERROR) << "Failed to watch DNS config";
  }

  bool ReadDevolutionSetting(const RegistryReader& reader,
                             DnsSystemSettings::DevolutionSetting& setting) {
    return reader.ReadDword(L"UseDomainNameDevolution", &setting.enabled) &&
           reader.ReadDword(L"DomainNameDevolutionLevel", &setting.level);
  }

  virtual void DoWork() OVERRIDE {
    // Should be called on WorkerPool.
    success_ = false;

    DnsSystemSettings settings;
    memset(&settings, 0, sizeof(settings));
    settings.addresses = ReadIpHelper(GAA_FLAG_SKIP_ANYCAST |
                                      GAA_FLAG_SKIP_UNICAST |
                                      GAA_FLAG_SKIP_MULTICAST |
                                      GAA_FLAG_SKIP_FRIENDLY_NAME);
    if (!settings.addresses.get())
      return;  // no point reading the rest

    RegistryReader tcpip_reader(kTcpipPath);
    RegistryReader tcpip6_reader(kTcpip6Path);
    RegistryReader dnscache_reader(kDnscachePath);
    RegistryReader policy_reader(kPolicyPath);

    if (!policy_reader.ReadString(L"SearchList",
                                  &settings.policy_search_list))
      return;

    if (!tcpip_reader.ReadString(L"SearchList", &settings.tcpip_search_list))
      return;

    if (!tcpip_reader.ReadString(L"Domain", &settings.tcpip_domain))
      return;

    if (!ReadDevolutionSetting(policy_reader, settings.policy_devolution))
      return;

    if (!ReadDevolutionSetting(dnscache_reader,
                               settings.dnscache_devolution))
      return;

    if (!ReadDevolutionSetting(tcpip_reader, settings.tcpip_devolution))
      return;

    if (!policy_reader.ReadDword(L"AppendToMultiLabelName",
                                 &settings.append_to_multi_label_name))
      return;

    success_ = ConvertSettingsToDnsConfig(settings, &dns_config_);
  }

  virtual void OnWorkFinished() OVERRIDE {
    DCHECK(loop()->BelongsToCurrentThread());
    DCHECK(!IsCancelled());
    if (success_) {
      service_->OnConfigRead(dns_config_);
    } else {
      LOG(WARNING) << "Failed to read config.";
    }
  }

  DnsConfigServiceWin* service_;
  // Written in DoRead(), read in OnReadFinished(). No locking required.
  DnsConfig dns_config_;
  bool success_;

  RegistryWatcher tcpip_watcher_;
  RegistryWatcher tcpip6_watcher_;
  RegistryWatcher dnscache_watcher_;
  RegistryWatcher policy_watcher_;
};

FilePath GetHostsPath() {
  TCHAR buffer[MAX_PATH];
  UINT rc = GetSystemDirectory(buffer, MAX_PATH);
  DCHECK(0 < rc && rc < MAX_PATH);
  return FilePath(buffer).Append(FILE_PATH_LITERAL("drivers\\etc\\hosts"));
}

// An extension for DnsHostsReader which also watches the HOSTS file,
// reads local name from GetComputerNameEx, local IP from GetAdaptersAddresses,
// and observes changes to local IP address.
class DnsConfigServiceWin::HostsReader
    : public DnsHostsReader,
      public NetworkChangeNotifier::IPAddressObserver {
 public:
  explicit HostsReader(DnsConfigServiceWin* service)
      : DnsHostsReader(GetHostsPath()), service_(service) {
  }

  bool Watch() {
    DCHECK(loop()->BelongsToCurrentThread());
    DCHECK(!IsCancelled());

    // In case the reader is restarted, remove it from the observer list.
    NetworkChangeNotifier::RemoveIPAddressObserver(this);

    if (!hosts_watcher_.Watch(path(),
                              base::Bind(&HostsReader::OnHostsChanged,
                                         base::Unretained(this)))) {
      return false;
    }
    NetworkChangeNotifier::AddIPAddressObserver(this);
    WorkNow();
    return true;
  }

  // Cancels the underlying SerialWorker. Cannot be undone.
  void Cancel() {
    DnsHostsReader::Cancel();
    hosts_watcher_.Cancel();
    NetworkChangeNotifier::RemoveIPAddressObserver(this);
  }

 private:
  virtual void OnIPAddressChanged() OVERRIDE {
    DCHECK(loop()->BelongsToCurrentThread());
    service_->InvalidateHosts();
    if (!hosts_watcher_.IsWatching())
      return;
    WorkNow();
  }

  void OnHostsChanged(bool succeeded) {
    DCHECK(loop()->BelongsToCurrentThread());
    service_->InvalidateHosts();
    if (succeeded)
      WorkNow();
    else
      LOG(ERROR) << "Failed to watch DNS hosts";
  }

  virtual void DoWork() OVERRIDE {
    DnsHostsReader::DoWork();

    if (!success_)
      return;

    success_ = false;

    // Default address of "localhost" and local computer name can be overridden
    // by the HOSTS file, but if it's not there, then we need to fill it in.

    const unsigned char kIPv4Localhost[] = { 127, 0, 0, 1 };
    const unsigned char kIPv6Localhost[] = { 0, 0, 0, 0, 0, 0, 0, 0,
                                             0, 0, 0, 0, 0, 0, 0, 1 };
    IPAddressNumber loopback_ipv4(kIPv4Localhost,
                                  kIPv4Localhost + arraysize(kIPv4Localhost));
    IPAddressNumber loopback_ipv6(kIPv6Localhost,
                                  kIPv6Localhost + arraysize(kIPv6Localhost));

    // This does not override any pre-existing entries from the HOSTS file.
    dns_hosts_.insert(
        std::make_pair(DnsHostsKey("localhost", ADDRESS_FAMILY_IPV4),
                       loopback_ipv4));
    dns_hosts_.insert(
        std::make_pair(DnsHostsKey("localhost", ADDRESS_FAMILY_IPV6),
                       loopback_ipv6));

    WCHAR buffer[MAX_PATH];
    DWORD size = MAX_PATH;
    std::string localname;
    if (!GetComputerNameExW(ComputerNameDnsHostname, buffer, &size) ||
        !ParseDomainASCII(buffer, &localname)) {
      LOG(ERROR) << "Failed to read local computer name";
      return;
    }
    StringToLowerASCII(&localname);

    bool have_ipv4 =
        dns_hosts_.count(DnsHostsKey(localname, ADDRESS_FAMILY_IPV4)) > 0;
    bool have_ipv6 =
        dns_hosts_.count(DnsHostsKey(localname, ADDRESS_FAMILY_IPV6)) > 0;

    if (have_ipv4 && have_ipv6) {
      success_ = true;
      return;
    }

    scoped_ptr_malloc<IP_ADAPTER_ADDRESSES> addresses =
        ReadIpHelper(GAA_FLAG_SKIP_ANYCAST |
                     GAA_FLAG_SKIP_DNS_SERVER |
                     GAA_FLAG_SKIP_MULTICAST |
                     GAA_FLAG_SKIP_FRIENDLY_NAME);
    if (!addresses.get())
      return;

    // The order of adapters is the network binding order, so stick to the
    // first good adapter for each family.
    for (const IP_ADAPTER_ADDRESSES* adapter = addresses.get();
         adapter != NULL && (!have_ipv4 || !have_ipv6);
         adapter = adapter->Next) {
      if (adapter->OperStatus != IfOperStatusUp)
        continue;
      if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
        continue;

      for (const IP_ADAPTER_UNICAST_ADDRESS* address =
               adapter->FirstUnicastAddress;
           address != NULL;
           address = address->Next) {
        IPEndPoint ipe;
        if (!ipe.FromSockAddr(address->Address.lpSockaddr,
                              address->Address.iSockaddrLength)) {
          return;
        }
        if (!have_ipv4 && (ipe.GetFamily() == AF_INET)) {
          have_ipv4 = true;
          dns_hosts_[DnsHostsKey(localname, ADDRESS_FAMILY_IPV4)] =
              ipe.address();
        } else if (!have_ipv6 && (ipe.GetFamily() == AF_INET6)) {
          have_ipv6 = true;
          dns_hosts_[DnsHostsKey(localname, ADDRESS_FAMILY_IPV6)] =
              ipe.address();
        }
      }
    }

    success_ = true;
  }

  virtual void OnWorkFinished() OVERRIDE {
    DCHECK(loop()->BelongsToCurrentThread());
    if (!success_ || !hosts_watcher_.IsWatching())
      return;
    service_->OnHostsRead(dns_hosts_);
  }

  DnsConfigServiceWin* service_;
  FilePathWatcherWrapper hosts_watcher_;

  DISALLOW_COPY_AND_ASSIGN(HostsReader);
};


DnsConfigServiceWin::DnsConfigServiceWin()
    : config_reader_(new ConfigReader(this)),
      hosts_reader_(new HostsReader(this)) {}

DnsConfigServiceWin::~DnsConfigServiceWin() {
  DCHECK(CalledOnValidThread());
  config_reader_->Cancel();
  hosts_reader_->Cancel();
}

void DnsConfigServiceWin::Watch(const CallbackType& callback) {
  DCHECK(CalledOnValidThread());
  DCHECK(!callback.is_null());
  set_callback(callback);

  // This is done only once per lifetime so open the keys and file watcher
  // handles on this thread.
  // TODO(szym): Should/can this be avoided? http://crbug.com/114223
  base::ThreadRestrictions::ScopedAllowIO allow_io;

  if (!config_reader_->Watch()) {
    LOG(ERROR) << "Failed to start watching DNS config";
    InvalidateConfig();
  }

  if (!hosts_reader_->Watch()) {
    LOG(ERROR) << "Failed to start watching HOSTS";
    InvalidateHosts();
  }
}

}  // namespace internal

// static
scoped_ptr<DnsConfigService> DnsConfigService::CreateSystemService() {
  return scoped_ptr<DnsConfigService>(new internal::DnsConfigServiceWin());
}

}  // namespace net

