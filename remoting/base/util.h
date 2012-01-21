// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_BASE_UTIL_H_
#define REMOTING_BASE_UTIL_H_

#include <string>

#include "media/base/video_frame.h"
#include "third_party/skia/include/core/SkRect.h"

namespace remoting {

std::string GetTimestampString();

// TODO(sergeyu): Move these methods to media.
int GetBytesPerPixel(media::VideoFrame::Format format);

// Convert YUV to RGB32 on a specific rectangle.
void ConvertYUVToRGB32WithRect(const uint8* y_plane,
                               const uint8* u_plane,
                               const uint8* v_plane,
                               uint8* rgb_plane,
                               const SkIRect& rect,
                               int y_stride,
                               int uv_stride,
                               int rgb_stride);

void ScaleYUVToRGB32WithRect(const uint8* y_plane,
                             const uint8* u_plane,
                             const uint8* v_plane,
                             uint8* rgb_plane,
                             const SkIRect& source_rect,
                             const SkIRect& dest_rect,
                             int y_stride,
                             int uv_stride,
                             int rgb_stride);

void ConvertRGB32ToYUVWithRect(const uint8* rgb_plane,
                               uint8* y_plane,
                               uint8* u_plane,
                               uint8* v_plane,
                               int x,
                               int y,
                               int width,
                               int height,
                               int rgb_stride,
                               int y_stride,
                               int uv_stride);

int RoundToTwosMultiple(int x);

// Align the sides of the rectangle to multiples of 2 (expanding outwards).
SkIRect AlignRect(const SkIRect& rect);

// Scale a rectangle by horizontal and vertical factors.  If the result has
// non-integer coordinates then the smallest integer-coordinate rectangle that
// wholly encloses it is returned.
SkIRect ScaleRect(const SkIRect& rect,
                  double horizontal_ratio,
                  double vertical_ratio);

// Copy pixels in the rectangle from source to destination.
void CopyRect(const uint8* src_plane,
              int src_plane_stride,
              uint8* dest_plane,
              int dest_plane_stride,
              int bytes_per_pixel,
              const SkIRect& rect);

}  // namespace remoting

#endif  // REMOTING_BASE_UTIL_H_
