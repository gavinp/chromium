// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/declarative/declarative_api.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/values.h"
#include "chrome/browser/extensions/api/declarative/rules_registry_service.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/api/experimental.declarative.h"
#include "content/public/browser/browser_thread.h"

using extensions::api::experimental_declarative::Rule;

namespace AddRules = extensions::api::experimental_declarative::AddRules;
namespace GetRules = extensions::api::experimental_declarative::GetRules;
namespace RemoveRules = extensions::api::experimental_declarative::RemoveRules;

namespace {

// Adds all entries from |list| to |out|. Assumes that all entries of |list|
// are strings. Returns true if successful.
bool AddAllStringValues(ListValue* list, std::vector<std::string>* out) {
  for (ListValue::iterator i = list->begin(); i != list->end(); ++i) {
    std::string value;
    if (!(*i)->GetAsString(&value))
      return false;
    out->push_back(value);
  }
  return true;
}

}  // namespace

namespace extensions {

RulesFunction::RulesFunction() : rules_registry_(NULL) {}

RulesFunction::~RulesFunction() {}

bool RulesFunction::RunImpl() {
  std::string event_name;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &event_name));

  RulesRegistryService* rules_registry_service =
      profile()->GetExtensionService()->GetRulesRegistryService();
  rules_registry_ = rules_registry_service->GetRulesRegistry(event_name);
  // Raw access to this function is not available to extensions, therefore
  // there should never be a request for a nonexisting rules registry.
  EXTENSION_FUNCTION_VALIDATE(rules_registry_);

  if (content::BrowserThread::CurrentlyOn(rules_registry_->GetOwnerThread())) {
    RunImplOnCorrectThread();
    SendResponseOnUIThread();
  } else {
    content::BrowserThread::PostTaskAndReply(
        rules_registry_->GetOwnerThread(), FROM_HERE,
        base::Bind(base::IgnoreResult(&RulesFunction::RunImplOnCorrectThread),
                   this),
        base::Bind(&RulesFunction::SendResponseOnUIThread, this));
  }

  return true;
}

void RulesFunction::SendResponseOnUIThread() {
  SendResponse(error_.empty());
}

bool AddRulesFunction::RunImplOnCorrectThread() {
  scoped_ptr<AddRules::Params> params(AddRules::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  error_ = rules_registry_->AddRules(extension_id(), params->rules);

  if (error_.empty())
    result_.reset(AddRules::Result::Create(params->rules));

  return error_.empty();
}

bool RemoveRulesFunction::RunImplOnCorrectThread() {
  scoped_ptr<RemoveRules::Params> params(RemoveRules::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  if (params->rule_identifiers.get()) {
    error_ = rules_registry_->RemoveRules(extension_id(),
                                          *params->rule_identifiers);
  } else {
    error_ = rules_registry_->RemoveAllRules(extension_id());
  }

  return error_.empty();
}

bool GetRulesFunction::RunImplOnCorrectThread() {
  scoped_ptr<RemoveRules::Params> params(RemoveRules::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  std::vector<linked_ptr<Rule> > rules;
  if (params->rule_identifiers.get()) {
    error_ = rules_registry_->GetRules(extension_id(),
                                       *params->rule_identifiers,
                                       &rules);
  } else {
    error_ = rules_registry_->GetAllRules(extension_id(), &rules);
  }

  if (error_.empty())
    result_.reset(GetRules::Result::Create(rules));

  return error_.empty();
}

}  // namespace extensions
