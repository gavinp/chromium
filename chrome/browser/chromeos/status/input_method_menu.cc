// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/status/input_method_menu.h"

#include <string>
#include <vector>

#include "base/string_split.h"
#include "base/string_util.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/input_method/input_method_util.h"
#include "chrome/browser/chromeos/language_preferences.h"
#include "chrome/browser/chromeos/status/status_area_view_chromeos.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/user_metrics.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/simple_menu_model.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/views/controls/menu/menu_model_adapter.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/menu/submenu_view.h"
#include "ui/views/widget/widget.h"

using content::UserMetricsAction;

// The language menu consists of 3 parts (in this order):
//
//   (1) input method names. The size of the list is always >= 1.
//   (2) input method properties. This list might be empty.
//   (3) "Customize language and input..." button.
//
// Example of the menu (Japanese):
//
// ============================== (border of the popup window)
// [ ] English                    (|index| in the following functions is 0)
// [*] Japanese
// [ ] Chinese (Simplified)
// ------------------------------ (separator)
// [*] Hiragana                   (index = 5, The property has 2 radio groups)
// [ ] Katakana
// [ ] HalfWidthKatakana
// [*] Roman
// [ ] Kana
// ------------------------------ (separator)
// Customize language and input...(index = 11)
// ============================== (border of the popup window)
//
// Example of the menu (Simplified Chinese):
//
// ============================== (border of the popup window)
// [ ] English
// [ ] Japanese
// [*] Chinese (Simplified)
// ------------------------------ (separator)
// Switch to full letter mode     (The property has 2 command buttons)
// Switch to half punctuation mode
// ------------------------------ (separator)
// Customize language and input...
// ============================== (border of the popup window)
//

namespace {

// Constants to specify the type of items in |model_|.
enum {
  COMMAND_ID_INPUT_METHODS = 0,  // English, Chinese, Japanese, Arabic, ...
  COMMAND_ID_IME_PROPERTIES,  // Hiragana, Katakana, ...
  COMMAND_ID_CUSTOMIZE_LANGUAGE,  // "Customize language and input..." button.
};

// A group ID for IME properties starts from 0. We use the huge value for the
// input method list to avoid conflict.
const int kRadioGroupLanguage = 1 << 16;
const int kRadioGroupNone = -1;

// Returns the language name for the given |language_code|.
string16 GetLanguageName(const std::string& language_code) {
  const string16 language_name = l10n_util::GetDisplayNameForLocale(
      language_code, g_browser_process->GetApplicationLocale(), true);
  return language_name;
}

PrefService* GetPrefService() {
  Profile* profile = ProfileManager::GetDefaultProfile();
  if (profile)
    return profile->GetPrefs();
  return NULL;
}

}  // namespace

namespace chromeos {

using input_method::InputMethodManager;

////////////////////////////////////////////////////////////////////////////////
// InputMethodMenu

InputMethodMenu::InputMethodMenu()
    : initialized_prefs_(false),
      initialized_observers_(false),
      input_method_descriptors_(InputMethodManager::GetInstance()->
                                GetActiveInputMethods()),
      model_(new ui::SimpleMenuModel(NULL)),
      ALLOW_THIS_IN_INITIALIZER_LIST(input_method_menu_delegate_(
          new views::MenuModelAdapter(this))),
      input_method_menu_(
          new views::MenuItemView(input_method_menu_delegate_.get())),
      input_method_menu_runner_(new views::MenuRunner(input_method_menu_)),
      minimum_input_method_menu_width_(0),
      menu_alignment_(views::MenuItemView::TOPRIGHT) {
  DCHECK(input_method_descriptors_.get() &&
         !input_method_descriptors_->empty());

  // Sync current and previous input methods on Chrome prefs with ibus-daemon.
  if (StatusAreaViewChromeos::IsBrowserMode())
    InitializePrefMembers();

  if (StatusAreaViewChromeos::IsLoginMode()) {
    registrar_.Add(this,
                   chrome::NOTIFICATION_LOGIN_USER_CHANGED,
                   content::NotificationService::AllSources());
    // On Aura status area is not recreated on sign in. Instead, 2 notifications
    // are sent to Chrome on sign in: NOTIFICATION_LOGIN_USER_CHANGED with
    // StatusAreaViewChromeos::IsLoginMode() and NOTIFICATION_SESSION_STARTED
    // with StatusAreaViewChromeos::IsBrowserMode().
    // In case of Chrome crash, Chrome will be reloaded but IsLoginMode() will
    // return false at this point so NOTIFICATION_SESSION_STARTED will be
    // ignored and all initialization will happen in ctor.
    registrar_.Add(this,
                   chrome::NOTIFICATION_SESSION_STARTED,
                   content::NotificationService::AllSources());
  }
  AddObservers();
}

InputMethodMenu::~InputMethodMenu() {
  // RemoveObservers() is no-op if |this| object is already removed from the
  // observer list
  RemoveObservers();
}

////////////////////////////////////////////////////////////////////////////////
// ui::MenuModel implementation:

int InputMethodMenu::GetCommandIdAt(int index) const {
  return index;
}

bool InputMethodMenu::IsItemDynamicAt(int index) const {
  // Menu content for the language button could change time by time.
  return true;
}

bool InputMethodMenu::GetAcceleratorAt(
    int index, ui::Accelerator* accelerator) const {
  // Views for Chromium OS does not support accelerators yet.
  return false;
}

bool InputMethodMenu::IsItemCheckedAt(int index) const {
  DCHECK_GE(index, 0);
  DCHECK(input_method_descriptors_.get());

  if (IndexIsInInputMethodList(index)) {
    const input_method::InputMethodDescriptor& input_method
        = input_method_descriptors_->at(index);
    return input_method == InputMethodManager::GetInstance()->
          GetCurrentInputMethod();
  }

  if (GetPropertyIndex(index, &index)) {
    const input_method::InputMethodPropertyList& property_list
        = InputMethodManager::GetInstance()->GetCurrentInputMethodProperties();
    return property_list.at(index).is_selection_item_checked;
  }

  // Separator(s) or the "Customize language and input..." button.
  return false;
}

int InputMethodMenu::GetGroupIdAt(int index) const {
  DCHECK_GE(index, 0);

  if (IndexIsInInputMethodList(index))
    return kRadioGroupLanguage;

  if (GetPropertyIndex(index, &index)) {
    const input_method::InputMethodPropertyList& property_list
        = InputMethodManager::GetInstance()->GetCurrentInputMethodProperties();
    return property_list.at(index).selection_item_id;
  }

  return kRadioGroupNone;
}

bool InputMethodMenu::HasIcons() const  {
  // We don't support icons on Chrome OS.
  return false;
}

bool InputMethodMenu::GetIconAt(int index, SkBitmap* icon) {
  return false;
}

ui::ButtonMenuItemModel* InputMethodMenu::GetButtonMenuItemAt(
    int index) const {
  return NULL;
}

bool InputMethodMenu::IsEnabledAt(int index) const {
  // Just return true so all input method names and input method propertie names
  // could be clicked.
  return true;
}

ui::MenuModel* InputMethodMenu::GetSubmenuModelAt(int index) const {
  // We don't use nested menus.
  return NULL;
}

void InputMethodMenu::HighlightChangedTo(int index) {
  // Views for Chromium OS does not support this interface yet.
}

void InputMethodMenu::MenuWillShow() {
  // Views for Chromium OS does not support this interface yet.
}

void InputMethodMenu::SetMenuModelDelegate(ui::MenuModelDelegate* delegate) {
  // Not needed for current usage.
}

int InputMethodMenu::GetItemCount() const {
  if (!model_.get()) {
    // Model is not constructed yet. This means that
    // InputMethodMenu is being constructed. Return zero.
    return 0;
  }
  return model_->GetItemCount();
}

ui::MenuModel::ItemType InputMethodMenu::GetTypeAt(int index) const {
  DCHECK_GE(index, 0);

  if (IndexPointsToConfigureImeMenuItem(index)) {
    return ui::MenuModel::TYPE_COMMAND;  // "Customize language and input"
  }

  if (IndexIsInInputMethodList(index))
    return ui::MenuModel::TYPE_RADIO;

  if (GetPropertyIndex(index, &index)) {
    const input_method::InputMethodPropertyList& property_list
        = InputMethodManager::GetInstance()->GetCurrentInputMethodProperties();
    if (property_list.at(index).is_selection_item) {
      return ui::MenuModel::TYPE_RADIO;
    }
    return ui::MenuModel::TYPE_COMMAND;
  }

  return ui::MenuModel::TYPE_SEPARATOR;
}

string16 InputMethodMenu::GetLabelAt(int index) const {
  DCHECK_GE(index, 0);
  DCHECK(input_method_descriptors_.get());

  // We use IDS_OPTIONS_SETTINGS_LANGUAGES_CUSTOMIZE here as the button
  // opens the same dialog that is opened from the main options dialog.
  if (IndexPointsToConfigureImeMenuItem(index)) {
    return l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_LANGUAGES_CUSTOMIZE);
  }

  string16 name;
  if (IndexIsInInputMethodList(index)) {
    name = GetTextForMenu(input_method_descriptors_->at(index));
  } else if (GetPropertyIndex(index, &index)) {
    InputMethodManager* manager = InputMethodManager::GetInstance();
    const input_method::InputMethodPropertyList& property_list =
        manager->GetCurrentInputMethodProperties();
    return manager->GetInputMethodUtil()->TranslateString(
        property_list.at(index).label);
  }

  return name;
}

void InputMethodMenu::ActivatedAt(int index) {
  DCHECK_GE(index, 0);
  DCHECK(input_method_descriptors_.get());

  if (IndexPointsToConfigureImeMenuItem(index)) {
    OpenConfigUI();
    return;
  }

  if (IndexIsInInputMethodList(index)) {
    // Inter-IME switching.
    const input_method::InputMethodDescriptor& input_method
        = input_method_descriptors_->at(index);
    InputMethodManager::GetInstance()->ChangeInputMethod(
        input_method.id());
    content::RecordAction(
        UserMetricsAction("LanguageMenuButton_InputMethodChanged"));
    return;
  }

  if (GetPropertyIndex(index, &index)) {
    // Intra-IME switching (e.g. Japanese-Hiragana to Japanese-Katakana).
    const input_method::InputMethodPropertyList& property_list
        = InputMethodManager::GetInstance()->GetCurrentInputMethodProperties();
    const std::string key = property_list.at(index).key;
    if (property_list.at(index).is_selection_item) {
      // Radio button is clicked.
      const int id = property_list.at(index).selection_item_id;
      // First, deactivate all other properties in the same radio group.
      for (int i = 0; i < static_cast<int>(property_list.size()); ++i) {
        if (i != index && id == property_list.at(i).selection_item_id) {
          InputMethodManager::GetInstance()->SetImePropertyActivated(
              property_list.at(i).key, false);
        }
      }
      // Then, activate the property clicked.
      InputMethodManager::GetInstance()->SetImePropertyActivated(
          key, true);
    } else {
      // Command button like "Switch to half punctuation mode" is clicked.
      // We can always use "Deactivate" for command buttons.
      InputMethodManager::GetInstance()->SetImePropertyActivated(
          key, false);
    }
    return;
  }

  LOG(ERROR) << "Unexpected index: " << index;
}

////////////////////////////////////////////////////////////////////////////////
// views::MenuButtonListener implementation:

void InputMethodMenu::OnMenuButtonClicked(views::View* source,
                                          const gfx::Point& point) {
  PrepareForMenuOpen();

  if (minimum_input_method_menu_width_ > 0) {
    DCHECK(input_method_menu_->HasSubmenu());
    views::SubmenuView* submenu = input_method_menu_->GetSubmenu();
    submenu->set_minimum_preferred_width(minimum_input_method_menu_width_);
  }

  gfx::Point screen_location;
  views::View::ConvertPointToScreen(source, &screen_location);
  gfx::Rect bounds(screen_location, source->size());
  if (input_method_menu_runner_->RunMenuAt(
          source->GetWidget()->GetTopLevelWidget(), NULL, bounds,
          menu_alignment_, views::MenuRunner::HAS_MNEMONICS) ==
      views::MenuRunner::MENU_DELETED)
    return;
}

////////////////////////////////////////////////////////////////////////////////
// InputMethodManager::Observer implementation:

void InputMethodMenu::InputMethodChanged(
    InputMethodManager* manager,
    const input_method::InputMethodDescriptor& current_input_method,
    size_t num_active_input_methods) {
  UpdateUIFromInputMethod(current_input_method, num_active_input_methods);
}

// TODO(yusukes): Move code for handling preferences to chromeos/input_method/.
void InputMethodMenu::PreferenceUpdateNeeded(
    InputMethodManager* manager,
    const input_method::InputMethodDescriptor& previous_input_method,
    const input_method::InputMethodDescriptor& current_input_method) {
  if (StatusAreaViewChromeos::IsBrowserMode()) {
    if (initialized_prefs_) {  // make sure we're not in unit tests.
      // Sometimes (e.g. initial boot) |previous_input_method.id()| is empty.
      previous_input_method_pref_.SetValue(previous_input_method.id());
      current_input_method_pref_.SetValue(current_input_method.id());
    }
  } else if (StatusAreaViewChromeos::IsLoginMode()) {
    if (g_browser_process && g_browser_process->local_state()) {
      g_browser_process->local_state()->SetString(
          language_prefs::kPreferredKeyboardLayout, current_input_method.id());
    }
  }
}

void InputMethodMenu::PropertyListChanged(
    InputMethodManager* manager,
    const input_method::InputMethodPropertyList& current_ime_properties) {
  // Usual order of notifications of input method change is:
  // 1. RegisterProperties(empty)
  // 2. RegisterProperties(list-of-new-properties)
  // 3. GlobalInputMethodChanged
  // However, due to the asynchronicity, we occasionally (but rarely) face to
  // 1. RegisterProperties(empty)
  // 2. GlobalInputMethodChanged
  // 3. RegisterProperties(list-of-new-properties)
  // this order. On this unusual case, we must rebuild the menu after the last
  // RegisterProperties. For the other cases, no rebuild is needed. Actually
  // it is better to be avoided. Otherwise users can sometimes observe the
  // awkward clear-then-register behavior.
  if (!current_ime_properties.empty()) {
    const input_method::InputMethodDescriptor& input_method =
        manager->GetCurrentInputMethod();
    size_t num_active_input_methods = manager->GetNumActiveInputMethods();
    UpdateUIFromInputMethod(input_method, num_active_input_methods);
  }
}

void InputMethodMenu::FirstObserverIsAdded(InputMethodManager* manager) {
  // NOTICE: Since this function might be called from the constructor of this
  // class, it's better to avoid calling virtual functions.

  if (initialized_prefs_ && StatusAreaViewChromeos::IsBrowserMode()) {
    // Get the input method name in the Preferences file which was in use last
    // time, and switch to the method. We remember two input method names in the
    // preference so that the Control+space hot-key could work fine from the
    // beginning. InputMethodChanged() will be called soon and the indicator
    // will be updated.
    const std::string previous_input_method_id =
        previous_input_method_pref_.GetValue();
    if (!previous_input_method_id.empty()) {
      manager->ChangeInputMethod(previous_input_method_id);
    }
    const std::string current_input_method_id =
        current_input_method_pref_.GetValue();
    if (!current_input_method_id.empty()) {
      manager->ChangeInputMethod(current_input_method_id);
    }
  }
}

void InputMethodMenu::PrepareForMenuOpen() {
  content::RecordAction(UserMetricsAction("LanguageMenuButton_Open"));
  PrepareMenuModel();
}

void InputMethodMenu::PrepareMenuModel() {
  input_method_descriptors_.reset(InputMethodManager::GetInstance()->
                                  GetActiveInputMethods());
  RebuildModel();
}

void InputMethodMenu::ActiveInputMethodsChanged(
    InputMethodManager* manager,
    const input_method::InputMethodDescriptor& current_input_method,
    size_t num_active_input_methods) {
  // Update the icon if active input methods are changed. See also
  // comments in UpdateUI() in input_method_menu_button.cc.
  UpdateUIFromInputMethod(current_input_method, num_active_input_methods);
}

void InputMethodMenu::UpdateUIFromInputMethod(
    const input_method::InputMethodDescriptor& input_method,
    size_t num_active_input_methods) {
  InputMethodManager* manager = InputMethodManager::GetInstance();
  const string16 name = manager->GetInputMethodUtil()->
      GetInputMethodShortName(input_method);
  const string16 tooltip = GetTextForMenu(input_method);
  UpdateUI(input_method.id(), name, tooltip, num_active_input_methods);
}

void InputMethodMenu::RebuildModel() {
  model_->Clear();
  string16 dummy_label = UTF8ToUTF16("");
  // Indicates if separator's needed before each section.
  bool need_separator = false;

  if (!input_method_descriptors_->empty()) {
    // We "abuse" the command_id and group_id arguments of AddRadioItem method.
    // A COMMAND_ID_XXX enum value is passed as command_id, and array index of
    // |input_method_descriptors_| or |property_list| is passed as group_id.
    for (size_t i = 0; i < input_method_descriptors_->size(); ++i) {
      model_->AddRadioItem(COMMAND_ID_INPUT_METHODS, dummy_label, i);
    }

    need_separator = true;
  }

  const input_method::InputMethodPropertyList& property_list
      = InputMethodManager::GetInstance()->GetCurrentInputMethodProperties();
  if (!property_list.empty()) {
    if (need_separator) {
      model_->AddSeparator();
    }
    for (size_t i = 0; i < property_list.size(); ++i) {
      model_->AddRadioItem(COMMAND_ID_IME_PROPERTIES, dummy_label, i);
    }
    need_separator = true;
  }

  if (ShouldSupportConfigUI()) {
    // Note: We use AddSeparator() for separators, and AddRadioItem() for all
    // other items even if an item is not actually a radio item.
    if (need_separator) {
      model_->AddSeparator();
    }
    model_->AddRadioItem(COMMAND_ID_CUSTOMIZE_LANGUAGE, dummy_label,
                         0 /* dummy */);
  }

  // Rebuild the menu from the model.
  input_method_menu_delegate_->BuildMenu(input_method_menu_);
}

bool InputMethodMenu::IndexIsInInputMethodList(int index) const {
  DCHECK_GE(index, 0);
  DCHECK(model_.get());
  if (index >= model_->GetItemCount()) {
    return false;
  }

  return ((model_->GetTypeAt(index) == ui::MenuModel::TYPE_RADIO) &&
          (model_->GetCommandIdAt(index) == COMMAND_ID_INPUT_METHODS) &&
          input_method_descriptors_.get() &&
          (index < static_cast<int>(input_method_descriptors_->size())));
}

bool InputMethodMenu::GetPropertyIndex(int index, int* property_index) const {
  DCHECK_GE(index, 0);
  DCHECK(property_index);
  DCHECK(model_.get());
  if (index >= model_->GetItemCount()) {
    return false;
  }

  if ((model_->GetTypeAt(index) == ui::MenuModel::TYPE_RADIO) &&
      (model_->GetCommandIdAt(index) == COMMAND_ID_IME_PROPERTIES)) {
    const int tmp_property_index = model_->GetGroupIdAt(index);
    const input_method::InputMethodPropertyList& property_list
        = InputMethodManager::GetInstance()->GetCurrentInputMethodProperties();
    if (tmp_property_index < static_cast<int>(property_list.size())) {
      *property_index = tmp_property_index;
      return true;
    }
  }
  return false;
}

bool InputMethodMenu::IndexPointsToConfigureImeMenuItem(int index) const {
  DCHECK_GE(index, 0);
  DCHECK(model_.get());
  if (index >= model_->GetItemCount()) {
    return false;
  }

  return ((model_->GetTypeAt(index) == ui::MenuModel::TYPE_RADIO) &&
          (model_->GetCommandIdAt(index) == COMMAND_ID_CUSTOMIZE_LANGUAGE));
}

string16 InputMethodMenu::GetTextForMenu(
    const input_method::InputMethodDescriptor& input_method) {
  if (!input_method.name().empty()) {
    // If the descriptor has a name, use it.
    return UTF8ToUTF16(input_method.name());
  }

  // We don't show language here.  Name of keyboard layout or input method
  // usually imply (or explicitly include) its language.

  input_method::InputMethodManager* manager =
      input_method::InputMethodManager::GetInstance();

  // Special case for German, French and Dutch: these languages have multiple
  // keyboard layouts and share the same layout of keyboard (Belgian). We need
  // to show explicitly the language for the layout. For Arabic, Amharic, and
  // Indic languages: they share "Standard Input Method".
  const string16 standard_input_method_text = l10n_util::GetStringUTF16(
      IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_STANDARD_INPUT_METHOD);
  const std::string language_code = input_method.language_code();

  string16 text =
      manager->GetInputMethodUtil()->TranslateString(input_method.id());
  if (text == standard_input_method_text ||
             language_code == "de" ||
             language_code == "fr" ||
             language_code == "nl") {
    text = GetLanguageName(language_code) + UTF8ToUTF16(" - ") + text;
  }

  DCHECK(!text.empty());
  return text;
}

void InputMethodMenu::RegisterPrefs(PrefService* local_state) {
  // We use an empty string here rather than a hardware keyboard layout name
  // since input_method::GetHardwareInputMethodId() might return a fallback
  // layout name if local_state->RegisterStringPref(kHardwareKeyboardLayout)
  // is not called yet.
  local_state->RegisterStringPref(language_prefs::kPreferredKeyboardLayout,
                                  "",
                                  PrefService::UNSYNCABLE_PREF);
}

void InputMethodMenu::Observe(int type,
                              const content::NotificationSource& source,
                              const content::NotificationDetails& details) {
  if (type == chrome::NOTIFICATION_LOGIN_USER_CHANGED) {
    // When a user logs in, we should remove |this| object from the observer
    // list so that PreferenceUpdateNeeded() does not update the local state
    // anymore.
    RemoveObservers();
  }
  if (type == chrome::NOTIFICATION_SESSION_STARTED) {
    InitializePrefMembers();
    AddObservers();
    InputMethodManager* manager = InputMethodManager::GetInstance();
    UpdateUIFromInputMethod(manager->GetCurrentInputMethod(),
                            manager->GetNumActiveInputMethods());
  }
}

void InputMethodMenu::SetMinimumWidth(int width) {
  // On the OOBE network selection screen, fixed width menu would be preferable.
  minimum_input_method_menu_width_ = width;
}

void InputMethodMenu::AddObservers() {
  if (initialized_observers_)
    return;
  InputMethodManager* manager = InputMethodManager::GetInstance();
  if (StatusAreaViewChromeos::IsLoginMode()) {
    manager->AddPreLoginPreferenceObserver(this);
  } else if (StatusAreaViewChromeos::IsBrowserMode()) {
    manager->AddPostLoginPreferenceObserver(this);
  }
  // AddObserver() should be called after AddXXXLoginPreferenceObserver. This is
  // because when the function is called FirstObserverIsAdded might be called
  // back, and FirstObserverIsAdded might then might call ChangeInputMethod() in
  // InputMethodManager. We have to prevent the manager function from calling
  // callback functions like InputMethodChanged since they touch (yet
  // uninitialized) UI elements.
  manager->AddObserver(this);
  initialized_observers_ = true;
}

void InputMethodMenu::RemoveObservers() {
  InputMethodManager* manager = InputMethodManager::GetInstance();
  if (StatusAreaViewChromeos::IsLoginMode()) {
    manager->RemovePreLoginPreferenceObserver(this);
  } else if (StatusAreaViewChromeos::IsBrowserMode()) {
    manager->RemovePostLoginPreferenceObserver(this);
  }
  manager->RemoveObserver(this);
  initialized_observers_ = false;
}

void InputMethodMenu::InitializePrefMembers() {
  if (!initialized_prefs_) {
    PrefService* pref_service = GetPrefService();
    if (pref_service) {
      initialized_prefs_ = true;
      previous_input_method_pref_.Init(
          prefs::kLanguagePreviousInputMethod, pref_service, this);
      current_input_method_pref_.Init(
          prefs::kLanguageCurrentInputMethod, pref_service, this);
    }
  }
}

}  // namespace chromeos
