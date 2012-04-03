// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains various hacks needed to inform JSCompiler of various
// WebKit-specific properties and methods. It is used only with JSCompiler
// to verify the type-correctness of our code.

/** @type Array.<HTMLElement> */
Document.prototype.all;

/** @return {void} Nothing. */
Document.prototype.webkitCancelFullScreen = function() {};

/** @type {boolean} */
Document.prototype.webkitIsFullScreen;

/** @return {void} Nothing. */
Element.prototype.webkitRequestFullScreen = function() {};

/** @type {{getRandomValues: function(Uint16Array):void}} */
Window.prototype.crypto;
