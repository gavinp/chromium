// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/aura/gestures/gesture_point.h"

#include <cmath>

#include "base/basictypes.h"
#include "ui/aura/event.h"
#include "ui/aura/gestures/gesture_configuration.h"
#include "ui/base/events.h"

namespace aura {

GesturePoint::GesturePoint()
    : first_touch_time_(0.0),
      last_touch_time_(0.0),
      last_tap_time_(0.0),
      velocity_calculator_(
          GestureConfiguration::points_buffered_for_velocity()),
      point_id_(-1) {
}

GesturePoint::~GesturePoint() {}

void GesturePoint::Reset() {
  first_touch_time_ = last_touch_time_ = 0.0;
  velocity_calculator_.ClearHistory();
  point_id_ = -1;
}

void GesturePoint::UpdateValues(const TouchEvent& event) {
  const int64 event_timestamp_microseconds =
      event.time_stamp().InMicroseconds();
  if (event.type() == ui::ET_TOUCH_MOVED) {
    velocity_calculator_.PointSeen(event.x(),
                                   event.y(),
                                   event_timestamp_microseconds);
  }

  last_touch_time_ = event.time_stamp().InSecondsF();
  last_touch_position_ = event.location();

  if (event.type() == ui::ET_TOUCH_PRESSED) {
    first_touch_time_ = last_touch_time_;
    first_touch_position_ = event.location();
    velocity_calculator_.ClearHistory();
    velocity_calculator_.PointSeen(event.x(),
                                   event.y(),
                                   event_timestamp_microseconds);
  }
}

void GesturePoint::UpdateForTap() {
  // Update the tap-position and time, and reset every other state.
  last_tap_time_ = last_touch_time_;
  last_tap_position_ = last_touch_position_;
  Reset();
}

void GesturePoint::UpdateForScroll() {
  // Update the first-touch position and time so that the scroll-delta and
  // scroll-velocity can be computed correctly for the next scroll gesture
  // event.
  first_touch_position_ = last_touch_position_;
  first_touch_time_ = last_touch_time_;
}

bool GesturePoint::IsInClickWindow(const TouchEvent& event) const {
  return IsInClickTimeWindow() && IsInsideManhattanSquare(event);
}

bool GesturePoint::IsInDoubleClickWindow(const TouchEvent& event) const {
  return IsInSecondClickTimeWindow() &&
         IsSecondClickInsideManhattanSquare(event);
}

bool GesturePoint::IsInScrollWindow(const TouchEvent& event) const {
  return event.type() == ui::ET_TOUCH_MOVED &&
         !IsInsideManhattanSquare(event);
}

bool GesturePoint::IsInFlickWindow(const TouchEvent& event) {
  return IsOverMinFlickSpeed() && event.type() != ui::ET_TOUCH_CANCELLED;
}

bool GesturePoint::DidScroll(const TouchEvent& event, int dist) const {
  return abs(last_touch_position_.x() - first_touch_position_.x()) > dist ||
         abs(last_touch_position_.y() - first_touch_position_.y()) > dist;
}

float GesturePoint::Distance(const GesturePoint& point) const {
  float x_diff = point.last_touch_position_.x() - last_touch_position_.x();
  float y_diff = point.last_touch_position_.y() - last_touch_position_.y();
  return sqrt(x_diff * x_diff + y_diff * y_diff);
}

bool GesturePoint::HasEnoughDataToEstablishRail() const {
  int dx = x_delta();
  int dy = y_delta();

  int delta_squared = dx * dx + dy * dy;
  return delta_squared > GestureConfiguration::min_scroll_delta_squared();
}

bool GesturePoint::IsInHorizontalRailWindow() const {
  int dx = x_delta();
  int dy = y_delta();
  return abs(dx) > GestureConfiguration::rail_start_proportion() * abs(dy);
}

bool GesturePoint::IsInVerticalRailWindow() const {
  int dx = x_delta();
  int dy = y_delta();
  return abs(dy) > GestureConfiguration::rail_start_proportion() * abs(dx);
}

bool GesturePoint::BreaksHorizontalRail() {
  float vx = XVelocity();
  float vy = YVelocity();
  return fabs(vy) > GestureConfiguration::rail_break_proportion() * fabs(vx) +
      GestureConfiguration::min_rail_break_velocity();
}

bool GesturePoint::BreaksVerticalRail() {
  float vx = XVelocity();
  float vy = YVelocity();
  return fabs(vx) > GestureConfiguration::rail_break_proportion() * fabs(vy) +
      GestureConfiguration::min_rail_break_velocity();
}

bool GesturePoint::IsInClickTimeWindow() const {
  double duration = last_touch_time_ - first_touch_time_;
  return duration >=
      GestureConfiguration::min_touch_down_duration_in_seconds_for_click() &&
      duration <
      GestureConfiguration::max_touch_down_duration_in_seconds_for_click();
}

bool GesturePoint::IsInSecondClickTimeWindow() const {
  double duration =  last_touch_time_ - last_tap_time_;
  return duration < GestureConfiguration::max_seconds_between_double_click();
}

bool GesturePoint::IsInsideManhattanSquare(const TouchEvent& event) const {
  int manhattanDistance = abs(event.x() - first_touch_position_.x()) +
                          abs(event.y() - first_touch_position_.y());
  return manhattanDistance <
      GestureConfiguration::max_touch_move_in_pixels_for_click();
}

bool GesturePoint::IsSecondClickInsideManhattanSquare(
    const TouchEvent& event) const {
  int manhattanDistance = abs(event.x() - last_tap_position_.x()) +
                          abs(event.y() - last_tap_position_.y());
  return manhattanDistance <
      GestureConfiguration::max_touch_move_in_pixels_for_click();
}

bool GesturePoint::IsOverMinFlickSpeed() {
  return velocity_calculator_.VelocitySquared() >
      GestureConfiguration::min_flick_speed_squared();
}

}  // namespace aura
