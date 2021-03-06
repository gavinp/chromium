// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_API_RESOURCE_CONTROLLER_H_
#define CHROME_BROWSER_EXTENSIONS_API_API_RESOURCE_CONTROLLER_H_
#pragma once

#include <string>
#include <map>

#include "base/memory/linked_ptr.h"
#include "chrome/browser/extensions/api/api_resource.h"

namespace extensions {

class SerialConnection;
class Socket;

// kSrcIdKey, or "srcId," binds an APIResource to the onEvent closure that was
// optionally passed to the APIResource's create() method. We generated it in
// schema_generated_bindings.js; the application code is unaware of it.
extern const char kSrcIdKey[];

// APIController keeps track of a collection of APIResources and provides a
// convenient set of methods to work with them.
class APIResourceController {
 public:
  APIResourceController();
  virtual ~APIResourceController();

  // Takes ownership of api_resource.
  int AddAPIResource(APIResource* api_resource);
  bool RemoveAPIResource(int api_resource_id);

  // APIResourceController knows about all classes derived from APIResource.
  // This is intentional to avoid scattering potentially unsafe static_cast<>
  // operations throughout the codebase.
  //
  // Another alternative we considered and rejected was templatizing
  // APIResourceController and scattering specialized versions throughout the
  // codebase.
  Socket* GetSocket(int api_resource_id) const;
  SerialConnection* GetSerialConnection(int api_resource_id) const;

 private:
  int next_api_resource_id_;
  typedef std::map<int, linked_ptr<APIResource> > APIResourceMap;
  APIResourceMap api_resource_map_;

  APIResource* GetAPIResource(APIResource::APIResourceType api_resource_type,
                              int api_resource_id) const;
  APIResource* GetAPIResource(int api_resource_id) const;

  int GenerateAPIResourceId();

  DISALLOW_COPY_AND_ASSIGN(APIResourceController);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_API_RESOURCE_CONTROLLER_H_
