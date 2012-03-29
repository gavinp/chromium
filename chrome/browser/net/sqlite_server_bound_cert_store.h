// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NET_SQLITE_SERVER_BOUND_CERT_STORE_H_
#define CHROME_BROWSER_NET_SQLITE_SERVER_BOUND_CERT_STORE_H_
#pragma once

#include "base/callback_forward.h"
#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "net/base/default_server_bound_cert_store.h"

class FilePath;

// Implements the net::DefaultServerBoundCertStore::PersistentStore interface
// in terms of a SQLite database. For documentation about the actual member
// functions consult the documentation of the parent class
// |net::DefaultServerBoundCertStore::PersistentCertStore|.
class SQLiteServerBoundCertStore
    : public net::DefaultServerBoundCertStore::PersistentStore {
 public:
  explicit SQLiteServerBoundCertStore(const FilePath& path);
  virtual ~SQLiteServerBoundCertStore();

  // net::DefaultServerBoundCertStore::PersistentStore implementation.
  virtual bool Load(
      std::vector<net::DefaultServerBoundCertStore::ServerBoundCert*>* certs)
          OVERRIDE;
  virtual void AddServerBoundCert(
      const net::DefaultServerBoundCertStore::ServerBoundCert& cert) OVERRIDE;
  virtual void DeleteServerBoundCert(
      const net::DefaultServerBoundCertStore::ServerBoundCert& cert) OVERRIDE;
  virtual void SetClearLocalStateOnExit(bool clear_local_state) OVERRIDE;
  virtual void Flush(const base::Closure& completion_task) OVERRIDE;

 private:
  class Backend;

  scoped_refptr<Backend> backend_;

  DISALLOW_COPY_AND_ASSIGN(SQLiteServerBoundCertStore);
};

#endif  // CHROME_BROWSER_NET_SQLITE_SERVER_BOUND_CERT_STORE_H_
