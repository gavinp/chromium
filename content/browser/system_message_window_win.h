// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_SYSTEM_MESSAGE_WINDOW_WIN_H_
#define CONTENT_BROWSER_SYSTEM_MESSAGE_WINDOW_WIN_H_
#pragma once

#include <windows.h>

#include "base/basictypes.h"
#include "content/common/content_export.h"

typedef LRESULT (*VolumeNameFunc)(LPCWSTR drive,
                                  LPWSTR volume_name,
                                  unsigned int volume_name_len);

class CONTENT_EXPORT SystemMessageWindowWin {
 public:
  SystemMessageWindowWin();
  // Only for use in unit tests.
  explicit SystemMessageWindowWin::SystemMessageWindowWin(
      VolumeNameFunc volumeNameFunc);

  virtual ~SystemMessageWindowWin();

  virtual LRESULT OnDeviceChange(UINT event_type, DWORD data);

 private:
  void Init();

  LRESULT CALLBACK WndProc(HWND hwnd, UINT message,
                           WPARAM wparam, LPARAM lparam);

  static LRESULT CALLBACK WndProcThunk(HWND hwnd,
                                       UINT message,
                                       WPARAM wparam,
                                       LPARAM lparam) {
    SystemMessageWindowWin* msg_wnd = reinterpret_cast<SystemMessageWindowWin*>(
        GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (msg_wnd)
      return msg_wnd->WndProc(hwnd, message, wparam, lparam);
    return ::DefWindowProc(hwnd, message, wparam, lparam);
  }

  HWND window_;
  VolumeNameFunc volume_name_func_;

  DISALLOW_COPY_AND_ASSIGN(SystemMessageWindowWin);
};

#endif  // CONTENT_BROWSER_SYSTEM_MESSAGE_WINDOW_WIN_H_
