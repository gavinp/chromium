// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_IDENTITY_IDENTITY_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_IDENTITY_IDENTITY_API_H_
#pragma once

#include <string>

#include "base/memory/weak_ptr.h"
#include "chrome/browser/extensions/app_notify_channel_setup.h"
#include "chrome/browser/extensions/extension_function.h"
#include "chrome/common/net/gaia/oauth2_mint_token_flow.h"

class GoogleServiceAuthError;

namespace extensions {

class GetAuthTokenFunction : public AsyncExtensionFunction,
                             public OAuth2MintTokenFlow::Delegate {
 public:
  GetAuthTokenFunction();

 private:
  virtual ~GetAuthTokenFunction();
  virtual bool RunImpl() OVERRIDE;

  // OAuth2MintTokenFlow::Delegate implementation:
  virtual void OnMintTokenSuccess(const std::string& access_token) OVERRIDE;
  virtual void OnMintTokenFailure(const GoogleServiceAuthError& error) OVERRIDE;

  scoped_ptr<OAuth2MintTokenFlow> flow_;

  DECLARE_EXTENSION_FUNCTION_NAME("experimental.identity.getAuthToken");
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_IDENTITY_IDENTITY_API_H_
