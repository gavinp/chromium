// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/fileapi/isolated_mount_point_provider.h"

#include "base/bind.h"
#include "base/file_path.h"
#include "base/logging.h"
#include "base/message_loop_proxy.h"
#include "googleurl/src/gurl.h"
#include "webkit/fileapi/file_system_callback_dispatcher.h"
#include "webkit/fileapi/file_system_operation.h"
#include "webkit/fileapi/file_system_types.h"
#include "webkit/fileapi/isolated_context.h"
#include "webkit/fileapi/isolated_file_util.h"
#include "webkit/fileapi/native_file_util.h"

namespace fileapi {

IsolatedMountPointProvider::IsolatedMountPointProvider()
  : isolated_file_util_(new IsolatedFileUtil(new NativeFileUtil())) {
}

IsolatedMountPointProvider::~IsolatedMountPointProvider() {
}

void IsolatedMountPointProvider::ValidateFileSystemRoot(
    const GURL& origin_url,
    FileSystemType type,
    bool create,
    const ValidateFileSystemCallback& callback) {
  // We never allow opening a new isolated FileSystem via usual OpenFileSystem.
  base::MessageLoopProxy::current()->PostTask(
      FROM_HERE,
      base::Bind(callback, base::PLATFORM_FILE_ERROR_SECURITY));
}

FilePath IsolatedMountPointProvider::GetFileSystemRootPathOnFileThread(
    const GURL& origin_url,
    FileSystemType type,
    const FilePath& virtual_path,
    bool create) {
  if (create || type != kFileSystemTypeIsolated)
    return FilePath();
  std::string fsid;
  FilePath root, path;
  if (!isolated_context()->CrackIsolatedPath(virtual_path, &fsid, &root, &path))
    return FilePath();
  return root;
}

bool IsolatedMountPointProvider::IsAccessAllowed(
    const GURL& origin_url, FileSystemType type, const FilePath& virtual_path) {
  if (type != fileapi::kFileSystemTypeIsolated)
    return false;

  std::string filesystem_id;
  FilePath root, path;
  return isolated_context()->CrackIsolatedPath(
      virtual_path, &filesystem_id, &root, &path);
}

bool IsolatedMountPointProvider::IsRestrictedFileName(
    const FilePath& filename) const {
  return false;
}

std::vector<FilePath> IsolatedMountPointProvider::GetRootDirectories() const {
  // We have no pre-defined root directories that need to be given
  // access permission.
  return  std::vector<FilePath>();
}

FileSystemFileUtil* IsolatedMountPointProvider::GetFileUtil() {
  return isolated_file_util_.get();
}

FilePath IsolatedMountPointProvider::GetPathForPermissionsCheck(
    const FilePath& virtual_path) const {
  std::string fsid;
  FilePath root, path;
  if (!isolated_context()->CrackIsolatedPath(virtual_path, &fsid, &root, &path))
    return FilePath();
  return path;
}

FileSystemOperationInterface*
IsolatedMountPointProvider::CreateFileSystemOperation(
    const GURL& origin_url,
    FileSystemType file_system_type,
    const FilePath& virtual_path,
    base::MessageLoopProxy* file_proxy,
    FileSystemContext* context) const {
  return new FileSystemOperation(file_proxy, context);
}

IsolatedContext* IsolatedMountPointProvider::isolated_context() const {
  return IsolatedContext::GetInstance();
}

}  // namespace fileapi
