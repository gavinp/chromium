// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/declarative_webrequest/webrequest_action.h"

#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {
const char kCancelRequestType[] = "experimental.webRequest.CancelRequest";
const char kUnknownActionType[] = "unknownType";
}  // namespace

namespace extensions {

TEST(WebRequestActionTest, CreateAction) {
  std::string error;
  scoped_ptr<WebRequestAction> result;

  // Test wrong data type passed.
  error.clear();
  base::ListValue empty_list;
  result = WebRequestAction::Create(empty_list, &error);
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(result.get());

  // Test missing instanceType element.
  base::DictionaryValue input;
  error.clear();
  result = WebRequestAction::Create(input, &error);
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(result.get());

  // Test wrong instanceType element.
  input.SetString("instanceType", kUnknownActionType);
  error.clear();
  result = WebRequestAction::Create(input, &error);
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(result.get());

  // Test success
  input.SetString("instanceType", kCancelRequestType);
  error.clear();
  result = WebRequestAction::Create(input, &error);
  EXPECT_TRUE(error.empty());
  ASSERT_TRUE(result.get());
  EXPECT_EQ(WebRequestAction::ACTION_CANCEL_REQUEST, result->GetType());
}

TEST(WebRequestActionTest, CreateActionSet) {
  std::string error;
  scoped_ptr<WebRequestActionSet> result;

  WebRequestActionSet::AnyVector input;

  // Test empty input.
  error.clear();
  result = WebRequestActionSet::Create(input, &error);
  EXPECT_TRUE(error.empty());
  ASSERT_TRUE(result.get());
  EXPECT_TRUE(result->actions().empty());

  DictionaryValue correct_action;
  correct_action.SetString("instanceType", kCancelRequestType);
  DictionaryValue incorrect_action;
  incorrect_action.SetString("instanceType", kUnknownActionType);

  // Test success.
  linked_ptr<json_schema_compiler::any::Any> action1 = make_linked_ptr(
      new json_schema_compiler::any::Any);
  action1->Init(correct_action);
  input.push_back(action1);
  error.clear();
  result = WebRequestActionSet::Create(input, &error);
  EXPECT_TRUE(error.empty());
  ASSERT_TRUE(result.get());
  ASSERT_EQ(1u, result->actions().size());
  EXPECT_EQ(WebRequestAction::ACTION_CANCEL_REQUEST,
            result->actions()[0]->GetType());

  // Test failure.
  linked_ptr<json_schema_compiler::any::Any> action2 = make_linked_ptr(
      new json_schema_compiler::any::Any);
  action2->Init(incorrect_action);
  input.push_back(action2);
  error.clear();
  result = WebRequestActionSet::Create(input, &error);
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(result.get());
}

}  // namespace extensions
