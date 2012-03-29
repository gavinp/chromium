// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/background/desktop_background_observer.h"

#include "ash/shell.h"
#include "ash/desktop_background/desktop_background_controller.h"
#include "ash/desktop_background/desktop_background_resources.h"
#include "base/logging.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/common/chrome_notification_types.h"
#include "content/public/browser/notification_service.h"

namespace chromeos {

namespace {

class UserWallpaperDelegate: public ash::UserWallpaperDelegate {
 public:
  UserWallpaperDelegate() {
  }

  virtual ~UserWallpaperDelegate() {
  }

  virtual const int GetUserWallpaperIndex() OVERRIDE {
    return chromeos::UserManager::Get()->GetUserWallpaperIndex();
  }

  virtual void OpenSetWallpaperPage() OVERRIDE {
    Browser* browser = Browser::GetOrCreateTabbedBrowser(
        ProfileManager::GetDefaultProfileOrOffTheRecord());
    browser->ShowOptionsTab("setWallpaper");
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(UserWallpaperDelegate);
};

}  // namespace

ash::UserWallpaperDelegate* CreateUserWallpaperDelegate() {
  return new chromeos::UserWallpaperDelegate();
}

DesktopBackgroundObserver::DesktopBackgroundObserver() {
  registrar_.Add(
      this,
      chrome::NOTIFICATION_LOGIN_USER_CHANGED,
      content::NotificationService::AllSources());
}

DesktopBackgroundObserver::~DesktopBackgroundObserver() {
}

void DesktopBackgroundObserver::Observe(int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_LOGIN_USER_CHANGED: {
      ash::Shell::GetInstance()->desktop_background_controller()->
          OnDesktopBackgroundChanged();
      break;
    }
    default:
      NOTREACHED();
  }
}

}  // namespace chromeos
