// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Custom bindings for the experimental.webRequest API.

var chromeHidden = requireNative('chrome_hidden').GetChromeHidden();

chromeHidden.registerCustomHook('experimental.webRequest', function(api) {
  // Returns the schema definition of type |typeId| defined in |namespace|.
  function getSchema(namespace, typeId) {
    var filterNamespace = function(val) {return val.namespace === namespace;};
    var apiSchema = api.apiDefinitions.filter(filterNamespace)[0];
    var filterTypeId = function (val) {return val.id === typeId;};
    var resultSchema = apiSchema.types.filter(filterTypeId)[0];
    return resultSchema;
  }

  // Helper function for the constructor of concrete datatypes of the
  // declarative webRequest API.
  // Makes sure that |this| contains the union of parameters and
  // {'instanceType': 'experimental.webRequest.' + typeId} and validates the
  // generated union dictionary against the schema for |typeId|.
  function setupInstance(instance, parameters, typeId) {
    for (var key in parameters) {
      if (parameters.hasOwnProperty(key)) {
        instance[key] = parameters[key];
      }
    }
    instance.instanceType = 'experimental.webRequest.' + typeId;
    var schema = getSchema('experimental.webRequest', typeId);
    chromeHidden.validate([instance], [schema]);
  }

  // Setup all data types for the declarative webRequest API.
  chrome.experimental.webRequest.RequestMatcher = function(parameters) {
    setupInstance(this, parameters, 'RequestMatcher');
  };
  chrome.experimental.webRequest.CancelRequest = function(parameters) {
    setupInstance(this, parameters, 'CancelRequest');
  };
  chrome.experimental.webRequest.RedirectRequest = function(parameters) {
    setupInstance(this, parameters, 'RedirectRequest');
  };
});
