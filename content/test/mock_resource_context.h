// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_TEST_MOCK_RESOURCE_CONTEXT_H_
#define CONTENT_TEST_MOCK_RESOURCE_CONTEXT_H_
#pragma once

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "content/public/browser/resource_context.h"

namespace content {

class MockResourceContext : public ResourceContext {
 public:
  MockResourceContext();
  explicit MockResourceContext(net::URLRequestContext* context);
  virtual ~MockResourceContext();

  void set_request_context(net::URLRequestContext* context) {
    test_request_context_ = context;
  }

  // ResourceContext implementation:
  virtual net::HostResolver* GetHostResolver() OVERRIDE;
  virtual net::URLRequestContext* GetRequestContext() OVERRIDE;

 private:
  scoped_refptr<net::URLRequestContext> test_request_context_;

  DISALLOW_COPY_AND_ASSIGN(MockResourceContext);
};

}  // namespace content

#endif  // CONTENT_TEST_MOCK_RESOURCE_CONTEXT_H_
