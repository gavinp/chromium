// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_DATE_TRAY_DATE_H_
#define ASH_SYSTEM_DATE_TRAY_DATE_H_
#pragma once

#include "ash/system/date/clock_observer.h"
#include "ash/system/tray/system_tray_item.h"

namespace ash {
namespace internal {

namespace tray {
class DateView;
}

class TrayDate : public SystemTrayItem,
                 public ClockObserver {
 public:
  TrayDate();
  virtual ~TrayDate();

 private:
  // Overridden from SystemTrayItem.
  virtual views::View* CreateTrayView(user::LoginStatus status) OVERRIDE;
  virtual views::View* CreateDefaultView(user::LoginStatus status) OVERRIDE;
  virtual views::View* CreateDetailedView(user::LoginStatus status) OVERRIDE;
  virtual void DestroyTrayView() OVERRIDE;
  virtual void DestroyDefaultView() OVERRIDE;
  virtual void DestroyDetailedView() OVERRIDE;

  // Overridden from ClockObserver.
  virtual void OnDateFormatChanged() OVERRIDE;
  virtual void Refresh() OVERRIDE;

  scoped_ptr<tray::DateView> date_tray_;

  DISALLOW_COPY_AND_ASSIGN(TrayDate);
};

}  // namespace internal
}  // namespace ash

#endif  // ASH_SYSTEM_DATE_TRAY_DATE_H_
