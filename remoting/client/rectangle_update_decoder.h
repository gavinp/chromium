// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_CLIENT_RECTANGLE_UPDATE_DECODER_H_
#define REMOTING_CLIENT_RECTANGLE_UPDATE_DECODER_H_

#include "base/memory/scoped_ptr.h"
#include "base/task.h"
#include "media/base/video_frame.h"
#include "remoting/base/decoder.h"

class MessageLoop;

namespace remoting {

class FrameConsumer;
class VideoPacket;

namespace protocol {
class SessionConfig;
}  // namespace protocol

// TODO(ajwong): Re-examine this API, especially with regards to how error
// conditions on each step are reported.  Should they be CHECKs? Logs? Other?
// TODO(sergeyu): Rename this class.
class RectangleUpdateDecoder :
    public base::RefCountedThreadSafe<RectangleUpdateDecoder> {
 public:
  RectangleUpdateDecoder(MessageLoop* message_loop,
                         FrameConsumer* consumer);

  // Initializes decoder with the infromation from the protocol config.
  void Initialize(const protocol::SessionConfig& config);

  // Decodes the contents of |packet| calling OnPartialFrameOutput() in the
  // regsitered as data is avaialable. DecodePacket may keep a reference to
  // |packet| so the |packet| must remain alive and valid until |done| is
  // executed.
  void DecodePacket(const VideoPacket* packet, const base::Closure& done);

  // Set the scale ratio for the decoded video frame. Scale ratio greater
  // than 1.0 is not supported.
  void SetScaleRatios(double horizontal_ratio, double vertical_ratio);

  // Set a new clipping rectangle for the decoder. Decoder should respect
  // this clipping rectangle and only decode content in this rectangle and
  // report dirty rectangles accordingly to enhance performance.
  //
  // If scale ratio is not 1.0 then clipping rectangle is ignored.
  void UpdateClipRect(const SkIRect& clip_rect);

  // Force the decoder to output the last decoded video frame without any
  // clipping.
  void RefreshFullFrame();

 private:
  friend class base::RefCountedThreadSafe<RectangleUpdateDecoder>;
  friend class PartialFrameCleanup;

  ~RectangleUpdateDecoder();

  void AllocateFrame(const VideoPacket* packet, const base::Closure& done);
  void ProcessPacketData(const VideoPacket* packet, const base::Closure& done);
  void RefreshRects(const RectVector& rects);

  // Obtain updated rectangles from decoder and submit it to the consumer.
  void SubmitToConsumer();

  // Use |refresh_rects_| to do a refresh to the backing video frame.
  // When done the affected rectangles are submitted to the consumer.
  void DoRefresh();

  // Callback for FrameConsumer::OnPartialFrameOutput()
  void OnFrameConsumed(RectVector* rects);

  // Pointers to infrastructure objects.  Not owned.
  MessageLoop* message_loop_;
  FrameConsumer* consumer_;

  SkISize initial_screen_size_;
  SkIRect clip_rect_;
  RectVector refresh_rects_;

  scoped_ptr<Decoder> decoder_;

  // The video frame that the decoder writes to.
  scoped_refptr<media::VideoFrame> frame_;
  bool frame_is_new_;

  // True if |consumer_| is currently using the frame.
  bool frame_is_consuming_;
};

}  // namespace remoting

#endif  // REMOTING_CLIENT_RECTANGLE_UPDATE_DECODER_H_
