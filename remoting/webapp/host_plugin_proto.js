// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains type definitions for the host plugin. It is used only
// with JSCompiler to verify the type-correctness of our code.

/** @suppress {duplicate} */
var remoting = remoting || {};

/** @constructor
 *  @extends HTMLElement
 */
remoting.HostPlugin = function() {};

/** @param {string} email The email address of the connector.
 *  @param {string} token The access token for the connector.
 *  @return {void} Nothing. */
remoting.HostPlugin.prototype.connect = function(email, token) {};

/** @return {void} Nothing. */
remoting.HostPlugin.prototype.disconnect = function() {};

/** @param {function(string):string} callback Pointer to chrome.i18n.getMessage.
 *  @return {void} Nothing. */
remoting.HostPlugin.prototype.localize = function(callback) {};

/** @param {string} pin The new PIN.
 *  @return {void} Nothing. */
remoting.HostPlugin.prototype.setDaemonPin = function(pin) {};

/** @param {string} callback Callback to be called for the config.
 *  @return {void} Nothing. */
remoting.HostPlugin.prototype.getDaemonConfig = function(callback) {};

/** @param {string} config Host configuration.
 *  @return {void} Nothing. */
remoting.HostPlugin.prototype.startDaemon = function(config) {};

/** @return {void} Nothing. */
remoting.HostPlugin.prototype.stopDaemon = function() {};

/** @param {function(string):void} callback Callback to be called
 *  after new key is generated.
 *  @return {void} Nothing. */
remoting.HostPlugin.prototype.generateKeyPair = function(callback) {};

/** @type {number} */ remoting.HostPlugin.prototype.state;

/** @type {number} */ remoting.HostPlugin.prototype.STARTING;
/** @type {number} */ remoting.HostPlugin.prototype.REQUESTED_ACCESS_CODE;
/** @type {number} */ remoting.HostPlugin.prototype.RECEIVED_ACCESS_CODE;
/** @type {number} */ remoting.HostPlugin.prototype.CONNECTED;
/** @type {number} */ remoting.HostPlugin.prototype.DISCONNECTED;
/** @type {number} */ remoting.HostPlugin.prototype.DISCONNECTING;
/** @type {number} */ remoting.HostPlugin.prototype.ERROR;

/** @type {string} */ remoting.HostPlugin.prototype.accessCode;
/** @type {number} */ remoting.HostPlugin.prototype.accessCodeLifetime;

/** @type {string} */ remoting.HostPlugin.prototype.client;

/** @type {remoting.DaemonPlugin.State} */
remoting.HostPlugin.prototype.daemonState;

/** @type {function(boolean):void} */
remoting.HostPlugin.prototype.onNatTraversalPolicyChanged;
