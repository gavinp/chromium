// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/importer/importer_data_types.h"

namespace importer {

ProfileInfo::ProfileInfo()
    : importer_type(NONE_IMPORTER),
      services_supported(0) {
}

ProfileInfo::~ProfileInfo() {
}

}  // namespace importer
