// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/spdy_settings_storage.h"

#include <utility>

namespace net {

SpdySettingsStorage::SpdySettingsStorage() {
}

SpdySettingsStorage::~SpdySettingsStorage() {
}

const SpdySettings& SpdySettingsStorage::Get(
    const HostPortPair& host_port_pair) const {
  SettingsMap::const_iterator it = settings_map_.find(host_port_pair);
  if (it == settings_map_.end()) {
    CR_DEFINE_STATIC_LOCAL(SpdySettings, kEmpty, ());
    return kEmpty;
  }
  return it->second;
}

void SpdySettingsStorage::Set(const HostPortPair& host_port_pair,
                              const SpdySettings& settings) {
  SpdySettings persistent_settings;

  // Iterate through the list, and only copy those settings which are marked
  // for persistence.
  SpdySettings::const_iterator it;
  for (it = settings.begin(); it != settings.end(); ++it) {
    SettingsFlagsAndId id = it->first;
    if (id.flags() & SETTINGS_FLAG_PLEASE_PERSIST) {
      SettingsFlagsAndId new_id(SETTINGS_FLAG_PERSISTED, id.id());
      persistent_settings.push_back(std::make_pair(new_id, it->second));
    }
  }

  // If we didn't persist anything, then we are done.
  if (persistent_settings.empty())
    return;

  settings_map_[host_port_pair] = persistent_settings;
}

void SpdySettingsStorage::Clear() {
  settings_map_.clear();
}

}  // namespace net

