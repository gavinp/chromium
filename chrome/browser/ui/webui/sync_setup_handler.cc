// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/sync_setup_handler.h"

#include "base/bind.h"
#include "base/basictypes.h"
#include "base/bind_helpers.h"
#include "base/compiler_specific.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_info_cache.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_metrics.h"
#include "chrome/browser/signin/signin_manager.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/webui/signin/login_ui_service.h"
#include "chrome/browser/ui/webui/signin/login_ui_service_factory.h"
#include "chrome/browser/ui/webui/sync_promo/sync_promo_ui.h"
#include "chrome/common/net/gaia/gaia_constants.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_view_host_delegate.h"
#include "content/public/browser/web_contents.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "sync/protocol/service_constants.h"
#include "ui/base/l10n/l10n_util.h"

using l10n_util::GetStringFUTF16;
using l10n_util::GetStringUTF16;

namespace {

// A structure which contains all the configuration information for sync.
struct SyncConfigInfo {
  SyncConfigInfo();
  ~SyncConfigInfo();

  bool encrypt_all;
  bool sync_everything;
  syncable::ModelTypeSet data_types;
  std::string passphrase;
  bool passphrase_is_gaia;
};

SyncConfigInfo::SyncConfigInfo()
    : encrypt_all(false),
      sync_everything(false),
      passphrase_is_gaia(false) {
}

SyncConfigInfo::~SyncConfigInfo() {}

const char* kDataTypeNames[] = {
  "apps",
  "autofill",
  "bookmarks",
  "extensions",
  "passwords",
  "preferences",
  "sessions",
  "themes",
  "typed_urls"
};

const syncable::ModelType kDataTypes[] = {
  syncable::APPS,
  syncable::AUTOFILL,
  syncable::BOOKMARKS,
  syncable::EXTENSIONS,
  syncable::PASSWORDS,
  syncable::PREFERENCES,
  syncable::SESSIONS,
  syncable::THEMES,
  syncable::TYPED_URLS
};

static const size_t kNumDataTypes = arraysize(kDataTypes);
COMPILE_ASSERT(arraysize(kDataTypeNames) == arraysize(kDataTypes),
               kDataTypes_does_not_match_kDataTypeNames);

bool GetAuthData(const std::string& json,
                 std::string* username,
                 std::string* password,
                 std::string* captcha,
                 std::string* access_code) {
  scoped_ptr<Value> parsed_value(base::JSONReader::Read(json, false));
  if (!parsed_value.get() || !parsed_value->IsType(Value::TYPE_DICTIONARY))
    return false;

  DictionaryValue* result = static_cast<DictionaryValue*>(parsed_value.get());
  if (!result->GetString("user", username) ||
      !result->GetString("pass", password) ||
      !result->GetString("captcha", captcha) ||
      !result->GetString("access_code", access_code)) {
      return false;
  }
  return true;
}

bool GetConfiguration(const std::string& json, SyncConfigInfo* config) {
  scoped_ptr<Value> parsed_value(base::JSONReader::Read(json, false));
  DictionaryValue* result;
  if (!parsed_value.get() || !parsed_value->GetAsDictionary(&result)) {
    DLOG(ERROR) << "GetConfiguration() not passed a Dictionary";
    return false;
  }

  if (!result->GetBoolean("syncAllDataTypes", &config->sync_everything)) {
    DLOG(ERROR) << "GetConfiguration() not passed a syncAllDataTypes value";
    return false;
  }

  for (size_t i = 0; i < arraysize(kDataTypeNames); ++i) {
    std::string key_name = std::string("sync_") + kDataTypeNames[i];
    bool sync_value;
    if (!result->GetBoolean(key_name, &sync_value)) {
      DLOG(ERROR) << "GetConfiguration() not passed a value for " << key_name;
      return false;
    }
    if (sync_value)
      config->data_types.Put(kDataTypes[i]);
  }

  // Encryption settings.
  if (!result->GetBoolean("encryptAllData", &config->encrypt_all)) {
    DLOG(ERROR) << "GetConfiguration() not passed a value for encryptAllData";
    return false;
  }

  // Passphrase settings.
  bool have_passphrase;
  if (!result->GetBoolean("usePassphrase", &have_passphrase)) {
    DLOG(ERROR) << "GetConfiguration() not passed a usePassphrase value";
    return false;
  }

  if (have_passphrase) {
    if (!result->GetBoolean("isGooglePassphrase",
                            &config->passphrase_is_gaia)) {
      DLOG(ERROR) << "GetConfiguration() not passed isGooglePassphrase value";
      return false;
    }
    if (!result->GetString("passphrase", &config->passphrase)) {
      DLOG(ERROR) << "GetConfiguration() not passed a passphrase value";
      return false;
    }
  }
  return true;
}

bool GetPassphrase(const std::string& json, std::string* passphrase) {
  scoped_ptr<Value> parsed_value(base::JSONReader::Read(json, false));
  if (!parsed_value.get() || !parsed_value->IsType(Value::TYPE_DICTIONARY))
    return false;

  DictionaryValue* result = static_cast<DictionaryValue*>(parsed_value.get());
  return result->GetString("passphrase", passphrase);
}

string16 NormalizeUserName(const string16& user) {
  if (user.find_first_of(ASCIIToUTF16("@")) != string16::npos)
    return user;
  return user + ASCIIToUTF16("@") + ASCIIToUTF16(DEFAULT_SIGNIN_DOMAIN);
}

bool AreUserNamesEqual(const string16& user1, const string16& user2) {
  return NormalizeUserName(user1) == NormalizeUserName(user2);
}

}  // namespace

SyncSetupHandler::SyncSetupHandler(ProfileManager* profile_manager)
    : configuring_sync_(false),
      profile_manager_(profile_manager),
      last_signin_error_(GoogleServiceAuthError::NONE) {
}

SyncSetupHandler::~SyncSetupHandler() {
  // Just exit if running unit tests (no actual WebUI is attached).
  if (!web_ui())
    return;

  // This case is hit when the user performs a back navigation.
  CloseSyncSetup();
}

void SyncSetupHandler::GetLocalizedValues(DictionaryValue* localized_strings) {
  GetStaticLocalizedValues(localized_strings, web_ui());
}

void SyncSetupHandler::GetStaticLocalizedValues(
    DictionaryValue* localized_strings,
    content::WebUI* web_ui) {
  DCHECK(localized_strings);

  localized_strings->SetString(
      "invalidPasswordHelpURL", chrome::kInvalidPasswordHelpURL);
  localized_strings->SetString(
      "cannotAccessAccountURL", chrome::kCanNotAccessAccountURL);
  string16 product_name(GetStringUTF16(IDS_PRODUCT_NAME));
  localized_strings->SetString(
      "introduction",
      GetStringFUTF16(IDS_SYNC_LOGIN_INTRODUCTION, product_name));
  localized_strings->SetString(
      "chooseDataTypesInstructions",
      GetStringFUTF16(IDS_SYNC_CHOOSE_DATATYPES_INSTRUCTIONS, product_name));
  localized_strings->SetString(
      "encryptionInstructions",
      GetStringFUTF16(IDS_SYNC_ENCRYPTION_INSTRUCTIONS, product_name));
  localized_strings->SetString(
      "encryptionHelpURL", chrome::kSyncEncryptionHelpURL);
  localized_strings->SetString(
      "passphraseEncryptionMessage",
      GetStringFUTF16(IDS_SYNC_PASSPHRASE_ENCRYPTION_MESSAGE, product_name));
  localized_strings->SetString(
      "passphraseRecover",
      GetStringFUTF16(IDS_SYNC_PASSPHRASE_RECOVER,
                      ASCIIToUTF16(google_util::StringAppendGoogleLocaleParam(
                          chrome::kSyncGoogleDashboardURL))));
  bool is_start_page = false;
  if (web_ui) {
    SyncPromoUI::Source source = SyncPromoUI::GetSourceForSyncPromoURL(
        web_ui->GetWebContents()->GetURL());
    is_start_page = source == SyncPromoUI::SOURCE_START_PAGE;
  }
  int title_id = is_start_page ? IDS_SYNC_PROMO_TITLE_SHORT :
                                 IDS_SYNC_PROMO_TITLE_EXISTING_USER;
  string16 short_product_name(GetStringUTF16(IDS_SHORT_PRODUCT_NAME));
  localized_strings->SetString(
      "promoTitle", GetStringFUTF16(title_id, short_product_name));

  localized_strings->SetString(
      "syncEverythingHelpURL", chrome::kSyncEverythingLearnMoreURL);
  localized_strings->SetString(
      "syncErrorHelpURL", chrome::kSyncErrorsHelpURL);

  std::string create_account_url = google_util::StringAppendGoogleLocaleParam(
      chrome::kSyncCreateNewAccountURL);
  string16 create_account = GetStringUTF16(IDS_SYNC_CREATE_ACCOUNT);
  create_account= UTF8ToUTF16("<a id='create-account-link' target='_blank' "
      "class='account-link' href='" + create_account_url + "'>") +
      create_account + UTF8ToUTF16("</a>");
  localized_strings->SetString("createAccountLinkHTML",
      GetStringFUTF16(IDS_SYNC_CREATE_ACCOUNT_PREFIX, create_account));

  string16 sync_benefits_url(
      UTF8ToUTF16(google_util::StringAppendGoogleLocaleParam(
          chrome::kSyncLearnMoreURL)));
  localized_strings->SetString("promoLearnMoreURL", sync_benefits_url);

  static OptionsStringResource resources[] = {
    { "syncSetupConfigureTitle", IDS_SYNC_SETUP_CONFIGURE_TITLE },
    { "cannotBeBlank", IDS_SYNC_CANNOT_BE_BLANK },
    { "emailLabel", IDS_SYNC_LOGIN_EMAIL_NEW_LINE },
    { "passwordLabel", IDS_SYNC_LOGIN_PASSWORD_NEW_LINE },
    { "invalidCredentials", IDS_SYNC_INVALID_USER_CREDENTIALS },
    { "signin", IDS_SYNC_SIGNIN },
    { "couldNotConnect", IDS_SYNC_LOGIN_COULD_NOT_CONNECT },
    { "unrecoverableError", IDS_SYNC_UNRECOVERABLE_ERROR },
    { "errorLearnMore", IDS_LEARN_MORE },
    { "unrecoverableErrorHelpURL", IDS_SYNC_UNRECOVERABLE_ERROR_HELP_URL },
    { "cannotAccessAccount", IDS_SYNC_CANNOT_ACCESS_ACCOUNT },
    { "cancel", IDS_CANCEL },
    { "loginSuccess", IDS_SYNC_SUCCESS },
    { "settingUp", IDS_SYNC_LOGIN_SETTING_UP },
    { "errorSigningIn", IDS_SYNC_ERROR_SIGNING_IN },
    { "signinHeader", IDS_SYNC_PROMO_SIGNIN_HEADER},
    { "captchaInstructions", IDS_SYNC_GAIA_CAPTCHA_INSTRUCTIONS },
    { "invalidAccessCode", IDS_SYNC_INVALID_ACCESS_CODE_LABEL },
    { "enterAccessCode", IDS_SYNC_ENTER_ACCESS_CODE_LABEL },
    { "getAccessCodeHelp", IDS_SYNC_ACCESS_CODE_HELP_LABEL },
    { "getAccessCodeURL", IDS_SYNC_GET_ACCESS_CODE_URL },
    { "syncAllDataTypes", IDS_SYNC_EVERYTHING },
    { "chooseDataTypes", IDS_SYNC_CHOOSE_DATATYPES },
    { "bookmarks", IDS_SYNC_DATATYPE_BOOKMARKS },
    { "preferences", IDS_SYNC_DATATYPE_PREFERENCES },
    { "autofill", IDS_SYNC_DATATYPE_AUTOFILL },
    { "themes", IDS_SYNC_DATATYPE_THEMES },
    { "passwords", IDS_SYNC_DATATYPE_PASSWORDS },
    { "extensions", IDS_SYNC_DATATYPE_EXTENSIONS },
    { "typedURLs", IDS_SYNC_DATATYPE_TYPED_URLS },
    { "apps", IDS_SYNC_DATATYPE_APPS },
    { "openTabs", IDS_SYNC_DATATYPE_TABS },
    { "syncZeroDataTypesError", IDS_SYNC_ZERO_DATA_TYPES_ERROR },
    { "serviceUnavailableError", IDS_SYNC_SETUP_ABORTED_BY_PENDING_CLEAR },
    { "encryptAllLabel", IDS_SYNC_ENCRYPT_ALL_LABEL },
    { "googleOption", IDS_SYNC_PASSPHRASE_OPT_GOOGLE },
    { "explicitOption", IDS_SYNC_PASSPHRASE_OPT_EXPLICIT },
    { "sectionGoogleMessage", IDS_SYNC_PASSPHRASE_MSG_GOOGLE },
    { "sectionExplicitMessage", IDS_SYNC_PASSPHRASE_MSG_EXPLICIT },
    { "passphraseLabel", IDS_SYNC_PASSPHRASE_LABEL },
    { "confirmLabel", IDS_SYNC_CONFIRM_PASSPHRASE_LABEL },
    { "emptyErrorMessage", IDS_SYNC_EMPTY_PASSPHRASE_ERROR },
    { "mismatchErrorMessage", IDS_SYNC_PASSPHRASE_MISMATCH_ERROR },
    { "passphraseWarning", IDS_SYNC_PASSPHRASE_WARNING },
    { "customizeLinkLabel", IDS_SYNC_CUSTOMIZE_LINK_LABEL },
    { "confirmSyncPreferences", IDS_SYNC_CONFIRM_SYNC_PREFERENCES },
    { "syncEverything", IDS_SYNC_SYNC_EVERYTHING },
    { "useDefaultSettings", IDS_SYNC_USE_DEFAULT_SETTINGS },
    { "passphraseSectionTitle", IDS_SYNC_PASSPHRASE_SECTION_TITLE },
    { "privacyDashboardLink", IDS_SYNC_PRIVACY_DASHBOARD_LINK_LABEL },
    { "enterPassphraseTitle", IDS_SYNC_ENTER_PASSPHRASE_TITLE },
    { "enterPassphraseBody", IDS_SYNC_ENTER_PASSPHRASE_BODY },
    { "enterGooglePassphraseBody", IDS_SYNC_ENTER_GOOGLE_PASSPHRASE_BODY },
    { "passphraseLabel", IDS_SYNC_PASSPHRASE_LABEL },
    { "incorrectPassphrase", IDS_SYNC_INCORRECT_PASSPHRASE },
    { "passphraseWarning", IDS_SYNC_PASSPHRASE_WARNING },
    { "cancelWarningHeader", IDS_SYNC_PASSPHRASE_CANCEL_WARNING_HEADER },
    { "cancelWarning", IDS_SYNC_PASSPHRASE_CANCEL_WARNING },
    { "yes", IDS_SYNC_PASSPHRASE_CANCEL_YES },
    { "no", IDS_SYNC_PASSPHRASE_CANCEL_NO },
    { "sectionExplicitMessagePrefix", IDS_SYNC_PASSPHRASE_MSG_EXPLICIT_PREFIX },
    { "sectionExplicitMessagePostfix",
        IDS_SYNC_PASSPHRASE_MSG_EXPLICIT_POSTFIX },
    { "encryptedDataTypesTitle", IDS_SYNC_ENCRYPTION_DATA_TYPES_TITLE },
    { "encryptSensitiveOption", IDS_SYNC_ENCRYPT_SENSITIVE_DATA },
    { "encryptAllOption", IDS_SYNC_ENCRYPT_ALL_DATA },
    { "encryptAllOption", IDS_SYNC_ENCRYPT_ALL_DATA },
    { "aspWarningText", IDS_SYNC_ASP_PASSWORD_WARNING_TEXT },
    { "promoPageTitle", IDS_SYNC_PROMO_TAB_TITLE },
    { "promoSkipButton", IDS_SYNC_PROMO_SKIP_BUTTON },
    { "promoAdvanced", IDS_SYNC_PROMO_ADVANCED },
    { "promoLearnMore", IDS_LEARN_MORE },
    { "promoTitleShort", IDS_SYNC_PROMO_MESSAGE_TITLE_SHORT },
  };

  RegisterStrings(localized_strings, resources, arraysize(resources));
  RegisterTitle(localized_strings, "syncSetupOverlay", IDS_SYNC_SETUP_TITLE);
}

void SyncSetupHandler::DisplayConfigureSync(bool show_advanced) {
  // Should only be called if user is signed in, so no longer need our
  // SigninTracker.
  signin_tracker_.reset();
  configuring_sync_ = true;
  ProfileSyncService* service = GetSyncService();

  // Setup args for the sync configure screen:
  //   showSyncEverythingPage: false to skip directly to the configure screen
  //   syncAllDataTypes: true if the user wants to sync everything
  //   <data_type>_registered: true if the associated data type is supported
  //   sync_<data_type>: true if the user wants to sync that specific data type
  //   encryptionEnabled: true if sync supports encryption
  //   encryptAllData: true if user wants to encrypt all data (not just
  //       passwords)
  //   usePassphrase: true if the data is encrypted with a secondary passphrase
  //   show_passphrase: true if a passphrase is needed to decrypt the sync data
  // TODO(atwilson): Convert all to unix_hacker style (http://crbug.com/119646).
  DictionaryValue args;

  // Tell the UI layer which data types are registered/enabled by the user.
  const syncable::ModelTypeSet registered_types =
      service->GetRegisteredDataTypes();
  const syncable::ModelTypeSet preferred_types =
      service->GetPreferredDataTypes();
  for (size_t i = 0; i < kNumDataTypes; ++i) {
    const std::string key_name = kDataTypeNames[i];
    args.SetBoolean(key_name + "_registered",
                    registered_types.Has(kDataTypes[i]));
    args.SetBoolean("sync_" + key_name, preferred_types.Has(kDataTypes[i]));
  }
  browser_sync::SyncPrefs sync_prefs(GetProfile()->GetPrefs());
  args.SetBoolean("showSyncEverythingPage", !show_advanced);
  args.SetBoolean("syncAllDataTypes", sync_prefs.HasKeepEverythingSynced());
  args.SetBoolean("encryptAllData", service->EncryptEverythingEnabled());
  args.SetBoolean("usePassphrase", service->IsUsingSecondaryPassphrase());
  args.SetBoolean("show_passphrase",
                  service->IsPassphraseRequiredForDecryption());

  StringValue page("configure");
  web_ui()->CallJavascriptFunction(
      "SyncSetupOverlay.showSyncSetupPage", page, args);
}

void SyncSetupHandler::ConfigureSyncDone() {
  StringValue page("done");
  web_ui()->CallJavascriptFunction(
      "SyncSetupOverlay.showSyncSetupPage", page);

  // Suppress the sync promo once the user signs into sync. This way the user
  // doesn't see the sync promo even if they sign out of sync later on.
  SyncPromoUI::SetUserSkippedSyncPromo(GetProfile());

  ProfileSyncService* service = GetSyncService();
  if (!service->HasSyncSetupCompleted()) {
    // This is the first time configuring sync, so log it.
    FilePath profile_file_path = GetProfile()->GetPath();
    ProfileMetrics::LogProfileSyncSignIn(profile_file_path);

    // We're done configuring, so notify ProfileSyncService that it is OK to
    // start syncing.
    service->SetSyncSetupCompleted();
  }
}

bool SyncSetupHandler::IsActiveLogin() const {
  // LoginUIService can be NULL if page is brought up in incognito mode
  // (i.e. if the user is running in guest mode in cros and brings up settings).
  LoginUIService* service = GetLoginUIService();
  return service && (service->current_login_ui() == web_ui());
}

void SyncSetupHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback("SyncSetupDidClosePage",
      base::Bind(&SyncSetupHandler::OnDidClosePage,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("SyncSetupSubmitAuth",
      base::Bind(&SyncSetupHandler::HandleSubmitAuth,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("SyncSetupConfigure",
      base::Bind(&SyncSetupHandler::HandleConfigure,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("SyncSetupAttachHandler",
      base::Bind(&SyncSetupHandler::HandleAttachHandler,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("SyncSetupShowErrorUI",
      base::Bind(&SyncSetupHandler::HandleShowErrorUI,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("SyncSetupShowSetupUI",
      base::Bind(&SyncSetupHandler::HandleShowSetupUI,
                 base::Unretained(this)));
}

SigninManager* SyncSetupHandler::GetSignin() const {
  return SigninManagerFactory::GetForProfile(GetProfile());
}

void SyncSetupHandler::DisplayGaiaLogin(bool fatal_error) {
  DisplayGaiaLoginWithErrorMessage(string16(), fatal_error);
}

void SyncSetupHandler::DisplayGaiaLoginWithErrorMessage(
    const string16& error_message, bool fatal_error) {
  // We are no longer configuring sync if the login screen is visible.
  // If the user exits the signin wizard after this without configuring sync,
  // CloseSyncSetup() will ensure they are logged out.
  configuring_sync_ = false;

  // Setup args for the GAIA login screen:
  //   error_message: custom error message to display.
  //   fatalError: fatal error message to display.
  //   error: GoogleServiceAuthError from previous login attempt (0 if none).
  //   user: The email the user most recently entered.
  //   editable_user: Whether the username field should be editable.
  //   captchaUrl: The captcha image to display to the user (empty if none).
  // TODO(atwilson): Convert all to unix_hacker style (http://crbug.com/119646).
  std::string user, captcha;
  int error;
  bool editable_user;
  if (!last_attempted_user_email_.empty()) {
    // This is a repeat of a login attempt.
    user = last_attempted_user_email_;
    error = last_signin_error_.state();
    captcha = last_signin_error_.captcha().image_url.spec();
    editable_user = true;
  } else {
    // Fresh login attempt - lock in the authenticated username if there is
    // one (don't let the user change it).
    user = GetSignin()->GetAuthenticatedUsername();
    error = 0;
    editable_user = user.empty();
  }
  DictionaryValue args;
  args.SetString("user", user);
  args.SetInteger("error", error);
  args.SetBoolean("editable_user", editable_user);
  if (!error_message.empty())
    args.SetString("error_message", error_message);
  if (fatal_error)
    args.SetBoolean("fatalError", true);
  args.SetString("captchaUrl", captcha);
  StringValue page("login");
  web_ui()->CallJavascriptFunction(
      "SyncSetupOverlay.showSyncSetupPage", page, args);
}

void SyncSetupHandler::RecordSignin() {
  // By default, do nothing - subclasses can override.
}

void SyncSetupHandler::DisplayGaiaSuccessAndClose() {
  RecordSignin();
  web_ui()->CallJavascriptFunction("SyncSetupOverlay.showSuccessAndClose");
}

void SyncSetupHandler::DisplayGaiaSuccessAndSettingUp() {
  RecordSignin();
  web_ui()->CallJavascriptFunction("SyncSetupOverlay.showSuccessAndSettingUp");
}

void SyncSetupHandler::ShowFatalError() {
  // For now, just send the user back to the login page. Ultimately may want
  // to give different feedback (especially for chromeos).
  DisplayGaiaLogin(true);
}

void SyncSetupHandler::OnDidClosePage(const ListValue* args) {
  CloseSyncSetup();
}

void SyncSetupHandler::HandleSubmitAuth(const ListValue* args) {
  std::string json;
  if (!args->GetString(0, &json)) {
    NOTREACHED() << "Could not read JSON argument";
    return;
  }

  if (json.empty())
    return;

  std::string username, password, captcha, access_code;
  if (!GetAuthData(json, &username, &password, &captcha, &access_code)) {
    // The page sent us something that we didn't understand.
    // This probably indicates a programming error.
    NOTREACHED();
    return;
  }

  string16 error_message;
  if (!IsLoginAuthDataValid(username, &error_message)) {
    DisplayGaiaLoginWithErrorMessage(error_message, false);
    return;
  }

  TryLogin(username, password, captcha, access_code);
}

void SyncSetupHandler::TryLogin(const std::string& username,
                                const std::string& password,
                                const std::string& captcha,
                                const std::string& access_code) {
  DCHECK(IsActiveLogin());
  // Make sure we are listening for signin traffic.
  if (!signin_tracker_.get())
    signin_tracker_.reset(new SigninTracker(GetProfile(), this));

  last_attempted_user_email_ = username;

  // User is trying to log in again so reset the cached error.
  last_signin_error_ = GoogleServiceAuthError::None();

  // If we're just being called to provide an ASP, then pass it to the
  // SigninManager and wait for the next step.
  SigninManager* signin = GetSignin();
  if (!access_code.empty()) {
    signin->ProvideSecondFactorAccessCode(access_code);
    return;
  }

  // Kick off a sign-in through the signin manager.
  signin->StartSignIn(username,
                      password,
                      last_signin_error_.captcha().token,
                      captcha);
}

void SyncSetupHandler::GaiaCredentialsValid() {
  DCHECK(IsActiveLogin());
  // The user has submitted credentials, which indicates they don't want to
  // suppress start up anymore.
  GetSyncService()->UnsuppressAndStart();

  // Gaia credentials are valid - update the UI.
  DisplayGaiaSuccessAndSettingUp();
}

void SyncSetupHandler::SigninFailed(const GoogleServiceAuthError& error) {
  last_signin_error_ = error;
  // Got a failed signin - this is either just a typical auth error, or a
  // sync error (treat sync errors as "fatal errors" - i.e. non-auth errors).
  DisplayGaiaLogin(GetSyncService()->unrecoverable_error_detected());
}

Profile* SyncSetupHandler::GetProfile() const {
  return Profile::FromWebUI(web_ui());
}

ProfileSyncService* SyncSetupHandler::GetSyncService() const {
  return ProfileSyncServiceFactory::GetForProfile(GetProfile());
}

void SyncSetupHandler::SigninSuccess() {
  DCHECK(GetSyncService()->sync_initialized());
  // If we have signed in while sync is already setup, it must be due to some
  // kind of re-authentication flow. In that case, just close the signin dialog
  // rather than forcing the user to go through sync configuration.
  if (GetSyncService()->HasSyncSetupCompleted())
    DisplayGaiaSuccessAndClose();
  else
    DisplayConfigureSync(false);
}

void SyncSetupHandler::HandleConfigure(const ListValue* args) {
  std::string json;
  if (!args->GetString(0, &json)) {
    NOTREACHED() << "Could not read JSON argument";
    return;
  }
  if (json.empty()) {
    NOTREACHED();
    return;
  }

  SyncConfigInfo configuration;
  if (!GetConfiguration(json, &configuration)) {
    // The page sent us something that we didn't understand.
    // This probably indicates a programming error.
    NOTREACHED();
    return;
  }

  // Start configuring the ProfileSyncService using the configuration passed
  // to us from the JS layer.
  ProfileSyncService* service = GetSyncService();

  // If the sync engine has shutdown for some reason, just close the sync
  // dialog.
  if (!service->sync_initialized()) {
    CloseOverlay();
    return;
  }

  // Note: Data encryption will not occur until configuration is complete
  // (when the PSS receives its CONFIGURE_DONE notification from the sync
  // backend), so the user still has a chance to cancel out of the operation
  // if (for example) some kind of passphrase error is encountered.
  if (configuration.encrypt_all)
    service->EnableEncryptEverything();

  if (!configuration.passphrase.empty()) {
    if (service->IsPassphraseRequiredForDecryption()) {
      // If we have pending keys, try to decrypt them with the provided
      // passphrase. We don't care if this succeeds or fails since we check
      // the result below by calling IsPassphraseRequiredForDecryption().
      ignore_result(service->SetDecryptionPassphrase(configuration.passphrase));
    } else {
      // OK, the user sent us a passphrase, but we don't have pending keys. So
      // it either means that the pending keys were resolved somehow since the
      // time the UI was displayed (re-encryption, pending passphrase change,
      // etc) or the user wants to re-encrypt.
      if (!configuration.passphrase_is_gaia &&
          !service->IsUsingSecondaryPassphrase()) {
        // User passed us a secondary passphrase, and the data is encrypted
        // with a GAIA passphrase so they must want to encrypt.
        service->SetEncryptionPassphrase(configuration.passphrase,
                                         ProfileSyncService::EXPLICIT);
      }
    }
  }

  service->OnUserChoseDatatypes(configuration.sync_everything,
                                configuration.data_types);

  // Need to call IsPassphraseRequiredForDecryption() *after* calling
  // OnUserChoseDatatypes() because the user may have just disabled the
  // encrypted datatypes.
  if (service->IsPassphraseRequiredForDecryption()) {
    // User didn't enter a valid passphrase, but we need one - go whine to them.
    DisplayConfigureSync(true);
  } else {
    // Configuration is complete.
    ConfigureSyncDone();
  }

  ProfileMetrics::LogProfileSyncInfo(ProfileMetrics::SYNC_CUSTOMIZE);
  if (configuration.encrypt_all)
    ProfileMetrics::LogProfileSyncInfo(ProfileMetrics::SYNC_ENCRYPT);
  if (configuration.passphrase_is_gaia && !configuration.passphrase.empty())
    ProfileMetrics::LogProfileSyncInfo(ProfileMetrics::SYNC_PASSPHRASE);
  if (!configuration.sync_everything)
    ProfileMetrics::LogProfileSyncInfo(ProfileMetrics::SYNC_CHOOSE);
}

void SyncSetupHandler::HandleAttachHandler(const ListValue* args) {
  bool force_login = false;
  std::string json;
  if (args->GetString(0, &json) && !json.empty()) {
    scoped_ptr<Value> parsed_value(base::JSONReader::Read(json, false));
    DictionaryValue* result = static_cast<DictionaryValue*>(parsed_value.get());
    result->GetBoolean("forceLogin", &force_login);
  }

  OpenSyncSetup(force_login);
}

void SyncSetupHandler::HandleShowErrorUI(const ListValue* args) {
  DCHECK(!configuring_sync_);

  ProfileSyncService* service = GetSyncService();
  DCHECK(service);

#if defined(OS_CHROMEOS)
  if (service->GetAuthError().state() != GoogleServiceAuthError::NONE) {
    DLOG(INFO) << "Signing out the user to fix a sync error.";
    BrowserList::GetLastActive()->ExecuteCommand(IDC_EXIT);
    return;
  }
#endif

  service->ShowErrorUI();
}

void SyncSetupHandler::HandleShowSetupUI(const ListValue* args) {
  DCHECK(!configuring_sync_);
  OpenSyncSetup(false);
}

void SyncSetupHandler::CloseSyncSetup() {
  // TODO(atwilson): Move UMA tracking of signin events out of sync module.
  if (IsActiveLogin()) {
    ProfileSyncService* sync_service = GetSyncService();
    if (!sync_service->HasSyncSetupCompleted()) {
      if (signin_tracker_.get()) {
        ProfileSyncService::SyncEvent(
            ProfileSyncService::CANCEL_DURING_SIGNON);
      } else if (configuring_sync_) {
        ProfileSyncService::SyncEvent(
            ProfileSyncService::CANCEL_DURING_CONFIGURE);
      } else {
        ProfileSyncService::SyncEvent(
            ProfileSyncService::CANCEL_FROM_SIGNON_WITHOUT_AUTH);
      }
    }

    // Let the various services know that we're no longer active.
    GetLoginUIService()->LoginUIClosed(web_ui());
    if (sync_service)
      sync_service->set_setup_in_progress(false);

    // Make sure user isn't left half-logged-in (signed in, but without sync
    // started up). If the user hasn't finished setting up sync, then sign out
    // and shut down sync.

    if (sync_service && !sync_service->HasSyncSetupCompleted()) {
      DVLOG(1) << "Signin aborted by user action";
      sync_service->DisableForUser();
      GetSignin()->SignOut();
    }
  }

  configuring_sync_ = false;
  signin_tracker_.reset();
}

void SyncSetupHandler::OpenSyncSetup(bool force_login) {
  ProfileSyncService* service = GetSyncService();
  if (!service) {
    // If there's no sync service, the user tried to manually invoke a syncSetup
    // URL, but sync features are disabled.  We need to close the overlay for
    // this (rare) case.
    DLOG(WARNING) << "Closing sync UI because sync is disabled";
    CloseOverlay();
    return;
  }

  // If the wizard is already visible, just focus that one.
  if (FocusExistingWizardIfPresent()) {
    if (!IsActiveLogin())
      CloseOverlay();
    return;
  }

  // Notify services that we are now active.
  GetLoginUIService()->SetLoginUI(web_ui());
  service->set_setup_in_progress(true);

  if (!force_login && service->HasSyncSetupCompleted()) {
    // User is already logged in. They must have brought up the config wizard
    // via the "Advanced..." button or the wrench menu.
    DisplayConfigureSync(true);
  } else {
    // User is not logged in - need to display login UI.
    DisplayGaiaLogin(false);
  }

  ShowSetupUI();
}

// Private member functions.

bool SyncSetupHandler::FocusExistingWizardIfPresent() {
  LoginUIService* service = GetLoginUIService();
  if (!service->current_login_ui())
    return false;
  service->FocusLoginUI();
  return true;
}

LoginUIService* SyncSetupHandler::GetLoginUIService() const {
  return LoginUIServiceFactory::GetForProfile(GetProfile());
}

void SyncSetupHandler::CloseOverlay() {
  CloseSyncSetup();
  web_ui()->CallJavascriptFunction("OptionsPage.closeOverlay");
}

bool SyncSetupHandler::IsLoginAuthDataValid(const std::string& username,
                                            string16* error_message) {
  // Happens during unit tests.
  if (!web_ui() || !profile_manager_)
    return true;

  if (username.empty())
    return true;

  // Check if the username is already in use by another profile.
  const ProfileInfoCache& cache = profile_manager_->GetProfileInfoCache();
  size_t current_profile_index =
      cache.GetIndexOfProfileWithPath(GetProfile()->GetPath());
  string16 username_utf16 = UTF8ToUTF16(username);

  for (size_t i = 0; i < cache.GetNumberOfProfiles(); ++i) {
    if (i != current_profile_index && AreUserNamesEqual(
        cache.GetUserNameOfProfileAtIndex(i), username_utf16)) {
      *error_message = l10n_util::GetStringUTF16(
          IDS_SYNC_USER_NAME_IN_USE_ERROR);
      return false;
    }
  }

  return true;
}
