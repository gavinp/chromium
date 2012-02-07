// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/translate/translate_manager.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/json/json_reader.h"
#include "base/memory/singleton.h"
#include "base/message_loop.h"
#include "base/metrics/histogram.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/infobars/infobar_tab_helper.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_contents/language_state.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "chrome/browser/tabs/tab_strip_model.h"
#include "chrome/browser/translate/page_translated_details.h"
#include "chrome/browser/translate/translate_infobar_delegate.h"
#include "chrome/browser/translate/translate_prefs.h"
#include "chrome/browser/translate/translate_tab_helper.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/common/chrome_notification_types.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/translate_errors.h"
#include "chrome/common/url_constants.h"
#include "content/browser/renderer_host/render_view_host.h"
#include "content/browser/tab_contents/navigation_details.h"
#include "content/browser/tab_contents/navigation_entry.h"
#include "content/browser/tab_contents/tab_contents.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/common/url_fetcher.h"
#include "grit/browser_resources.h"
#include "net/base/escape.h"
#include "net/url_request/url_request_status.h"
#include "ui/base/resource/resource_bundle.h"

namespace {

// The list of languages the Google translation server supports.
// For information, here is the list of languages that Chrome can be run in
// but that the translation server does not support:
// am Amharic
// bn Bengali
// gu Gujarati
// kn Kannada
// ml Malayalam
// mr Marathi
// ta Tamil
// te Telugu
const char* const kSupportedLanguages[] = {
    "af",     // Afrikaans
    "az",     // Azerbaijani
    "sq",     // Albanian
    "ar",     // Arabic
    "hy",     // Armenian
    "eu",     // Basque
    "be",     // Belarusian
    "bg",     // Bulgarian
    "ca",     // Catalan
    "zh-CN",  // Chinese (Simplified)
    "zh-TW",  // Chinese (Traditional)
    "hr",     // Croatian
    "cs",     // Czech
    "da",     // Danish
    "nl",     // Dutch
    "en",     // English
    "et",     // Estonian
    "fi",     // Finnish
    "fil",    // Filipino
    "fr",     // French
    "gl",     // Galician
    "de",     // German
    "el",     // Greek
    "ht",     // Haitian Creole
    "he",     // Hebrew
    "hi",     // Hindi
    "hu",     // Hungarian
    "is",     // Icelandic
    "id",     // Indonesian
    "it",     // Italian
    "ga",     // Irish
    "ja",     // Japanese
    "ka",     // Georgian
    "ko",     // Korean
    "lv",     // Latvian
    "lt",     // Lithuanian
    "mk",     // Macedonian
    "ms",     // Malay
    "mt",     // Maltese
    "nb",     // Norwegian
    "fa",     // Persian
    "pl",     // Polish
    "pt",     // Portuguese
    "ro",     // Romanian
    "ru",     // Russian
    "sr",     // Serbian
    "sk",     // Slovak
    "sl",     // Slovenian
    "es",     // Spanish
    "sw",     // Swahili
    "sv",     // Swedish
    "th",     // Thai
    "tr",     // Turkish
    "uk",     // Ukrainian
    "ur",     // Urdu
    "vi",     // Vietnamese
    "cy",     // Welsh
    "yi",     // Yiddish
};

const char* const kTranslateScriptURL =
    "https://translate.google.com/translate_a/element.js?"
    "cb=cr.googleTranslate.onTranslateElementLoad";
const char* const kTranslateScriptHeader =
    "Google-Translate-Element-Mode: library";
const char* const kReportLanguageDetectionErrorURL =
    // TODO(palmer): bug 112236. Make this https://.
    "http://translate.google.com/translate_error";
const char* const kLanguageListFetchURL =
    "https://translate.googleapis.com/translate_a/l?client=chrome&cb=sl";
const int kMaxRetryLanguageListFetch = 5;
const int kTranslateScriptExpirationDelayMS = 24 * 60 * 60 * 1000;  // 1 day.

}  // namespace

// This must be kept in sync with the &cb= value in the kLanguageListFetchURL.
const char* const TranslateManager::kLanguageListCallbackName = "sl(";
const char* const TranslateManager::kTargetLanguagesKey = "tl";

// static
base::LazyInstance<std::set<std::string> >
    TranslateManager::supported_languages_ = LAZY_INSTANCE_INITIALIZER;

TranslateManager::~TranslateManager() {
  weak_method_factory_.InvalidateWeakPtrs();
}

// static
TranslateManager* TranslateManager::GetInstance() {
  return Singleton<TranslateManager>::get();
}

// static
bool TranslateManager::IsTranslatableURL(const GURL& url) {
  // A URLs is translatable unless it is one of the following:
  // - an internal URL (chrome:// and others)
  // - the devtools (which is considered UI)
  // - an FTP page (as FTP pages tend to have long lists of filenames that may
  //   confuse the CLD)
  return !url.SchemeIs(chrome::kChromeUIScheme) &&
         !url.SchemeIs(chrome::kChromeDevToolsScheme) &&
         !url.SchemeIs(chrome::kFtpScheme);
}

// static
void TranslateManager::SetSupportedLanguages(const std::string& language_list) {
  // The format is:
  // sl({'sl': {'XX': 'LanguageName', ...}, 'tl': {'XX': 'LanguageName', ...}})
  // Where "sl(" is set in kLanguageListCallbackName
  // and 'tl' is kTargetLanguagesKey
  if (!StartsWithASCII(language_list, kLanguageListCallbackName, false) ||
      !EndsWith(language_list, ")", false)) {
    // We don't have a NOTREACHED here since this can happen in ui_tests, even
    // though the the BrowserMain function won't call us with parameters.ui_task
    // is NULL some tests don't set it, so we must bail here.
    return;
  }
  static const size_t kLanguageListCallbackNameLength =
      strlen(kLanguageListCallbackName);
  std::string languages_json = language_list.substr(
      kLanguageListCallbackNameLength,
      language_list.size() - kLanguageListCallbackNameLength - 1);
  // JSON doesn't support single quotes though this is what is used on the
  // translate server so we must replace them with double quotes.
  ReplaceSubstringsAfterOffset(&languages_json, 0, "'", "\"");
  scoped_ptr<Value> json_value(base::JSONReader::Read(languages_json, true));
  if (json_value == NULL || !json_value->IsType(Value::TYPE_DICTIONARY)) {
    NOTREACHED();
    return;
  }
  // The first level dictionary contains two sub-dict, one for source languages
  // and the other for target languages, we want to use the target languages.
  DictionaryValue* language_dict =
      static_cast<DictionaryValue*>(json_value.get());
  DictionaryValue* target_languages = NULL;
  if (!language_dict->GetDictionary(kTargetLanguagesKey, &target_languages) ||
      target_languages == NULL) {
    NOTREACHED();
    return;
  }
  // Now we can clear our current state...
  std::set<std::string>* supported_languages = supported_languages_.Pointer();
  supported_languages->clear();
  // ... and replace it with the values we just fetched from the server.
  DictionaryValue::key_iterator iter = target_languages->begin_keys();
  for (; iter != target_languages->end_keys(); ++iter)
    supported_languages_.Pointer()->insert(*iter);
}

// static
void TranslateManager::InitSupportedLanguages() {
  // If our list of supported languages have not been set yet, we default
  // to our hard coded list of languages in kSupportedLanguages.
  if (supported_languages_.Pointer()->empty()) {
    for (size_t i = 0; i < arraysize(kSupportedLanguages); ++i)
      supported_languages_.Pointer()->insert(kSupportedLanguages[i]);
  }
}

// static
void TranslateManager::GetSupportedLanguages(
    std::vector<std::string>* languages) {
  DCHECK(languages && languages->empty());
  InitSupportedLanguages();
  std::set<std::string>* supported_languages = supported_languages_.Pointer();
  std::set<std::string>::const_iterator iter = supported_languages->begin();
  for (; iter != supported_languages->end(); ++iter)
    languages->push_back(*iter);
}

// static
std::string TranslateManager::GetLanguageCode(
    const std::string& chrome_locale) {
  // Only remove the country code for country specific languages we don't
  // support specifically yet.
  if (IsSupportedLanguage(chrome_locale))
    return chrome_locale;

  size_t hypen_index = chrome_locale.find('-');
  if (hypen_index == std::string::npos)
    return chrome_locale;
  return chrome_locale.substr(0, hypen_index);
}

// static
bool TranslateManager::IsSupportedLanguage(const std::string& page_language) {
  InitSupportedLanguages();
  return supported_languages_.Pointer()->count(page_language) != 0;
}

void TranslateManager::Observe(int type,
                               const content::NotificationSource& source,
                               const content::NotificationDetails& details) {
  switch (type) {
    case content::NOTIFICATION_NAV_ENTRY_COMMITTED: {
      NavigationController* controller =
          content::Source<NavigationController>(source).ptr();
      content::LoadCommittedDetails* load_details =
          content::Details<content::LoadCommittedDetails>(details).ptr();
      NavigationEntry* entry = controller->GetActiveEntry();
      if (!entry) {
        NOTREACHED();
        return;
      }

      TabContentsWrapper* wrapper =
          TabContentsWrapper::GetCurrentWrapperForContents(
              controller->tab_contents());
      if (!wrapper || !wrapper->translate_tab_helper())
        return;

      TranslateTabHelper* helper = wrapper->translate_tab_helper();
      if (!load_details->is_main_frame &&
          helper->language_state().translation_declined()) {
        // Some sites (such as Google map) may trigger sub-frame navigations
        // when the user interacts with the page.  We don't want to show a new
        // infobar if the user already dismissed one in that case.
        return;
      }
      if (entry->transition_type() != content::PAGE_TRANSITION_RELOAD &&
          load_details->type != content::NAVIGATION_TYPE_SAME_PAGE) {
        return;
      }
      // When doing a page reload, we don't get a TAB_LANGUAGE_DETERMINED
      // notification.  So we need to explictly initiate the translation.
      // Note that we delay it as the TranslateManager gets this notification
      // before the TabContents and the TabContents processing might remove the
      // current infobars.  Since InitTranslation might add an infobar, it must
      // be done after that.
      MessageLoop::current()->PostTask(FROM_HERE,
          base::Bind(
              &TranslateManager::InitiateTranslationPosted,
              weak_method_factory_.GetWeakPtr(),
              controller->tab_contents()->render_view_host()->process()->
                  GetID(),
              controller->tab_contents()->render_view_host()->routing_id(),
              helper->language_state().original_language()));
      break;
    }
    case chrome::NOTIFICATION_TAB_LANGUAGE_DETERMINED: {
      TabContents* tab = content::Source<TabContents>(source).ptr();
      // We may get this notifications multiple times.  Make sure to translate
      // only once.
      TabContentsWrapper* wrapper =
          TabContentsWrapper::GetCurrentWrapperForContents(tab);
      if (!wrapper || !wrapper->translate_tab_helper())
        return;

      LanguageState& language_state =
          wrapper->translate_tab_helper()->language_state();
      if (language_state.page_translatable() &&
          !language_state.translation_pending() &&
          !language_state.translation_declined() &&
          !language_state.IsPageTranslated()) {
        std::string language = *(content::Details<std::string>(details).ptr());
        InitiateTranslation(tab, language);
      }
      break;
    }
    case chrome::NOTIFICATION_PAGE_TRANSLATED: {
      // Only add translate infobar if it doesn't exist; if it already exists,
      // just update the state, the actual infobar would have received the same
      //  notification and update the visual display accordingly.
      TabContents* tab = content::Source<TabContents>(source).ptr();
      PageTranslatedDetails* page_translated_details =
          content::Details<PageTranslatedDetails>(details).ptr();
      PageTranslated(tab, page_translated_details);
      break;
    }
    case chrome::NOTIFICATION_PROFILE_DESTROYED: {
      PrefService* pref_service =
          content::Source<Profile>(source).ptr()->GetPrefs();
      notification_registrar_.Remove(this,
                                     chrome::NOTIFICATION_PROFILE_DESTROYED,
                                     source);
      size_t count = accept_languages_.erase(pref_service);
      // We should know about this profile since we are listening for
      // notifications on it.
      DCHECK(count == 1u);
      PrefChangeRegistrar* pref_change_registrar =
          pref_change_registrars_[pref_service];
      count = pref_change_registrars_.erase(pref_service);
      DCHECK(count == 1u);
      delete pref_change_registrar;
      break;
    }
    case chrome::NOTIFICATION_PREF_CHANGED: {
      DCHECK(*content::Details<std::string>(details).ptr() ==
             prefs::kAcceptLanguages);
      PrefService* prefs = content::Source<PrefService>(source).ptr();
      InitAcceptLanguages(prefs);
      break;
    }
    default:
      NOTREACHED();
  }
}

void TranslateManager::OnURLFetchComplete(const content::URLFetcher* source) {
  if (translate_script_request_pending_.get() != source &&
      language_list_request_pending_.get() != source) {
    // Looks like crash on Mac is possibly caused with callback entering here
    // with unknown fetcher when network is refreshed.
    scoped_ptr<const content::URLFetcher> delete_ptr(source);
    return;
  }

  bool error =
      (source->GetStatus().status() != net::URLRequestStatus::SUCCESS ||
      source->GetResponseCode() != 200);
  if (translate_script_request_pending_.get() == source) {
    scoped_ptr<const content::URLFetcher> delete_ptr(
        translate_script_request_pending_.release());
    if (!error) {
      base::StringPiece str = ResourceBundle::GetSharedInstance().
          GetRawDataResource(IDR_TRANSLATE_JS);
      DCHECK(translate_script_.empty());
      str.CopyToString(&translate_script_);
      std::string data;
      source->GetResponseAsString(&data);
      translate_script_ += "\n" + data;
      // We'll expire the cached script after some time, to make sure long
      // running browsers still get fixes that might get pushed with newer
      // scripts.
      MessageLoop::current()->PostDelayedTask(FROM_HERE,
          base::Bind(&TranslateManager::ClearTranslateScript,
                     weak_method_factory_.GetWeakPtr()),
          translate_script_expiration_delay_);
    }
    // Process any pending requests.
    std::vector<PendingRequest>::const_iterator iter;
    for (iter = pending_requests_.begin(); iter != pending_requests_.end();
         ++iter) {
      const PendingRequest& request = *iter;
      TabContents* tab = tab_util::GetTabContentsByID(request.render_process_id,
                                                      request.render_view_id);
      if (!tab) {
        // The tab went away while we were retrieving the script.
        continue;
      }
      NavigationEntry* entry = tab->controller().GetActiveEntry();
      if (!entry || entry->page_id() != request.page_id) {
        // We navigated away from the page the translation was triggered on.
        continue;
      }

      if (error) {
        TabContentsWrapper* wrapper =
            TabContentsWrapper::GetCurrentWrapperForContents(tab);
        InfoBarTabHelper* infobar_helper = wrapper->infobar_tab_helper();
        ShowInfoBar(
            tab, TranslateInfoBarDelegate::CreateErrorDelegate(
                TranslateErrors::NETWORK,
                infobar_helper,
                wrapper->profile()->GetPrefs(),
                request.source_lang,
                request.target_lang));
      } else {
        // Translate the page.
        DoTranslatePage(tab, translate_script_,
                        request.source_lang, request.target_lang);
      }
    }
    pending_requests_.clear();
  } else {  // if (translate_script_request_pending_.get() == source)
    scoped_ptr<const content::URLFetcher> delete_ptr(
        language_list_request_pending_.release());
    if (!error) {
      std::string data;
      source->GetResponseAsString(&data);
      SetSupportedLanguages(data);
    } else {
      VLOG(1) << "Failed to Fetch languages from: " << kLanguageListFetchURL;
    }
  }
}

// static
bool TranslateManager::IsShowingTranslateInfobar(TabContents* tab) {
  return GetTranslateInfoBarDelegate(tab) != NULL;
}

TranslateManager::TranslateManager()
    : ALLOW_THIS_IN_INITIALIZER_LIST(weak_method_factory_(this)),
      translate_script_expiration_delay_(kTranslateScriptExpirationDelayMS) {
  notification_registrar_.Add(this, content::NOTIFICATION_NAV_ENTRY_COMMITTED,
                              content::NotificationService::AllSources());
  notification_registrar_.Add(this,
                              chrome::NOTIFICATION_TAB_LANGUAGE_DETERMINED,
                              content::NotificationService::AllSources());
  notification_registrar_.Add(this, chrome::NOTIFICATION_PAGE_TRANSLATED,
                              content::NotificationService::AllSources());
}

void TranslateManager::InitiateTranslation(TabContents* tab,
                                           const std::string& page_lang) {
  Profile* profile = Profile::FromBrowserContext(tab->browser_context());
  PrefService* prefs = profile->GetOriginalProfile()->GetPrefs();
  if (!prefs->GetBoolean(prefs::kEnableTranslate))
    return;

  // Allow disabling of translate from the command line to assist with
  // automated browser testing.
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kDisableTranslate))
    return;

  NavigationEntry* entry = tab->controller().GetActiveEntry();
  if (!entry) {
    // This can happen for popups created with window.open("").
    return;
  }

  // If there is already a translate infobar showing, don't show another one.
  if (GetTranslateInfoBarDelegate(tab))
    return;

  std::string target_lang = GetTargetLanguage(prefs);
  std::string language_code = GetLanguageCode(page_lang);
  // Nothing to do if either the language Chrome is in or the language of the
  // page is not supported by the translation server.
  if (target_lang.empty() || !IsSupportedLanguage(language_code)) {
    return;
  }

  // We don't want to translate:
  // - any Chrome specific page (New Tab Page, Download, History... pages).
  // - similar languages (ex: en-US to en).
  // - any user black-listed URLs or user selected language combination.
  // - any language the user configured as accepted languages.
  if (!IsTranslatableURL(entry->url()) || language_code == target_lang ||
      !TranslatePrefs::CanTranslate(prefs, language_code, entry->url()) ||
      IsAcceptLanguage(tab, language_code)) {
    return;
  }

  // If the user has previously selected "always translate" for this language we
  // automatically translate.  Note that in incognito mode we disable that
  // feature; the user will get an infobar, so they can control whether the
  // page's text is sent to the translate server.
  std::string auto_target_lang;
  if (!tab->browser_context()->IsOffTheRecord() &&
      TranslatePrefs::ShouldAutoTranslate(prefs, language_code,
          &auto_target_lang)) {
    // We need to confirm that the saved target language is still supported.
    // Also, GetLanguageCode will take care of removing country code if any.
    auto_target_lang = GetLanguageCode(auto_target_lang);
    if (IsSupportedLanguage(auto_target_lang)) {
      TranslatePage(tab, language_code, auto_target_lang);
      return;
    }
  }

  TabContentsWrapper* wrapper =
      TabContentsWrapper::GetCurrentWrapperForContents(tab);
  if (!wrapper || !wrapper->translate_tab_helper())
    return;

  TranslateTabHelper* helper = wrapper->translate_tab_helper();
  std::string auto_translate_to = helper->language_state().AutoTranslateTo();
  if (!auto_translate_to.empty()) {
    // This page was navigated through a click from a translated page.
    TranslatePage(tab, language_code, auto_translate_to);
    return;
  }

  InfoBarTabHelper* infobar_helper = wrapper->infobar_tab_helper();
  // Prompts the user if he/she wants the page translated.
  infobar_helper->AddInfoBar(
      TranslateInfoBarDelegate::CreateDelegate(
          TranslateInfoBarDelegate::BEFORE_TRANSLATE, infobar_helper,
          wrapper->profile()->GetPrefs(), language_code, target_lang));
}

void TranslateManager::InitiateTranslationPosted(
    int process_id, int render_id, const std::string& page_lang) {
  // The tab might have been closed.
  TabContents* tab = tab_util::GetTabContentsByID(process_id, render_id);
  if (!tab)
    return;

  TranslateTabHelper* helper = TabContentsWrapper::GetCurrentWrapperForContents(
      tab)->translate_tab_helper();
  if (helper->language_state().translation_pending())
    return;

  InitiateTranslation(tab, GetLanguageCode(page_lang));
}

void TranslateManager::TranslatePage(TabContents* tab_contents,
                                     const std::string& source_lang,
                                     const std::string& target_lang) {
  NavigationEntry* entry = tab_contents->controller().GetActiveEntry();
  if (!entry) {
    NOTREACHED();
    return;
  }

  TabContentsWrapper* wrapper =
      TabContentsWrapper::GetCurrentWrapperForContents(tab_contents);
  InfoBarTabHelper* infobar_helper = wrapper->infobar_tab_helper();
  ShowInfoBar(tab_contents, TranslateInfoBarDelegate::CreateDelegate(
      TranslateInfoBarDelegate::TRANSLATING, infobar_helper,
      wrapper->profile()->GetPrefs(), source_lang, target_lang));

  if (!translate_script_.empty()) {
    DoTranslatePage(tab_contents, translate_script_, source_lang, target_lang);
    return;
  }

  // The script is not available yet.  Queue that request and query for the
  // script.  Once it is downloaded we'll do the translate.
  RenderViewHost* rvh = tab_contents->render_view_host();
  PendingRequest request;
  request.render_process_id = rvh->process()->GetID();
  request.render_view_id = rvh->routing_id();
  request.page_id = entry->page_id();
  request.source_lang = source_lang;
  request.target_lang = target_lang;
  pending_requests_.push_back(request);
  RequestTranslateScript();
}

void TranslateManager::RevertTranslation(TabContents* tab_contents) {
  NavigationEntry* entry = tab_contents->controller().GetActiveEntry();
  if (!entry) {
    NOTREACHED();
    return;
  }
  tab_contents->render_view_host()->Send(new ChromeViewMsg_RevertTranslation(
      tab_contents->render_view_host()->routing_id(), entry->page_id()));

  TranslateTabHelper* helper = TabContentsWrapper::GetCurrentWrapperForContents(
      tab_contents)->translate_tab_helper();
  helper->language_state().set_current_language(
      helper->language_state().original_language());
}

void TranslateManager::ReportLanguageDetectionError(TabContents* tab_contents) {
  UMA_HISTOGRAM_COUNTS("Translate.ReportLanguageDetectionError", 1);
  GURL page_url = tab_contents->controller().GetActiveEntry()->url();
  // Report option should be disabled for secure URLs.
  DCHECK(!page_url.SchemeIsSecure());
  std::string report_error_url(kReportLanguageDetectionErrorURL);
  report_error_url += "?client=cr&action=langidc&u=";
  report_error_url += net::EscapeUrlEncodedData(page_url.spec(), true);
  report_error_url += "&sl=";

  TranslateTabHelper* helper = TabContentsWrapper::GetCurrentWrapperForContents(
      tab_contents)->translate_tab_helper();
  report_error_url += helper->language_state().original_language();
  report_error_url += "&hl=";
  report_error_url +=
      GetLanguageCode(g_browser_process->GetApplicationLocale());
  // Open that URL in a new tab so that the user can tell us more.
  Profile* profile =
      Profile::FromBrowserContext(tab_contents->browser_context());
  Browser* browser = BrowserList::GetLastActiveWithProfile(profile);
  if (!browser) {
    NOTREACHED();
    return;
  }
  browser->AddSelectedTabWithURL(GURL(report_error_url),
                                 content::PAGE_TRANSITION_AUTO_BOOKMARK);
}

void TranslateManager::DoTranslatePage(TabContents* tab,
                                       const std::string& translate_script,
                                       const std::string& source_lang,
                                       const std::string& target_lang) {
  NavigationEntry* entry = tab->controller().GetActiveEntry();
  if (!entry) {
    NOTREACHED();
    return;
  }

  TabContentsWrapper* wrapper =
      TabContentsWrapper::GetCurrentWrapperForContents(tab);
  if (!wrapper || !wrapper->translate_tab_helper())
    return;

  wrapper->translate_tab_helper()->language_state().set_translation_pending(
      true);
  tab->render_view_host()->Send(new ChromeViewMsg_TranslatePage(
      tab->render_view_host()->routing_id(), entry->page_id(), translate_script,
      source_lang, target_lang));
}

void TranslateManager::PageTranslated(TabContents* tab,
                                      PageTranslatedDetails* details) {
  TabContentsWrapper* wrapper =
      TabContentsWrapper::GetCurrentWrapperForContents(tab);
  InfoBarTabHelper* infobar_helper = wrapper->infobar_tab_helper();
  PrefService* prefs = wrapper->profile()->GetPrefs();

  // Create the new infobar to display.
  TranslateInfoBarDelegate* infobar;
  if (details->error_type != TranslateErrors::NONE) {
    infobar = TranslateInfoBarDelegate::CreateErrorDelegate(
        details->error_type,
        infobar_helper,
        prefs,
        details->source_language,
        details->target_language);
  } else if (!IsSupportedLanguage(details->source_language)) {
    // TODO(jcivelli): http://crbug.com/9390 We should change the "after
    //                 translate" infobar to support unknown as the original
    //                 language.
    UMA_HISTOGRAM_COUNTS("Translate.ServerReportedUnsupportedLanguage", 1);
    infobar = TranslateInfoBarDelegate::CreateErrorDelegate(
        TranslateErrors::UNSUPPORTED_LANGUAGE, infobar_helper,
        prefs, details->source_language, details->target_language);
  } else {
    infobar = TranslateInfoBarDelegate::CreateDelegate(
        TranslateInfoBarDelegate::AFTER_TRANSLATE, infobar_helper,
        prefs, details->source_language, details->target_language);
  }
  ShowInfoBar(tab, infobar);
}

bool TranslateManager::IsAcceptLanguage(TabContents* tab,
                                        const std::string& language) {
  Profile* profile = Profile::FromBrowserContext(tab->browser_context());
  profile = profile->GetOriginalProfile();
  PrefService* pref_service = profile->GetPrefs();
  PrefServiceLanguagesMap::const_iterator iter =
      accept_languages_.find(pref_service);
  if (iter == accept_languages_.end()) {
    InitAcceptLanguages(pref_service);
    // Listen for this profile going away, in which case we would need to clear
    // the accepted languages for the profile.
    notification_registrar_.Add(this, chrome::NOTIFICATION_PROFILE_DESTROYED,
                                content::Source<Profile>(profile));
    // Also start listening for changes in the accept languages.
    DCHECK(pref_change_registrars_.find(pref_service) ==
           pref_change_registrars_.end());
    PrefChangeRegistrar* pref_change_registrar = new PrefChangeRegistrar;
    pref_change_registrar->Init(pref_service);
    pref_change_registrar->Add(prefs::kAcceptLanguages, this);
    pref_change_registrars_[pref_service] = pref_change_registrar;

    iter = accept_languages_.find(pref_service);
  }

  return iter->second.count(language) != 0;
}

void TranslateManager::InitAcceptLanguages(PrefService* prefs) {
  // We have been asked for this profile, build the languages.
  std::string accept_langs_str = prefs->GetString(prefs::kAcceptLanguages);
  std::vector<std::string> accept_langs_list;
  LanguageSet accept_langs_set;
  base::SplitString(accept_langs_str, ',', &accept_langs_list);
  std::vector<std::string>::const_iterator iter;
  std::string ui_lang =
      GetLanguageCode(g_browser_process->GetApplicationLocale());
  bool is_ui_english = StartsWithASCII(ui_lang, "en-", false);
  for (iter = accept_langs_list.begin();
       iter != accept_langs_list.end(); ++iter) {
    // Get rid of the locale extension if any (ex: en-US -> en), but for Chinese
    // for which the CLD reports zh-CN and zh-TW.
    std::string accept_lang(*iter);
    size_t index = iter->find("-");
    if (index != std::string::npos && *iter != "zh-CN" && *iter != "zh-TW")
      accept_lang = iter->substr(0, index);
    // Special-case English until we resolve bug 36182 properly.
    // Add English only if the UI language is not English. This will annoy
    // users of non-English Chrome who can comprehend English until English is
    // black-listed.
    // TODO(jungshik): Once we determine that it's safe to remove English from
    // the default Accept-Language values for most locales, remove this
    // special-casing.
    if (accept_lang != "en" || is_ui_english)
      accept_langs_set.insert(accept_lang);
  }
  accept_languages_[prefs] = accept_langs_set;
}

void TranslateManager::FetchLanguageListFromTranslateServer(
    PrefService* prefs) {
  if (language_list_request_pending_.get() != NULL)
    return;

  // We don't want to do this when translate is disabled.
  DCHECK(prefs != NULL);
  if (CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kDisableTranslate) ||
      (prefs != NULL && !prefs->GetBoolean(prefs::kEnableTranslate))) {
    return;
  }

  language_list_request_pending_.reset(content::URLFetcher::Create(
      1, GURL(kLanguageListFetchURL), content::URLFetcher::GET, this));
  language_list_request_pending_->SetRequestContext(
      g_browser_process->system_request_context());
  language_list_request_pending_->SetMaxRetries(kMaxRetryLanguageListFetch);
  language_list_request_pending_->Start();
}

void TranslateManager::CleanupPendingUlrFetcher() {
  language_list_request_pending_.reset();
  translate_script_request_pending_.reset();
}

void TranslateManager::RequestTranslateScript() {
  if (translate_script_request_pending_.get() != NULL)
    return;

  translate_script_request_pending_.reset(content::URLFetcher::Create(
      0, GURL(kTranslateScriptURL), content::URLFetcher::GET, this));
  translate_script_request_pending_->SetRequestContext(
      g_browser_process->system_request_context());
  translate_script_request_pending_->SetExtraRequestHeaders(
      kTranslateScriptHeader);
  translate_script_request_pending_->Start();
}

void TranslateManager::ShowInfoBar(TabContents* tab,
                                   TranslateInfoBarDelegate* infobar) {
  DCHECK(infobar != NULL);
  TranslateInfoBarDelegate* old_infobar = GetTranslateInfoBarDelegate(tab);
  infobar->UpdateBackgroundAnimation(old_infobar);
  TabContentsWrapper* wrapper =
      TabContentsWrapper::GetCurrentWrapperForContents(tab);
  if (!wrapper)
    return;
  InfoBarTabHelper* infobar_helper = wrapper->infobar_tab_helper();
  if (old_infobar) {
    // There already is a translate infobar, simply replace it.
    infobar_helper->ReplaceInfoBar(old_infobar, infobar);
  } else {
    infobar_helper->AddInfoBar(infobar);
  }
}

// static
std::string TranslateManager::GetTargetLanguage(PrefService* prefs) {
  std::string ui_lang =
    GetLanguageCode(g_browser_process->GetApplicationLocale());
  if (IsSupportedLanguage(ui_lang))
    return ui_lang;

  // Getting the accepted languages list
  std::string accept_langs_str = prefs->GetString(prefs::kAcceptLanguages);

  std::vector<std::string> accept_langs_list;
  base::SplitString(accept_langs_str, ',', &accept_langs_list);

  // Will translate to the first supported language on the Accepted Language
  // list or not at all if no such candidate exists
  std::vector<std::string>::iterator iter;
  for (iter = accept_langs_list.begin();
       iter != accept_langs_list.end(); ++iter) {
    std::string lang_code = GetLanguageCode(*iter);
    if (IsSupportedLanguage(lang_code))
      return lang_code;
  }
  return std::string();
}

// static
TranslateInfoBarDelegate* TranslateManager::GetTranslateInfoBarDelegate(
    TabContents* tab) {
  TabContentsWrapper* wrapper =
      TabContentsWrapper::GetCurrentWrapperForContents(tab);
  if (!wrapper)
    return NULL;
  InfoBarTabHelper* infobar_helper = wrapper->infobar_tab_helper();

  for (size_t i = 0; i < infobar_helper->infobar_count(); ++i) {
    TranslateInfoBarDelegate* delegate =
        infobar_helper->GetInfoBarDelegateAt(i)->AsTranslateInfoBarDelegate();
    if (delegate)
      return delegate;
  }
  return NULL;
}
