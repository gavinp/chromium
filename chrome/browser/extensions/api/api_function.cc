// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/api_function.h"

#include "base/bind.h"
#include "chrome/browser/extensions/api/api_resource_controller.h"
#include "chrome/browser/extensions/api/api_resource_event_notifier.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace extensions {

bool AsyncIOAPIFunction::RunImpl() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  extension_service_ = profile()->GetExtensionService();

  if (!Prepare()) {
    return false;
  }
  bool rv = BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&AsyncIOAPIFunction::WorkOnIOThread, this));
  DCHECK(rv);
  return true;
}

void AsyncIOAPIFunction::WorkOnIOThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  Work();
  bool rv = BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&AsyncIOAPIFunction::RespondOnUIThread, this));
  DCHECK(rv);
}

void AsyncIOAPIFunction::RespondOnUIThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  SendResponse(Respond());
}

int AsyncIOAPIFunction::ExtractSrcId(size_t argument_position) {
  scoped_ptr<DictionaryValue> options(new DictionaryValue());
  if (args_->GetSize() > argument_position) {
    DictionaryValue* temp_options = NULL;
    if (args_->GetDictionary(argument_position, &temp_options))
      options.reset(temp_options->DeepCopy());
  }

  // If we tacked on a srcId to the options object, pull it out here to provide
  // to the Socket.
  int src_id = -1;
  if (options->HasKey(kSrcIdKey)) {
    EXTENSION_FUNCTION_VALIDATE(options->GetInteger(kSrcIdKey, &src_id));
  }

  return src_id;
}

APIResourceEventNotifier* AsyncIOAPIFunction::CreateEventNotifier(int src_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  return new APIResourceEventNotifier(
      profile()->GetExtensionEventRouter(), profile(), extension_id(),
      src_id, source_url());
}

APIResourceController* AsyncIOAPIFunction::controller() {
  // ExtensionService's APIResourceController is set exactly once, long before
  // this code is reached, so it's safe to access it on either the IO or UI
  // thread.
  return extension_service_->api_resource_controller();
}

}  // namespace extensions
