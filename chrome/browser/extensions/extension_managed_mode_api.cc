// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the Chrome Extensions Managed Mode API.

#include "chrome/browser/extensions/extension_managed_mode_api.h"

#include <string>

#include "chrome/browser/managed_mode.h"
#include "chrome/browser/extensions/extension_preference_api_constants.h"

namespace {

// Key to report whether the attempt to enter managed mode succeeded.
const char kEnterSuccessKey[] = "success";

}  // namespace

namespace keys = extension_preference_api_constants;

GetManagedModeFunction::~GetManagedModeFunction() { }

bool GetManagedModeFunction::RunImpl() {
  bool in_managed_mode = ManagedMode::IsInManagedMode();

  scoped_ptr<DictionaryValue> result(new DictionaryValue);
  result->SetBoolean(keys::kValue, in_managed_mode);
  result_.reset(result.release());
  return true;
}

EnterManagedModeFunction::~EnterManagedModeFunction() { }

bool EnterManagedModeFunction::RunImpl() {
  bool confirmed = true;
  if (!ManagedMode::IsInManagedMode()) {
    // TODO(pamg): WIP. Show modal password dialog and save hashed password. Set
    //     |confirmed| to false if user cancels dialog.

    confirmed = ManagedMode::EnterManagedMode(profile());
  }

  scoped_ptr<DictionaryValue> result(new DictionaryValue);
  result->SetBoolean(kEnterSuccessKey, confirmed);
  result_.reset(result.release());
  return true;
}
