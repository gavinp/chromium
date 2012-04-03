// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is here so other GLES2 related files can have a common set of
// includes where appropriate.

#ifndef GPU_COMMAND_BUFFER_COMMON_GLES2_CMD_UTILS_H_
#define GPU_COMMAND_BUFFER_COMMON_GLES2_CMD_UTILS_H_

#include <string>
#include <vector>

#include "../common/types.h"
#include "gpu/command_buffer/common/gles2_utils_export.h"

namespace gpu {
namespace gles2 {

// Does a multiply and checks for overflow.  If the multiply did not overflow
// returns true.
template <typename T>
inline bool SafeMultiply(T a, T b, T* dst) {
  if (b == 0) {
    *dst = 0;
    return true;
  }
  T v = a * b;
  if (v / b != a) {
    *dst = 0;
    return false;
  }
  *dst = v;
  return true;
}

// A wrapper for SafeMultiply to remove the need to cast.
inline bool SafeMultiplyUint32(uint32 a, uint32 b, uint32* dst) {
  return SafeMultiply(a, b, dst);
}

// Does an add checking for overflow.  If there was no overflow returns true.
template <typename T>
inline bool SafeAdd(T a, T b, T* dst) {
  if (a + b < a) {
    *dst = 0;
    return false;
  }
  *dst = a + b;
  return true;
}

// A wrapper for SafeAdd to remove the need to cast.
inline bool SafeAddUint32(uint32 a, uint32 b, uint32* dst) {
  return SafeAdd(a, b, dst);
}

// Utilties for GLES2 support.
class GLES2_UTILS_EXPORT GLES2Util {
 public:
  static const int kNumFaces = 6;

  // Bits returned by GetChannelsForFormat
  enum ChannelBits {
    kRed = 0x1,
    kGreen = 0x2,
    kBlue = 0x4,
    kAlpha = 0x8,
    kDepth = 0x10000,
    kStencil = 0x20000,

    kRGB = kRed | kGreen | kBlue,
    kRGBA = kRGB | kAlpha
  };

  struct EnumToString {
    uint32 value;
    const char* name;
  };

  GLES2Util()
      : num_compressed_texture_formats_(0),
        num_shader_binary_formats_(0) {
  }

  int num_compressed_texture_formats() const {
    return num_compressed_texture_formats_;
  }

  void set_num_compressed_texture_formats(int num_compressed_texture_formats) {
    num_compressed_texture_formats_ = num_compressed_texture_formats;
  }

  int num_shader_binary_formats() const {
    return num_shader_binary_formats_;
  }

  void set_num_shader_binary_formats(int num_shader_binary_formats) {
    num_shader_binary_formats_ = num_shader_binary_formats;
  }

  // Gets the number of values a particular id will return when a glGet
  // function is called. If 0 is returned the id is invalid.
  int GLGetNumValuesReturned(int id) const;

  // Computes the size of image data for TexImage2D and TexSubImage2D.
  static bool ComputeImageDataSize(
    int width, int height, int format, int type, int unpack_alignment,
    uint32* size);

  static size_t RenderbufferBytesPerPixel(int format);

  static uint32 GetGLDataTypeSizeForUniforms(int type);

  static size_t GetGLTypeSizeForTexturesAndBuffers(uint32 type);

  static uint32 GLErrorToErrorBit(uint32 gl_error);

  static uint32 GLErrorBitToGLError(uint32 error_bit);

  static uint32 IndexToGLFaceTarget(int index);

  // Returns a bitmask for the channels the given format supports.
  // See ChannelBits.
  static uint32 GetChannelsForFormat(int format);

  // Returns a bitmask for the channels the given attachment type needs.
  static uint32 GetChannelsNeededForAttachmentType(int type);

  static bool IsNPOT(uint32 value) {
    return value > 0 && (value & (value - 1)) != 0;
  }

  static std::string GetStringEnum(uint32 value);
  static std::string GetStringBool(uint32 value);
  static std::string GetStringError(uint32 value);

  #include "../common/gles2_cmd_utils_autogen.h"

 private:
  static std::string GetQualifiedEnumString(
      const EnumToString* table, size_t count, uint32 value);

  static const EnumToString* enum_to_string_table_;
  static const size_t enum_to_string_table_len_;

  int num_compressed_texture_formats_;
  int num_shader_binary_formats_;
};

class GLES2_UTILS_EXPORT ContextCreationAttribParser {
 public:
  ContextCreationAttribParser();
  bool Parse(const std::vector<int32>& attribs);

  // -1 if invalid or unspecified.
  int32 alpha_size_;
  int32 blue_size_;
  int32 green_size_;
  int32 red_size_;
  int32 depth_size_;
  int32 stencil_size_;
  int32 samples_;
  int32 sample_buffers_;
  bool buffer_preserved_;
  bool share_resources_;
  bool bind_generates_resource_;
};

}  // namespace gles2
}  // namespace gpu

#endif  // GPU_COMMAND_BUFFER_COMMON_GLES2_CMD_UTILS_H_

