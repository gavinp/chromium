// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/user_manager_impl.h"

#include <vector>

#include "ash/shell.h"
#include "ash/desktop_background/desktop_background_controller.h"
#include "ash/desktop_background/desktop_background_resources.h"
#include "base/bind.h"
#include "base/chromeos/chromeos_version.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram.h"
#include "base/path_service.h"
#include "base/rand_util.h"
#include "base/stl_util.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/cros/cert_library.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros_settings.h"
#include "chrome/browser/chromeos/cryptohome/async_method_caller.h"
#include "chrome/browser/chromeos/dbus/cryptohome_client.h"
#include "chrome/browser/chromeos/dbus/dbus_thread_manager.h"
#include "chrome/browser/chromeos/input_method/input_method_manager.h"
#include "chrome/browser/chromeos/login/default_user_images.h"
#include "chrome/browser/chromeos/login/helper.h"
#include "chrome/browser/chromeos/login/login_display.h"
#include "chrome/browser/chromeos/login/ownership_service.h"
#include "chrome/browser/chromeos/login/remove_user_delegate.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/prefs/scoped_user_pref_update.h"
#include "chrome/browser/profiles/profile_downloader.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/ui/webui/web_ui_util.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/net/gaia/google_service_auth_error.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "content/public/common/url_constants.h"
#include "crypto/nss_util.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/png_codec.h"

using content::BrowserThread;

typedef GoogleServiceAuthError AuthError;

namespace chromeos {

namespace {

// Incognito user is represented by an empty string (since some code already
// depends on that and it's hard to figure out what).
const char kGuestUser[] = "";

// Stub user email (for test paths).
const char kStubUser[] = "stub-user@example.com";

// Names of nodes with info about user image.
const char kImagePathNodeName[] = "path";
const char kImageIndexNodeName[] = "index";

// Index of the default image used for the |kStubUser| user.
const int kStubDefaultImageIndex = 0;

// Delay betweeen user login and attempt to update user's profile image.
const long kProfileImageDownloadDelayMs = 10000;

// Enum for reporting histograms about profile picture download.
enum ProfileDownloadResult {
  kDownloadSuccessChanged,
  kDownloadSuccess,
  kDownloadFailure,
  kDownloadDefault,

  // Must be the last, convenient count.
  kDownloadResultsCount
};

// Time histogram prefix for the default profile image download.
const char kProfileDownloadDefaultTime[] =
    "UserImage.ProfileDownloadTime.Default";
// Time histogram prefix for a failed profile image download.
const char kProfileDownloadFailureTime[] =
    "UserImage.ProfileDownloadTime.Failure";
// Time histogram prefix for a successful profile image download.
const char kProfileDownloadSuccessTime[] =
    "UserImage.ProfileDownloadTime.Success";
// Time histogram suffix for a profile image download after login.
const char kProfileDownloadReasonLoggedIn[] = "LoggedIn";

// Add a histogram showing the time it takes to download a profile image.
// Separate histograms are reported for each download |reason| and |result|.
void AddProfileImageTimeHistogram(ProfileDownloadResult result,
                                  const std::string& download_reason,
                                  const base::TimeDelta& time_delta) {
  std::string histogram_name;
  switch (result) {
    case kDownloadFailure:
      histogram_name = kProfileDownloadFailureTime;
      break;
    case kDownloadDefault:
      histogram_name = kProfileDownloadDefaultTime;
      break;
    case kDownloadSuccess:
      histogram_name = kProfileDownloadSuccessTime;
      break;
    default:
      NOTREACHED();
  }
  if (!download_reason.empty()) {
    histogram_name += ".";
    histogram_name += download_reason;
  }

  static const base::TimeDelta min_time = base::TimeDelta::FromMilliseconds(1);
  static const base::TimeDelta max_time = base::TimeDelta::FromSeconds(50);
  const size_t bucket_count(50);

  base::Histogram* counter = base::Histogram::FactoryTimeGet(
      histogram_name, min_time, max_time, bucket_count,
      base::Histogram::kUmaTargetedHistogramFlag);
  counter->AddTime(time_delta);

  DVLOG(1) << "Profile image download time: " << time_delta.InSecondsF();
}

// Callback that is called after user removal is complete.
void OnRemoveUserComplete(const std::string& user_email,
                          bool success,
                          cryptohome::MountError return_code) {
  // Log the error, but there's not much we can do.
  if (!success) {
    LOG(ERROR) << "Removal of cryptohome for " << user_email
               << " failed, return code: " << return_code;
  }
}

// This method is used to implement UserManager::RemoveUser.
void RemoveUserInternal(const std::string& user_email,
                        chromeos::RemoveUserDelegate* delegate) {
  CrosSettings* cros_settings = CrosSettings::Get();

  // Ensure the value of owner email has been fetched.
  if (!cros_settings->PrepareTrustedValues(
          base::Bind(&RemoveUserInternal, user_email, delegate))) {
    // Value of owner email is not fetched yet.  RemoveUserInternal will be
    // called again after fetch completion.
    return;
  }
  std::string owner;
  cros_settings->GetString(kDeviceOwner, &owner);
  if (user_email == owner) {
    // Owner is not allowed to be removed from the device.
    return;
  }

  if (delegate)
    delegate->OnBeforeUserRemoved(user_email);

  chromeos::UserManager::Get()->RemoveUserFromList(user_email);
  cryptohome::AsyncMethodCaller::GetInstance()->AsyncRemove(
      user_email, base::Bind(&OnRemoveUserComplete, user_email));

  if (delegate)
    delegate->OnUserRemoved(user_email);
}

class RealTPMTokenInfoDelegate : public crypto::TPMTokenInfoDelegate {
 public:
  RealTPMTokenInfoDelegate();
  virtual ~RealTPMTokenInfoDelegate();
  // TPMTokenInfoDeleagte overrides:
  virtual bool IsTokenAvailable() const OVERRIDE;
  virtual void RequestIsTokenReady(
      base::Callback<void(bool result)> callback) const OVERRIDE;
  virtual void GetTokenInfo(std::string* token_name,
                            std::string* user_pin) const OVERRIDE;
 private:
  // This method is used to implement RequestIsTokenReady.
  void OnPkcs11IsTpmTokenReady(base::Callback<void(bool result)> callback,
                               CryptohomeClient::CallStatus call_status,
                               bool is_tpm_token_ready) const;

  // This method is used to implement RequestIsTokenReady.
  void OnPkcs11GetTpmTokenInfo(base::Callback<void(bool result)> callback,
                               CryptohomeClient::CallStatus call_status,
                               const std::string& token_name,
                               const std::string& user_pin) const;

  // These are mutable since we need to cache them in IsTokenReady().
  mutable bool token_ready_;
  mutable std::string token_name_;
  mutable std::string user_pin_;
  mutable base::WeakPtrFactory<RealTPMTokenInfoDelegate> weak_ptr_factory_;
};

RealTPMTokenInfoDelegate::RealTPMTokenInfoDelegate() : token_ready_(false),
                                                       weak_ptr_factory_(this) {
}

RealTPMTokenInfoDelegate::~RealTPMTokenInfoDelegate() {}

bool RealTPMTokenInfoDelegate::IsTokenAvailable() const {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  bool result = false;
  DBusThreadManager::Get()->GetCryptohomeClient()->CallTpmIsEnabledAndBlock(
      &result);
  return result;
}

void RealTPMTokenInfoDelegate::RequestIsTokenReady(
    base::Callback<void(bool result)> callback) const {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (token_ready_) {
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                            base::Bind(callback, true));
    return;
  }
  DBusThreadManager::Get()->GetCryptohomeClient()->Pkcs11IsTpmTokenReady(
      base::Bind(&RealTPMTokenInfoDelegate::OnPkcs11IsTpmTokenReady,
                 weak_ptr_factory_.GetWeakPtr(),
                 callback));
}

void RealTPMTokenInfoDelegate::GetTokenInfo(std::string* token_name,
                                            std::string* user_pin) const {
  // May be called from a non UI thread, but must only be called after
  // IsTokenReady() returns true.
  CHECK(token_ready_);
  if (token_name)
    *token_name = token_name_;
  if (user_pin)
    *user_pin = user_pin_;
}

void RealTPMTokenInfoDelegate::OnPkcs11IsTpmTokenReady(
    base::Callback<void(bool result)> callback,
    CryptohomeClient::CallStatus call_status,
    bool is_tpm_token_ready) const {
  if (call_status != CryptohomeClient::SUCCESS || !is_tpm_token_ready) {
    callback.Run(false);
    return;
  }

  // Retrieve token_name_ and user_pin_ here since they will never change
  // and CryptohomeClient calls are not thread safe.
  DBusThreadManager::Get()->GetCryptohomeClient()->Pkcs11GetTpmTokenInfo(
      base::Bind(&RealTPMTokenInfoDelegate::OnPkcs11GetTpmTokenInfo,
                 weak_ptr_factory_.GetWeakPtr(),
                 callback));
}

void RealTPMTokenInfoDelegate::OnPkcs11GetTpmTokenInfo(
    base::Callback<void(bool result)> callback,
    CryptohomeClient::CallStatus call_status,
    const std::string& token_name,
    const std::string& user_pin) const {
  if (call_status == CryptohomeClient::SUCCESS) {
    token_name_ = token_name;
    user_pin_ = user_pin;
    token_ready_ = true;
  }
  callback.Run(token_ready_);
}

}  // namespace

UserManagerImpl::UserManagerImpl()
    : ALLOW_THIS_IN_INITIALIZER_LIST(image_loader_(new UserImageLoader)),
      logged_in_user_(NULL),
      is_current_user_owner_(false),
      is_current_user_new_(false),
      is_current_user_ephemeral_(false),
      key_store_loaded_(false),
      ephemeral_users_enabled_(false),
      observed_sync_service_(NULL),
      last_image_set_async_(false),
      downloaded_profile_image_data_url_(chrome::kAboutBlankURL) {
  // If we're not running on ChromeOS, and are not showing the login manager
  // or attempting a command line login? Then login the stub user.
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (!base::chromeos::IsRunningOnChromeOS() &&
      !command_line->HasSwitch(switches::kLoginManager) &&
      !command_line->HasSwitch(switches::kLoginPassword)) {
    StubUserLoggedIn();
  }

  registrar_.Add(this, chrome::NOTIFICATION_OWNER_KEY_FETCH_ATTEMPT_SUCCEEDED,
      content::NotificationService::AllSources());
  registrar_.Add(this, chrome::NOTIFICATION_PROFILE_ADDED,
      content::NotificationService::AllSources());
  RetrieveTrustedDevicePolicies();
}

UserManagerImpl::~UserManagerImpl() {
  // Can't use STLDeleteElements because of the private destructor of User.
  for (size_t i = 0; i < users_.size();++i)
    delete users_[i];
  users_.clear();
  if (is_current_user_ephemeral_)
    delete logged_in_user_;
}

const UserList& UserManagerImpl::GetUsers() const {
  const_cast<UserManagerImpl*>(this)->EnsureUsersLoaded();
  return users_;
}

void UserManagerImpl::UserLoggedIn(const std::string& email) {
  // Get a random wallpaper each time a user logged in.
  current_user_wallpaper_index_ = ash::GetDefaultWallpaperIndex();

  // Remove the stub user if it is still around.
  if (logged_in_user_) {
    DCHECK(IsLoggedInAsStub());
    delete logged_in_user_;
    logged_in_user_ = NULL;
    is_current_user_ephemeral_ = false;
  }

  if (email == kGuestUser) {
    GuestUserLoggedIn();
    return;
  }

  if (email == kDemoUser) {
    DemoUserLoggedIn();
    return;
  }

  if (IsEphemeralUser(email)) {
    EphemeralUserLoggedIn(email);
    return;
  }

  EnsureUsersLoaded();

  // Clear the prefs view of the users.
  PrefService* prefs = g_browser_process->local_state();
  ListPrefUpdate prefs_users_update(prefs, UserManager::kLoggedInUsers);
  prefs_users_update->Clear();

  // Make sure this user is first.
  prefs_users_update->Append(Value::CreateStringValue(email));
  UserList::iterator logged_in_user = users_.end();
  for (UserList::iterator it = users_.begin(); it != users_.end(); ++it) {
    std::string user_email = (*it)->email();
    // Skip the most recent user.
    if (email != user_email)
      prefs_users_update->Append(Value::CreateStringValue(user_email));
    else
      logged_in_user = it;
  }

  if (logged_in_user == users_.end()) {
    is_current_user_new_ = true;
    logged_in_user_ = CreateUser(email);
  } else {
    logged_in_user_ = *logged_in_user;
    users_.erase(logged_in_user);
  }
  // This user must be in the front of the user list.
  users_.insert(users_.begin(), logged_in_user_);

  NotifyOnLogin();

  if (is_current_user_new_) {
    SetInitialUserImage(email);
  } else {
    // Download profile image if it's user image and see if it has changed.
    int image_index = logged_in_user_->image_index();
    if (image_index == User::kProfileImageIndex) {
      InitDownloadedProfileImage();
      BrowserThread::PostDelayedTask(
          BrowserThread::UI,
          FROM_HERE,
          base::Bind(&UserManagerImpl::DownloadProfileImage,
                     base::Unretained(this),
                     kProfileDownloadReasonLoggedIn),
          kProfileImageDownloadDelayMs);
    }

    int histogram_index = image_index;
    switch (image_index) {
      case User::kExternalImageIndex:
        // TODO(avayvod): Distinguish this from selected from file.
        histogram_index = kHistogramImageFromCamera;
        break;

      case User::kProfileImageIndex:
        histogram_index = kHistogramImageFromProfile;
        break;
    }
    UMA_HISTOGRAM_ENUMERATION("UserImage.LoggedIn",
                              histogram_index,
                              kHistogramImagesCount);
  }
}

void UserManagerImpl::DemoUserLoggedIn() {
  is_current_user_new_ = true;
  is_current_user_ephemeral_ = true;
  logged_in_user_ = new User(kDemoUser, false);
  SetInitialUserImage(kDemoUser);
  NotifyOnLogin();
}

void UserManagerImpl::GuestUserLoggedIn() {
  is_current_user_ephemeral_ = true;
  // Guest user always uses the same wallpaper.
  current_user_wallpaper_index_ = ash::GetGuestWallpaperIndex();
  logged_in_user_ = new User(kGuestUser, true);
  NotifyOnLogin();
}

void UserManagerImpl::EphemeralUserLoggedIn(const std::string& email) {
  is_current_user_new_ = true;
  is_current_user_ephemeral_ = true;
  logged_in_user_ = CreateUser(email);
  SetInitialUserImage(email);
  NotifyOnLogin();
}

void UserManagerImpl::StubUserLoggedIn() {
  is_current_user_ephemeral_ = true;
  current_user_wallpaper_index_ = ash::GetGuestWallpaperIndex();
  logged_in_user_ = new User(kStubUser, false);
  logged_in_user_->SetImage(GetDefaultImage(kStubDefaultImageIndex),
                            kStubDefaultImageIndex);
}

void UserManagerImpl::RemoveUser(const std::string& email,
                                 RemoveUserDelegate* delegate) {
  if (!IsKnownUser(email))
    return;

  // Sanity check: we must not remove single user. This check may seem
  // redundant at a first sight because this single user must be an owner and
  // we perform special check later in order not to remove an owner.  However
  // due to non-instant nature of ownership assignment this later check may
  // sometimes fail. See http://crosbug.com/12723
  if (users_.size() < 2)
    return;

  // Sanity check: do not allow the logged-in user to remove himself.
  if (logged_in_user_ && logged_in_user_->email() == email)
    return;

  RemoveUserInternal(email, delegate);
}

void UserManagerImpl::RemoveUserFromList(const std::string& email) {
  EnsureUsersLoaded();
  RemoveUserFromListInternal(email);
}

bool UserManagerImpl::IsKnownUser(const std::string& email) const {
  return FindUser(email) != NULL;
}

const User* UserManagerImpl::FindUser(const std::string& email) const {
  if (logged_in_user_ && logged_in_user_->email() == email)
    return logged_in_user_;
  return FindUserInList(email);
}

const User& UserManagerImpl::GetLoggedInUser() const {
  return *logged_in_user_;
}

User& UserManagerImpl::GetLoggedInUser() {
  return *logged_in_user_;
}

bool UserManagerImpl::IsDisplayNameUnique(
    const std::string& display_name) const {
  return display_name_count_[display_name] < 2;
}

void UserManagerImpl::SaveUserOAuthStatus(
    const std::string& username,
    User::OAuthTokenStatus oauth_token_status) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  DVLOG(1) << "Saving user OAuth token status in Local State";
  User* user = const_cast<User*>(FindUser(username));
  if (user)
    user->set_oauth_token_status(oauth_token_status);

  // Do not update local store if the user is ephemeral.
  if (IsEphemeralUser(username))
    return;

  PrefService* local_state = g_browser_process->local_state();

  DictionaryPrefUpdate oauth_status_update(local_state,
                                           UserManager::kUserOAuthTokenStatus);
  oauth_status_update->SetWithoutPathExpansion(username,
      new base::FundamentalValue(static_cast<int>(oauth_token_status)));
}

User::OAuthTokenStatus UserManagerImpl::LoadUserOAuthStatus(
    const std::string& username) const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kSkipOAuthLogin)) {
    // Use OAUTH_TOKEN_STATUS_VALID flag if kSkipOAuthLogin is present.
    return User::OAUTH_TOKEN_STATUS_VALID;
  } else {
    PrefService* local_state = g_browser_process->local_state();
    const DictionaryValue* prefs_oauth_status =
        local_state->GetDictionary(UserManager::kUserOAuthTokenStatus);

    int oauth_token_status = User::OAUTH_TOKEN_STATUS_UNKNOWN;
    if (prefs_oauth_status &&
        prefs_oauth_status->GetIntegerWithoutPathExpansion(username,
            &oauth_token_status)) {
      return static_cast<User::OAuthTokenStatus>(oauth_token_status);
    }
  }

  return User::OAUTH_TOKEN_STATUS_UNKNOWN;
}

void UserManagerImpl::SaveUserDisplayEmail(const std::string& username,
                                           const std::string& display_email) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  User* user = const_cast<User*>(FindUser(username));
  if (!user)
    return;  // Ignore if there is no such user.

  user->set_display_email(display_email);

  // Do not update local store if the user is ephemeral.
  if (IsEphemeralUser(username))
    return;

  PrefService* local_state = g_browser_process->local_state();

  DictionaryPrefUpdate display_email_update(local_state,
                                            UserManager::kUserDisplayEmail);
  display_email_update->SetWithoutPathExpansion(
      username,
      base::Value::CreateStringValue(display_email));
}

std::string UserManagerImpl::GetUserDisplayEmail(
    const std::string& username) const {
  const User* user = FindUser(username);
  return user ? user->display_email() : username;
}

void UserManagerImpl::SaveUserDefaultImageIndex(const std::string& username,
                                                int image_index) {
  DCHECK(image_index >= 0 && image_index < kDefaultImagesCount);
  SetUserImage(username, image_index, GetDefaultImage(image_index));
  SaveImageToLocalState(username, "", image_index, false);
}

void UserManagerImpl::SaveUserImage(const std::string& username,
                                    const SkBitmap& image) {
  SaveUserImageInternal(username, User::kExternalImageIndex, image);
}

void UserManagerImpl::SaveUserImageFromFile(const std::string& username,
                                            const FilePath& path) {
  image_loader_->Start(
      path.value(), login::kUserImageSize,
      base::Bind(&UserManagerImpl::SaveUserImage,
                 base::Unretained(this), username));
}

void UserManagerImpl::SaveUserImageFromProfileImage(
    const std::string& username) {
  if (!downloaded_profile_image_.empty()) {
    // Profile image has already been downloaded, so save it to file right now.
    SaveUserImageInternal(username, User::kProfileImageIndex,
                          downloaded_profile_image_);
  } else {
    // No profile image - use the stub image (gray avatar).
    SetUserImage(username, User::kProfileImageIndex, SkBitmap());
    SaveImageToLocalState(username, "", User::kProfileImageIndex, false);
  }
}

void UserManagerImpl::DownloadProfileImage(const std::string& reason) {
  if (profile_image_downloader_.get()) {
    // Another download is already in progress
    return;
  }

  if (IsLoggedInAsGuest()) {
    // This is a guest login so there's no profile image to download.
    return;
  }

  profile_image_download_reason_ = reason;
  profile_image_load_start_time_ = base::Time::Now();
  profile_image_downloader_.reset(new ProfileDownloader(this));
  profile_image_downloader_->Start();
}

void UserManagerImpl::Observe(int type,
                              const content::NotificationSource& source,
                              const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_OWNER_KEY_FETCH_ATTEMPT_SUCCEEDED:
      BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
                              base::Bind(&UserManagerImpl::CheckOwnership,
                                         base::Unretained(this)));
      BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
          base::Bind(&UserManagerImpl::RetrieveTrustedDevicePolicies,
          base::Unretained(this)));
      break;
    case chrome::NOTIFICATION_PROFILE_ADDED:
      if (IsUserLoggedIn() && !IsLoggedInAsGuest() && !IsLoggedInAsStub()) {
        Profile* profile = content::Source<Profile>(source).ptr();
        if (!profile->IsOffTheRecord() &&
            profile == ProfileManager::GetDefaultProfile()) {
          DCHECK(NULL == observed_sync_service_);
          observed_sync_service_ =
              ProfileSyncServiceFactory::GetForProfile(profile);
          if (observed_sync_service_)
            observed_sync_service_->AddObserver(this);
        }
      }
      break;
    default:
      NOTREACHED();
  }
}

void UserManagerImpl::OnStateChanged() {
  DCHECK(IsUserLoggedIn() && !IsLoggedInAsGuest() && !IsLoggedInAsStub());
  if (observed_sync_service_->GetAuthError().state() != AuthError::NONE) {
      // Invalidate OAuth token to force Gaia sign-in flow. This is needed
      // because sign-out/sign-in solution is suggested to the user.
      // TODO(altimofeev): this code isn't needed after crosbug.com/25978 is
      // implemented.
      DVLOG(1) << "Invalidate OAuth token because of a sync error.";
      SaveUserOAuthStatus(GetLoggedInUser().email(),
                          User::OAUTH_TOKEN_STATUS_INVALID);
  }
}

bool UserManagerImpl::IsCurrentUserOwner() const {
  base::AutoLock lk(is_current_user_owner_lock_);
  return is_current_user_owner_;
}

void UserManagerImpl::SetCurrentUserIsOwner(bool is_current_user_owner) {
  base::AutoLock lk(is_current_user_owner_lock_);
  is_current_user_owner_ = is_current_user_owner;
}

bool UserManagerImpl::IsCurrentUserNew() const {
  return is_current_user_new_;
}

bool UserManagerImpl::IsCurrentUserEphemeral() const {
  return is_current_user_ephemeral_;
}

bool UserManagerImpl::IsUserLoggedIn() const {
  return logged_in_user_;
}

bool UserManagerImpl::IsLoggedInAsDemoUser() const {
  return IsUserLoggedIn() && logged_in_user_->email() == kDemoUser;
}

bool UserManagerImpl::IsLoggedInAsGuest() const {
  return IsUserLoggedIn() && logged_in_user_->email() == kGuestUser;
}

bool UserManagerImpl::IsLoggedInAsStub() const {
  return IsUserLoggedIn() && logged_in_user_->email() == kStubUser;
}

void UserManagerImpl::AddObserver(Observer* obs) {
  observer_list_.AddObserver(obs);
}

void UserManagerImpl::RemoveObserver(Observer* obs) {
  observer_list_.RemoveObserver(obs);
}

const SkBitmap& UserManagerImpl::DownloadedProfileImage() const {
  return downloaded_profile_image_;
}

void UserManagerImpl::NotifyLocalStateChanged() {
  FOR_EACH_OBSERVER(
    Observer,
    observer_list_,
    LocalStateChanged(this));
}

FilePath UserManagerImpl::GetImagePathForUser(const std::string& username) {
  std::string filename = username + ".png";
  FilePath user_data_dir;
  PathService::Get(chrome::DIR_USER_DATA, &user_data_dir);
  return user_data_dir.AppendASCII(filename);
}

void UserManagerImpl::EnsureUsersLoaded() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (!users_.empty())
    return;
  if (!g_browser_process)
    return;

  PrefService* local_state = g_browser_process->local_state();
  const ListValue* prefs_users =
      local_state->GetList(UserManager::kLoggedInUsers);
  const DictionaryValue* prefs_images =
      local_state->GetDictionary(UserManager::kUserImages);
  const DictionaryValue* prefs_display_emails =
      local_state->GetDictionary(UserManager::kUserDisplayEmail);

  if (prefs_users) {
    for (ListValue::const_iterator it = prefs_users->begin();
         it != prefs_users->end(); ++it) {
      std::string email;
      if ((*it)->GetAsString(&email)) {
        User* user = CreateUser(email);
        users_.push_back(user);

        if (prefs_images) {
          // Get account image path.
          // TODO(avayvod): Reading image path as a string is here for
          // backward compatibility.
          std::string image_path;
          base::DictionaryValue* image_properties;
          if (prefs_images->GetStringWithoutPathExpansion(email, &image_path)) {
            int image_id = User::kInvalidImageIndex;
            if (IsDefaultImagePath(image_path, &image_id)) {
              user->SetImage(GetDefaultImage(image_id), image_id);
            } else {
              int image_index = User::kExternalImageIndex;
              // Until image has been loaded, use the stub image.
              user->SetStubImage(image_index);
              DCHECK(!image_path.empty());
              // Load user image asynchronously.
              image_loader_->Start(
                  image_path, 0,
                  base::Bind(&UserManagerImpl::SetUserImage,
                             base::Unretained(this), email, image_index));
            }
          } else if (prefs_images->GetDictionaryWithoutPathExpansion(
                         email, &image_properties)) {
            int image_index = User::kInvalidImageIndex;
            image_properties->GetString(kImagePathNodeName, &image_path);
            image_properties->GetInteger(kImageIndexNodeName, &image_index);
            if (image_index >= 0 && image_index < kDefaultImagesCount) {
              user->SetImage(GetDefaultImage(image_index), image_index);
            } else if (image_index == User::kExternalImageIndex ||
                       image_index == User::kProfileImageIndex) {
              // Path may be empty for profile images (meaning that the image
              // hasn't been downloaded for the first time yet, in which case a
              // download will be scheduled for |kProfileImageDownloadDelayMs|
              // after user logs in).
              DCHECK(!image_path.empty() ||
                     image_index == User::kProfileImageIndex);
              // Until image has been loaded, use the stub image (gray avatar).
              user->SetStubImage(image_index);
              if (!image_path.empty()) {
                // Load user image asynchronously.
                image_loader_->Start(
                    image_path, 0,
                    base::Bind(&UserManagerImpl::SetUserImage,
                               base::Unretained(this), email, image_index));
              }
            } else {
              NOTREACHED();
            }
          }
        }

        std::string display_email;
        if (prefs_display_emails &&
            prefs_display_emails->GetStringWithoutPathExpansion(
                email, &display_email)) {
          user->set_display_email(display_email);
        }
      }
    }
  }
}

void UserManagerImpl::RetrieveTrustedDevicePolicies() {
  ephemeral_users_enabled_ = false;
  owner_email_ = "";

  CrosSettings* cros_settings = CrosSettings::Get();
  // Schedule a callback if device policy has not yet been verified.
  if (!cros_settings->PrepareTrustedValues(
      base::Bind(&UserManagerImpl::RetrieveTrustedDevicePolicies,
                 base::Unretained(this)))) {
    return;
  }

  cros_settings->GetBoolean(kAccountsPrefEphemeralUsersEnabled,
                            &ephemeral_users_enabled_);
  cros_settings->GetString(kDeviceOwner, &owner_email_);

  // If ephemeral users are enabled, remove all users except the owner.
  if (ephemeral_users_enabled_) {
    scoped_ptr<base::ListValue> users(
        g_browser_process->local_state()->GetList(kLoggedInUsers)->DeepCopy());

    bool changed = false;
    for (base::ListValue::const_iterator user = users->begin();
        user != users->end(); ++user) {
      std::string user_email;
      (*user)->GetAsString(&user_email);
      if (user_email != owner_email_) {
        RemoveUserFromListInternal(user_email);
        changed = true;
      }
    }

    if (changed) {
      // Trigger a redraw of the login window.
      content::NotificationService::current()->Notify(
          chrome::NOTIFICATION_SYSTEM_SETTING_CHANGED,
          content::Source<UserManagerImpl>(this),
          content::NotificationService::NoDetails());
    }
  }
}

bool UserManagerImpl::AreEphemeralUsersEnabled() const {
  return ephemeral_users_enabled_ &&
      (g_browser_process->browser_policy_connector()->IsEnterpriseManaged() ||
      !owner_email_.empty());
}

bool UserManagerImpl::IsEphemeralUser(const std::string& email) const {
  // The guest user always is ephemeral.
  if (email == kGuestUser)
    return true;

  // The currently logged-in user is ephemeral iff logged in as ephemeral.
  if (logged_in_user_ && (email == logged_in_user_->email()))
    return is_current_user_ephemeral_;

  // Any other user is ephemeral iff ephemeral users are enabled, the user is
  // not the owner and is not in the persistent list.
  return AreEphemeralUsersEnabled() &&
      (email != owner_email_) &&
      !FindUserInList(email);
}

const User* UserManagerImpl::FindUserInList(const std::string& email) const {
  const UserList& users = GetUsers();
  for (UserList::const_iterator it = users.begin(); it != users.end(); ++it) {
    if ((*it)->email() == email)
      return *it;
  }
  return NULL;
}

void UserManagerImpl::NotifyOnLogin() {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_LOGIN_USER_CHANGED,
      content::Source<UserManagerImpl>(this),
      content::Details<const User>(logged_in_user_));

  LoadKeyStore();

  // Schedules current user ownership check on file thread.
  BrowserThread::PostTask(BrowserThread::FILE, FROM_HERE,
                          base::Bind(&UserManagerImpl::CheckOwnership,
                                     base::Unretained(this)));
}

void UserManagerImpl::LoadKeyStore() {
  CHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (key_store_loaded_)
    return;

  // Ensure we've opened the real user's key/certificate database.
  crypto::OpenPersistentNSSDB();

  // Only load the Opencryptoki library into NSS if we have this switch.
  // TODO(gspencer): Remove this switch once cryptohomed work is finished:
  // http://crosbug.com/12295 and http://crosbug.com/12304
  if (CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kLoadOpencryptoki)) {
    crypto::EnableTPMTokenForNSS(new RealTPMTokenInfoDelegate());
    CertLibrary* cert_library;
    cert_library = chromeos::CrosLibrary::Get()->GetCertLibrary();
    // Note: this calls crypto::EnsureTPMTokenReady()
    cert_library->RequestCertificates();
  }
  key_store_loaded_ = true;
}

void UserManagerImpl::SetInitialUserImage(const std::string& username) {
  // Choose a random default image.
  int image_id = base::RandInt(0, kDefaultImagesCount - 1);
  SaveUserDefaultImageIndex(username, image_id);
}

int UserManagerImpl::GetUserWallpaperIndex() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // If at login screen, use the default guest wallpaper.
  if (!IsUserLoggedIn())
    return ash::GetGuestWallpaperIndex();
  // If logged in as other ephemeral users (Demo/Stub/Normal user with
  // ephemeral policy enabled/Guest), use the index in memory.
  if (IsCurrentUserEphemeral())
    return current_user_wallpaper_index_;

  const chromeos::User& user = GetLoggedInUser();
  std::string username = user.email();
  DCHECK(!username.empty());

  PrefService* local_state = g_browser_process->local_state();
  const DictionaryValue* user_wallpapers =
      local_state->GetDictionary(UserManager::kUserWallpapers);
  int index;
  if (!user_wallpapers->GetIntegerWithoutPathExpansion(username, &index))
    index = current_user_wallpaper_index_;

  DCHECK(index >=0 && index < ash::GetWallpaperCount());
  return index;
}

void UserManagerImpl::SaveUserWallpaperIndex(int wallpaper_index) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  current_user_wallpaper_index_ = wallpaper_index;
  // Ephemeral users can not save data to local state. We just cache the index
  // in memory for them.
  if (IsCurrentUserEphemeral() || !IsUserLoggedIn()) {
    return;
  }

  const chromeos::User& user = GetLoggedInUser();
  std::string username = user.email();
  DCHECK(!username.empty());

  PrefService* local_state = g_browser_process->local_state();
  DictionaryPrefUpdate wallpapers_update(local_state,
                                         UserManager::kUserWallpapers);
  wallpapers_update->SetWithoutPathExpansion(username,
      new base::FundamentalValue(wallpaper_index));
}

void UserManagerImpl::SetUserImage(const std::string& username,
                                   int image_index,
                                   const SkBitmap& image) {
  User* user = const_cast<User*>(FindUser(username));
  // User may have been removed by now.
  if (user) {
    // For existing users, a valid image index should have been set upon loading
    // them from Local State.
    DCHECK(user->image_index() != User::kInvalidImageIndex ||
           is_current_user_new_);
    bool image_changed = user->image_index() != User::kInvalidImageIndex;
    if (!image.empty())
      user->SetImage(image, image_index);
    else
      user->SetStubImage(image_index);
    // For the logged-in user with a profile picture, initialize
    // |downloaded_profile_picture_|.
    if (user == logged_in_user_ && image_index == User::kProfileImageIndex)
      InitDownloadedProfileImage();
    if (image_changed) {
      // Unless this is first-time setting with |SetInitialUserImage|,
      // send a notification about image change.
      content::NotificationService::current()->Notify(
          chrome::NOTIFICATION_LOGIN_USER_IMAGE_CHANGED,
          content::Source<UserManagerImpl>(this),
          content::Details<const User>(user));
    }
  }
}

void UserManagerImpl::SaveUserImageInternal(const std::string& username,
                                            int image_index,
                                            const SkBitmap& image) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  SetUserImage(username, image_index, image);

  // Ignore for ephemeral users.
  if (IsEphemeralUser(username))
    return;

  FilePath image_path = GetImagePathForUser(username);
  DVLOG(1) << "Saving user image to " << image_path.value();

  last_image_set_async_ = true;

  BrowserThread::PostTask(
      BrowserThread::FILE,
      FROM_HERE,
      base::Bind(&UserManagerImpl::SaveImageToFile,
                 base::Unretained(this),
                 username, image, image_path, image_index));
}

void UserManagerImpl::SaveImageToFile(const std::string& username,
                                      const SkBitmap& image,
                                      const FilePath& image_path,
                                      int image_index) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));

  std::vector<unsigned char> encoded_image;
  if (!gfx::PNGCodec::EncodeBGRASkBitmap(image, false, &encoded_image)) {
    LOG(ERROR) << "Failed to PNG encode the image.";
    return;
  }

  if (file_util::WriteFile(image_path,
                           reinterpret_cast<char*>(&encoded_image[0]),
                           encoded_image.size()) == -1) {
    LOG(ERROR) << "Failed to save image to file.";
    return;
  }

  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(&UserManagerImpl::SaveImageToLocalState,
                 base::Unretained(this),
                 username, image_path.value(), image_index, true));
}

void UserManagerImpl::SaveImageToLocalState(const std::string& username,
                                            const std::string& image_path,
                                            int image_index,
                                            bool is_async) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Ignore for ephemeral users.
  if (IsEphemeralUser(username))
    return;

  // TODO(ivankr): use unique filenames for user images each time
  // a new image is set so that only the last image update is saved
  // to Local State and notified.
  if (is_async && !last_image_set_async_) {
    DVLOG(1) << "Ignoring saved image because it has changed";
    return;
  } else if (!is_async) {
    // Reset the async image save flag if called directly from the UI thread.
    last_image_set_async_ = false;
  }

  PrefService* local_state = g_browser_process->local_state();
  DictionaryPrefUpdate images_update(local_state, UserManager::kUserImages);
  base::DictionaryValue* image_properties = new base::DictionaryValue();
  image_properties->Set(kImagePathNodeName, new StringValue(image_path));
  image_properties->Set(kImageIndexNodeName,
                        new base::FundamentalValue(image_index));
  images_update->SetWithoutPathExpansion(username, image_properties);
  DVLOG(1) << "Saving path to user image in Local State.";

  NotifyLocalStateChanged();
}

void UserManagerImpl::InitDownloadedProfileImage() {
  DCHECK(logged_in_user_);
  if (downloaded_profile_image_.empty() && !logged_in_user_->image_is_stub()) {
    VLOG(1) << "Profile image initialized";
    downloaded_profile_image_ = logged_in_user_->image();
    downloaded_profile_image_data_url_ =
        web_ui_util::GetImageDataUrl(downloaded_profile_image_);
  }
}

void UserManagerImpl::DeleteUserImage(const FilePath& image_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  if (!file_util::Delete(image_path, false)) {
    LOG(ERROR) << "Failed to remove user image.";
    return;
  }
}

void UserManagerImpl::UpdateOwnership(bool is_owner) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  SetCurrentUserIsOwner(is_owner);
  content::NotificationService::current()->Notify(
      chrome::NOTIFICATION_OWNERSHIP_CHECKED,
      content::NotificationService::AllSources(),
      content::NotificationService::NoDetails());
  if (is_owner) {
    // Also update cached value.
    CrosSettings::Get()->SetString(kDeviceOwner, GetLoggedInUser().email());
  }
}

void UserManagerImpl::CheckOwnership() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  bool is_owner = OwnershipService::GetSharedInstance()->IsCurrentUserOwner();
  VLOG(1) << "Current user " << (is_owner ? "is owner" : "is not owner");

  SetCurrentUserIsOwner(is_owner);

  // UserManagerImpl should be accessed only on UI thread.
  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(&UserManagerImpl::UpdateOwnership,
                 base::Unretained(this),
                 is_owner));
}

int UserManagerImpl::GetDesiredImageSideLength() const {
  return login::kUserImageSize;
}

Profile* UserManagerImpl::GetBrowserProfile() {
  return ProfileManager::GetDefaultProfile();
}

std::string UserManagerImpl::GetCachedPictureURL() const {
  // Currently the profile picture URL is not cached on ChromeOS.
  return std::string();
}

void UserManagerImpl::OnDownloadComplete(ProfileDownloader* downloader,
                                         bool success) {
  // Make sure that |ProfileDownloader| gets deleted after return.
  scoped_ptr<ProfileDownloader> profile_image_downloader(
      profile_image_downloader_.release());
  DCHECK(profile_image_downloader.get() == downloader);

  ProfileDownloadResult result;
  if (!success) {
    result = kDownloadFailure;
  } else if (downloader->GetProfilePicture().isNull()) {
    result = kDownloadDefault;
  } else {
    result = kDownloadSuccess;
  }
  UMA_HISTOGRAM_ENUMERATION("UserImage.ProfileDownloadResult",
      result, kDownloadResultsCount);

  DCHECK(!profile_image_load_start_time_.is_null());
  base::TimeDelta delta = base::Time::Now() - profile_image_load_start_time_;
  AddProfileImageTimeHistogram(result, profile_image_download_reason_, delta);

  if (result == kDownloadSuccess) {
    // Check if this image is not the same as already downloaded.
    std::string new_image_data_url =
        web_ui_util::GetImageDataUrl(downloader->GetProfilePicture());
    if (!downloaded_profile_image_data_url_.empty() &&
        new_image_data_url == downloaded_profile_image_data_url_)
      return;

    downloaded_profile_image_data_url_ = new_image_data_url;
    downloaded_profile_image_ = downloader->GetProfilePicture();

    if (GetLoggedInUser().image_index() == User::kProfileImageIndex) {
      VLOG(1) << "Updating profile image for logged-in user";
      UMA_HISTOGRAM_ENUMERATION("UserImage.ProfileDownloadResult",
                                kDownloadSuccessChanged,
                                kDownloadResultsCount);

      // This will persist |downloaded_profile_image_| to file.
      SaveUserImageFromProfileImage(GetLoggedInUser().email());
    }
  }

  if (result == kDownloadSuccess) {
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_PROFILE_IMAGE_UPDATED,
        content::Source<UserManagerImpl>(this),
        content::Details<const SkBitmap>(&downloaded_profile_image_));
  } else {
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_PROFILE_IMAGE_UPDATE_FAILED,
        content::Source<UserManagerImpl>(this),
        content::NotificationService::NoDetails());
  }
}

User* UserManagerImpl::CreateUser(const std::string& email) const {
  User* user = new User(email, email == kGuestUser);
  user->set_oauth_token_status(LoadUserOAuthStatus(email));
  // Used to determine whether user's display name is unique.
  ++display_name_count_[user->GetDisplayName()];
  return user;
}

void UserManagerImpl::RemoveUserFromListInternal(const std::string& email) {
  // Clear the prefs view of the users.
  PrefService* prefs = g_browser_process->local_state();
  ListPrefUpdate prefs_users_update(prefs, kLoggedInUsers);
  prefs_users_update->Clear();

  UserList::iterator user_to_remove = users_.end();
  for (UserList::iterator it = users_.begin(); it != users_.end(); ++it) {
    std::string user_email = (*it)->email();
    // Skip user that we would like to delete.
    if (email != user_email)
      prefs_users_update->Append(Value::CreateStringValue(user_email));
    else
      user_to_remove = it;
  }

  DictionaryPrefUpdate prefs_wallpapers_update(prefs,
                                               kUserWallpapers);
  prefs_wallpapers_update->RemoveWithoutPathExpansion(email, NULL);

  DictionaryPrefUpdate prefs_images_update(prefs, kUserImages);
  std::string image_path_string;
  prefs_images_update->GetStringWithoutPathExpansion(email, &image_path_string);
  prefs_images_update->RemoveWithoutPathExpansion(email, NULL);

  DictionaryPrefUpdate prefs_oauth_update(prefs, kUserOAuthTokenStatus);
  int oauth_status;
  prefs_oauth_update->GetIntegerWithoutPathExpansion(email, &oauth_status);
  prefs_oauth_update->RemoveWithoutPathExpansion(email, NULL);

  DictionaryPrefUpdate prefs_display_email_update(prefs, kUserDisplayEmail);
  prefs_display_email_update->RemoveWithoutPathExpansion(email, NULL);

  if (user_to_remove != users_.end()) {
    --display_name_count_[(*user_to_remove)->GetDisplayName()];
    delete *user_to_remove;
    users_.erase(user_to_remove);
  }

  int default_image_id = User::kInvalidImageIndex;
  if (!image_path_string.empty() &&
      !IsDefaultImagePath(image_path_string, &default_image_id)) {
    FilePath image_path(image_path_string);
    BrowserThread::PostTask(
        BrowserThread::FILE,
        FROM_HERE,
        base::Bind(&UserManagerImpl::DeleteUserImage,
                   base::Unretained(this),
                   image_path));
  }
}

}  // namespace chromeos
