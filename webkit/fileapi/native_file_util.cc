// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/fileapi/native_file_util.h"

#include <vector>

#include "base/file_util.h"
#include "base/memory/scoped_ptr.h"
#include "webkit/fileapi/file_system_operation_context.h"

namespace fileapi {

class NativeFileEnumerator : public FileSystemFileUtil::AbstractFileEnumerator {
 public:
  NativeFileEnumerator(const FilePath& root_path,
                       bool recursive,
                       file_util::FileEnumerator::FileType file_type)
    : file_enum_(root_path, recursive, file_type) {
#if defined(OS_WIN)
    memset(&file_util_info_, 0, sizeof(file_util_info_));
#endif  // defined(OS_WIN)
  }

  ~NativeFileEnumerator() {}

  virtual FilePath Next() OVERRIDE;
  virtual int64 Size() OVERRIDE;
  virtual base::Time LastModifiedTime() OVERRIDE;
  virtual bool IsDirectory() OVERRIDE;
  virtual bool IsLink() OVERRIDE;

 private:
  file_util::FileEnumerator file_enum_;
  file_util::FileEnumerator::FindInfo file_util_info_;
};

FilePath NativeFileEnumerator::Next() {
  FilePath rv = file_enum_.Next();
  if (!rv.empty())
    file_enum_.GetFindInfo(&file_util_info_);
  return rv;
}

int64 NativeFileEnumerator::Size() {
  return file_util::FileEnumerator::GetFilesize(file_util_info_);
}

base::Time NativeFileEnumerator::LastModifiedTime() {
  return file_util::FileEnumerator::GetLastModifiedTime(file_util_info_);
}

bool NativeFileEnumerator::IsDirectory() {
  return file_util::FileEnumerator::IsDirectory(file_util_info_);
}

bool NativeFileEnumerator::IsLink() {
  return file_util::FileEnumerator::IsLink(file_util_info_);
}

PlatformFileError NativeFileUtil::CreateOrOpen(
    FileSystemOperationContext* unused,
    const FileSystemPath& path, int file_flags,
    PlatformFile* file_handle, bool* created) {
  if (!file_util::DirectoryExists(path.internal_path().DirName())) {
    // If its parent does not exist, should return NOT_FOUND error.
    return base::PLATFORM_FILE_ERROR_NOT_FOUND;
  }
  PlatformFileError error_code = base::PLATFORM_FILE_OK;
  *file_handle = base::CreatePlatformFile(path.internal_path(), file_flags,
                                          created, &error_code);
  return error_code;
}

PlatformFileError NativeFileUtil::Close(
    FileSystemOperationContext* unused,
    PlatformFile file_handle) {
  if (!base::ClosePlatformFile(file_handle))
    return base::PLATFORM_FILE_ERROR_FAILED;
  return base::PLATFORM_FILE_OK;
}

PlatformFileError NativeFileUtil::EnsureFileExists(
    FileSystemOperationContext* unused,
    const FileSystemPath& path,
    bool* created) {
  if (!file_util::DirectoryExists(path.internal_path().DirName()))
    // If its parent does not exist, should return NOT_FOUND error.
    return base::PLATFORM_FILE_ERROR_NOT_FOUND;
  PlatformFileError error_code = base::PLATFORM_FILE_OK;
  // Tries to create the |path| exclusively.  This should fail
  // with base::PLATFORM_FILE_ERROR_EXISTS if the path already exists.
  PlatformFile handle = base::CreatePlatformFile(
      path.internal_path(),
      base::PLATFORM_FILE_CREATE | base::PLATFORM_FILE_READ,
      created, &error_code);
  if (error_code == base::PLATFORM_FILE_ERROR_EXISTS) {
    // Make sure created_ is false.
    if (created)
      *created = false;
    error_code = base::PLATFORM_FILE_OK;
  }
  if (handle != base::kInvalidPlatformFileValue)
    base::ClosePlatformFile(handle);
  return error_code;
}

PlatformFileError NativeFileUtil::CreateDirectory(
    FileSystemOperationContext* context,
    const FileSystemPath& path,
    bool exclusive,
    bool recursive) {
  // If parent dir of file doesn't exist.
  if (!recursive && !file_util::PathExists(path.internal_path().DirName()))
    return base::PLATFORM_FILE_ERROR_NOT_FOUND;

  bool path_exists = file_util::PathExists(path.internal_path());
  if (exclusive && path_exists)
    return base::PLATFORM_FILE_ERROR_EXISTS;

  // If file exists at the path.
  if (path_exists && !file_util::DirectoryExists(path.internal_path()))
    return base::PLATFORM_FILE_ERROR_EXISTS;

  if (!file_util::CreateDirectory(path.internal_path()))
    return base::PLATFORM_FILE_ERROR_FAILED;
  return base::PLATFORM_FILE_OK;
}

PlatformFileError NativeFileUtil::GetFileInfo(
    FileSystemOperationContext* unused,
    const FileSystemPath& path,
    base::PlatformFileInfo* file_info,
    FilePath* platform_file_path) {
  if (!file_util::PathExists(path.internal_path()))
    return base::PLATFORM_FILE_ERROR_NOT_FOUND;
  // TODO(rkc): Fix this hack once we have refactored file_util to handle
  // symlinks correctly.
  // http://code.google.com/p/chromium-os/issues/detail?id=15948
  if (file_util::IsLink(path.internal_path()))
    return base::PLATFORM_FILE_ERROR_NOT_FOUND;
  if (!file_util::GetFileInfo(path.internal_path(), file_info))
    return base::PLATFORM_FILE_ERROR_FAILED;
  *platform_file_path = path.internal_path();
  return base::PLATFORM_FILE_OK;
}

FileSystemFileUtil::AbstractFileEnumerator*
NativeFileUtil::CreateFileEnumerator(
    FileSystemOperationContext* unused,
    const FileSystemPath& root_path,
    bool recursive) {
  return new NativeFileEnumerator(
      root_path.internal_path(), recursive,
      static_cast<file_util::FileEnumerator::FileType>(
          file_util::FileEnumerator::FILES |
          file_util::FileEnumerator::DIRECTORIES));
}

PlatformFileError NativeFileUtil::GetLocalFilePath(
    FileSystemOperationContext* unused,
    const FileSystemPath& file_system_path,
    FilePath* local_file_path) {
  *local_file_path = file_system_path.internal_path();
  return base::PLATFORM_FILE_OK;
}

PlatformFileError NativeFileUtil::Touch(
    FileSystemOperationContext* unused,
    const FileSystemPath& path,
    const base::Time& last_access_time,
    const base::Time& last_modified_time) {
  if (!file_util::TouchFile(
          path.internal_path(), last_access_time, last_modified_time))
    return base::PLATFORM_FILE_ERROR_FAILED;
  return base::PLATFORM_FILE_OK;
}

PlatformFileError NativeFileUtil::Truncate(
    FileSystemOperationContext* unused,
    const FileSystemPath& path,
    int64 length) {
  PlatformFileError error_code(base::PLATFORM_FILE_ERROR_FAILED);
  PlatformFile file =
      base::CreatePlatformFile(
          path.internal_path(),
          base::PLATFORM_FILE_OPEN | base::PLATFORM_FILE_WRITE,
          NULL,
          &error_code);
  if (error_code != base::PLATFORM_FILE_OK) {
    return error_code;
  }
  DCHECK_NE(base::kInvalidPlatformFileValue, file);
  if (!base::TruncatePlatformFile(file, length))
    error_code = base::PLATFORM_FILE_ERROR_FAILED;
  base::ClosePlatformFile(file);
  return error_code;
}

bool NativeFileUtil::PathExists(
    FileSystemOperationContext* unused,
    const FileSystemPath& path) {
  return file_util::PathExists(path.internal_path());
}

bool NativeFileUtil::DirectoryExists(
    FileSystemOperationContext* unused,
    const FileSystemPath& path) {
  return file_util::DirectoryExists(path.internal_path());
}

bool NativeFileUtil::IsDirectoryEmpty(
    FileSystemOperationContext* unused,
    const FileSystemPath& path) {
  return file_util::IsDirectoryEmpty(path.internal_path());
}

PlatformFileError NativeFileUtil::CopyOrMoveFile(
      FileSystemOperationContext* unused,
      const FileSystemPath& src_path,
      const FileSystemPath& dest_path,
      bool copy) {
  if (copy) {
    if (file_util::CopyFile(src_path.internal_path(),
                            dest_path.internal_path()))
      return base::PLATFORM_FILE_OK;
  } else {
    DCHECK(!file_util::DirectoryExists(src_path.internal_path()));
    if (file_util::Move(src_path.internal_path(), dest_path.internal_path()))
      return base::PLATFORM_FILE_OK;
  }
  return base::PLATFORM_FILE_ERROR_FAILED;
}

PlatformFileError NativeFileUtil::CopyInForeignFile(
      FileSystemOperationContext* context,
      const FileSystemPath& underlying_src_path,
      const FileSystemPath& dest_path) {
  return CopyOrMoveFile(context,
                        underlying_src_path,
                        dest_path,
                        true);
}

PlatformFileError NativeFileUtil::DeleteFile(
    FileSystemOperationContext* unused,
    const FileSystemPath& path) {
  if (!file_util::PathExists(path.internal_path()))
    return base::PLATFORM_FILE_ERROR_NOT_FOUND;
  if (file_util::DirectoryExists(path.internal_path()))
    return base::PLATFORM_FILE_ERROR_NOT_A_FILE;
  if (!file_util::Delete(path.internal_path(), false))
    return base::PLATFORM_FILE_ERROR_FAILED;
  return base::PLATFORM_FILE_OK;
}

PlatformFileError NativeFileUtil::DeleteSingleDirectory(
    FileSystemOperationContext* unused,
    const FileSystemPath& path) {
  if (!file_util::PathExists(path.internal_path()))
    return base::PLATFORM_FILE_ERROR_NOT_FOUND;
  if (!file_util::DirectoryExists(path.internal_path())) {
    // TODO(dmikurube): Check if this error code is appropriate.
    return base::PLATFORM_FILE_ERROR_NOT_A_DIRECTORY;
  }
  if (!file_util::IsDirectoryEmpty(path.internal_path())) {
    // TODO(dmikurube): Check if this error code is appropriate.
    return base::PLATFORM_FILE_ERROR_NOT_EMPTY;
  }
  if (!file_util::Delete(path.internal_path(), false))
    return base::PLATFORM_FILE_ERROR_FAILED;
  return base::PLATFORM_FILE_OK;
}

}  // namespace fileapi
