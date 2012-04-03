// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_AURA_GESTURES_GESTURE_CONFIGURATION_H_
#define UI_AURA_GESTURES_GESTURE_CONFIGURATION_H_
#pragma once

#include "base/basictypes.h"
#include "ui/aura/aura_export.h"

namespace aura {

// TODO: Expand this design to support multiple OS configuration
// approaches (windows, chrome, others).  This would turn into an
// abstract base class.

class AURA_EXPORT GestureConfiguration {
 public:
  // Ordered alphabetically ignoring underscores, to align with the
  // associated list of prefs in gesture_prefs_aura.cc.
  static double long_press_time_in_seconds() {
    return long_press_time_in_seconds_;
  }
  static void set_long_press_time_in_seconds(double val) {
    long_press_time_in_seconds_ = val;
  }
  static double max_seconds_between_double_click() {
    return max_seconds_between_double_click_;
  }
  static void set_max_seconds_between_double_click(double val) {
    max_seconds_between_double_click_ = val;
  }
  static int max_separation_for_gesture_touches_in_pixels() {
    return max_separation_for_gesture_touches_in_pixels_;
  }
  static void set_max_separation_for_gesture_touches_in_pixels(int val) {
    max_separation_for_gesture_touches_in_pixels_ = val;
  }
  static double max_touch_down_duration_in_seconds_for_click() {
    return max_touch_down_duration_in_seconds_for_click_;
  }
  static void set_max_touch_down_duration_in_seconds_for_click(double val) {
    max_touch_down_duration_in_seconds_for_click_ = val;
  }
  static double max_touch_move_in_pixels_for_click() {
    return max_touch_move_in_pixels_for_click_;
  }
  static void set_max_touch_move_in_pixels_for_click(double val) {
    max_touch_move_in_pixels_for_click_ = val;
  }
  static double min_distance_for_pinch_scroll_in_pixels() {
    return min_distance_for_pinch_scroll_in_pixels_;
  }
  static void set_min_distance_for_pinch_scroll_in_pixels(double val) {
    min_distance_for_pinch_scroll_in_pixels_ = val;
  }
  static double min_flick_speed_squared() {
    return min_flick_speed_squared_;
  }
  static void set_min_flick_speed_squared(double val) {
    min_flick_speed_squared_ = val;
  }
  static double min_pinch_update_distance_in_pixels() {
    return min_pinch_update_distance_in_pixels_;
  }
  static void set_min_pinch_update_distance_in_pixels(double val) {
    min_pinch_update_distance_in_pixels_ = val;
  }
  static double min_rail_break_velocity() {
    return min_rail_break_velocity_;
  }
  static void set_min_rail_break_velocity(double val) {
    min_rail_break_velocity_ = val;
  }
  static double min_scroll_delta_squared() {
    return min_scroll_delta_squared_;
  }
  static void set_min_scroll_delta_squared(double val) {
    min_scroll_delta_squared_ = val;
  }
  static double min_touch_down_duration_in_seconds_for_click() {
    return min_touch_down_duration_in_seconds_for_click_;
  }
  static void set_min_touch_down_duration_in_seconds_for_click(double val) {
    min_touch_down_duration_in_seconds_for_click_ = val;
  }
  static int points_buffered_for_velocity() {
    return points_buffered_for_velocity_;
  }
  static void set_points_buffered_for_velocity(int val) {
    points_buffered_for_velocity_ = val;
  }
  static double rail_break_proportion() {
    return rail_break_proportion_;
  }
  static void set_rail_break_proportion(double val) {
    rail_break_proportion_ = val;
  }
  static double rail_start_proportion() {
    return rail_start_proportion_;
  }
  static void set_rail_start_proportion(double val) {
    rail_start_proportion_ = val;
  }

 private:
  // These are listed in alphabetical order ignoring underscores, to
  // align with the associated list of preferences in
  // gesture_prefs_aura.cc. These two lists should be kept in sync.
  static double long_press_time_in_seconds_;
  static double max_seconds_between_double_click_;
  static double max_separation_for_gesture_touches_in_pixels_;
  static double max_touch_down_duration_in_seconds_for_click_;
  static double max_touch_move_in_pixels_for_click_;
  static double min_distance_for_pinch_scroll_in_pixels_;
  static double min_flick_speed_squared_;
  static double min_pinch_update_distance_in_pixels_;
  static double min_rail_break_velocity_;
  static double min_scroll_delta_squared_;
  static double min_touch_down_duration_in_seconds_for_click_;
  static int points_buffered_for_velocity_;
  static double rail_break_proportion_;
  static double rail_start_proportion_;

  DISALLOW_COPY_AND_ASSIGN(GestureConfiguration);
};

}  // namespace aura

#endif  // UI_AURA_GESTURES_GESTURE_CONFIGURATION_H_
