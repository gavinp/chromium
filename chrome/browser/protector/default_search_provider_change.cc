// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/metrics/histogram.h"
#include "chrome/browser/protector/base_setting_change.h"
#include "chrome/browser/protector/histograms.h"
#include "chrome/browser/protector/protector.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_prepopulate_data.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_observer.h"
#include "chrome/browser/webdata/keyword_table.h"
#include "chrome/common/url_constants.h"
#include "googleurl/src/gurl.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace protector {

namespace {

// Maximum length of the search engine name to be displayed.
const size_t kMaxDisplayedNameLength = 10;

// Predicate that matches a TemplateURL with given ID.
class TemplateURLHasId {
 public:
  explicit TemplateURLHasId(TemplateURLID id) : id_(id) {
  }

  bool operator()(const TemplateURL* url) {
    return url->id() == id_;
  }

 private:
  TemplateURLID id_;
};

// Matches TemplateURL with all fields set from the prepopulated data equal
// to fields in another TemplateURL.
class TemplateURLIsSame {
 public:
  // Creates a matcher based on |other|.
  explicit TemplateURLIsSame(const TemplateURL* other) : other_(other) {
  }

  // Returns true if both |other| and |url| are NULL or have same field values.
  bool operator()(const TemplateURL* url) {
    if (url == other_ )
      return true;
    if (!url || !other_)
      return false;
    return url->short_name() == other_->short_name() &&
        AreKeywordsSame(url, other_) &&
        TemplateURLRef::SameUrlRefs(url->url(), other_->url()) &&
        TemplateURLRef::SameUrlRefs(url->suggestions_url(),
                                    other_->suggestions_url()) &&
        TemplateURLRef::SameUrlRefs(url->instant_url(),
                                    other_->instant_url()) &&
        url->GetFaviconURL() == other_->GetFaviconURL() &&
        url->safe_for_autoreplace() == other_->safe_for_autoreplace() &&
        url->show_in_default_list() == other_->show_in_default_list() &&
        url->input_encodings() == other_->input_encodings() &&
        url->logo_id() == other_->logo_id() &&
        url->prepopulate_id() == other_->prepopulate_id();
  }

 private:
  // Returns true if both |url1| and |url2| have autogenerated keywords
  // or if their keywords are identical.
  bool AreKeywordsSame(const TemplateURL* url1, const TemplateURL* url2) {
    return (url1->autogenerate_keyword() && url2->autogenerate_keyword()) ||
        url1->keyword() == url2->keyword();
  }

  const TemplateURL* other_;
};

}  // namespace

class DefaultSearchProviderChange : public BaseSettingChange,
                                    public TemplateURLServiceObserver {
 public:
  DefaultSearchProviderChange(const TemplateURL* old_url,
                              const TemplateURL* new_url);

  // BaseSettingChange overrides:
  virtual bool Init(Protector* protector) OVERRIDE;
  virtual void Apply() OVERRIDE;
  virtual void Discard() OVERRIDE;
  virtual void Timeout() OVERRIDE;
  virtual void OnBeforeRemoved() OVERRIDE;
  virtual int GetBadgeIconID() const OVERRIDE;
  virtual int GetMenuItemIconID() const OVERRIDE;
  virtual int GetBubbleIconID() const OVERRIDE;
  virtual string16 GetBubbleTitle() const OVERRIDE;
  virtual string16 GetBubbleMessage() const OVERRIDE;
  virtual string16 GetApplyButtonText() const OVERRIDE;
  virtual string16 GetDiscardButtonText() const OVERRIDE;

  // TemplateURLServiceObserver overrides:
  virtual void OnTemplateURLServiceChanged() OVERRIDE;

 private:
  virtual ~DefaultSearchProviderChange();

  // Sets the given default search provider to profile that this change is
  // related to. Returns the |TemplateURL| instance of the new default search
  // provider. If no search provider with |id| exists and |allow_fallback| is
  // true, sets one of the prepopulated search providers.
  const TemplateURL* SetDefaultSearchProvider(int64 id,
                                              bool allow_fallback);

  // Opens the Search engine settings page in a new tab.
  void OpenSearchEngineSettings();

  int64 old_id_;
  int64 new_id_;
  // ID of the search engine that we fall back to if the backup is lost.
  int64 fallback_id_;
  string16 old_name_;
  string16 new_name_;
  // Name of the search engine that we fall back to if the backup is lost.
  string16 fallback_name_;
  // Histogram ID of the new search provider.
  int new_histogram_id_;
  // Default search provider set by |Init| for the period until user makes a
  // choice and either |Apply| or |Discard| is performed. Should only be used
  // for comparison with the current default search provider and never
  // dereferenced other than in |Init| because it may be deallocated by
  // TemplateURLService at any time.
  const TemplateURL* default_search_provider_;

  DISALLOW_COPY_AND_ASSIGN(DefaultSearchProviderChange);
};

DefaultSearchProviderChange::DefaultSearchProviderChange(
    const TemplateURL* old_url,
    const TemplateURL* new_url)
    : old_id_(0),
      new_id_(0),
      fallback_id_(0),
      new_histogram_id_(GetSearchProviderHistogramID(new_url)),
      default_search_provider_(NULL) {
  if (new_url) {
    new_id_ = new_url->id();
    new_name_ = new_url->short_name();
  }
  if (old_url) {
    old_id_ = old_url->id();
    old_name_ = old_url->short_name();
  }
}

DefaultSearchProviderChange::~DefaultSearchProviderChange() {
}

bool DefaultSearchProviderChange::Init(Protector* protector) {
  BaseSettingChange::Init(protector);

  UMA_HISTOGRAM_ENUMERATION(
      kProtectorHistogramNewSearchProvider,
      new_histogram_id_,
      kProtectorMaxSearchProviderID);

  // Initially reset the search engine to its previous setting.
  default_search_provider_ = SetDefaultSearchProvider(old_id_, true);
  if (!default_search_provider_)
    return false;

  int restored_histogram_id =
      GetSearchProviderHistogramID(default_search_provider_);
  UMA_HISTOGRAM_ENUMERATION(
      kProtectorHistogramSearchProviderRestored,
      restored_histogram_id,
      kProtectorMaxSearchProviderID);

  if (!old_id_ || default_search_provider_->id() != old_id_) {
    // Old settings is lost or invalid, so we had to fall back to one of the
    // prepopulated search engines.
    fallback_id_ = default_search_provider_->id();
    fallback_name_ = default_search_provider_->short_name();

    VLOG(1) << "Fallback to search provider: " << fallback_name_;
    UMA_HISTOGRAM_ENUMERATION(
        kProtectorHistogramSearchProviderFallback,
        restored_histogram_id,
        kProtectorMaxSearchProviderID);
  }

  protector->GetTemplateURLService()->AddObserver(this);

  return true;
}

void DefaultSearchProviderChange::Apply() {
  UMA_HISTOGRAM_ENUMERATION(
      kProtectorHistogramSearchProviderApplied,
      new_histogram_id_,
      kProtectorMaxSearchProviderID);

  protector()->GetTemplateURLService()->RemoveObserver(this);
  if (!new_id_) {
    // Open settings page in case the new setting is invalid.
    OpenSearchEngineSettings();
  } else {
    SetDefaultSearchProvider(new_id_, false);
  }
}

void DefaultSearchProviderChange::Discard() {
  UMA_HISTOGRAM_ENUMERATION(
      kProtectorHistogramSearchProviderDiscarded,
      new_histogram_id_,
      kProtectorMaxSearchProviderID);

  protector()->GetTemplateURLService()->RemoveObserver(this);
  if (!old_id_) {
    // Open settings page in case the old setting is invalid.
    OpenSearchEngineSettings();
  }
  // Nothing to do otherwise since we have already set the search engine
  // to |old_id_| in |Init|.
}

void DefaultSearchProviderChange::Timeout() {
  UMA_HISTOGRAM_ENUMERATION(
      kProtectorHistogramSearchProviderTimeout,
      new_histogram_id_,
      kProtectorMaxSearchProviderID);
}

void DefaultSearchProviderChange::OnBeforeRemoved() {
  protector()->GetTemplateURLService()->RemoveObserver(this);
}

int DefaultSearchProviderChange::GetBadgeIconID() const {
  return IDR_SEARCH_ENGINE_CHANGE_BADGE;
}

int DefaultSearchProviderChange::GetMenuItemIconID() const {
  return IDR_SEARCH_ENGINE_CHANGE_MENU;
}

int DefaultSearchProviderChange::GetBubbleIconID() const {
  return IDR_SEARCH_ENGINE_CHANGE_ALERT;
}

string16 DefaultSearchProviderChange::GetBubbleTitle() const {
  return l10n_util::GetStringUTF16(IDS_SEARCH_ENGINE_CHANGE_TITLE);
}

string16 DefaultSearchProviderChange::GetBubbleMessage() const {
  if (fallback_name_.empty())
    return l10n_util::GetStringUTF16(IDS_SEARCH_ENGINE_CHANGE_MESSAGE);
  else
    return l10n_util::GetStringFUTF16(
        IDS_SEARCH_ENGINE_CHANGE_NO_BACKUP_MESSAGE, fallback_name_);
}

string16 DefaultSearchProviderChange::GetApplyButtonText() const {
  if (new_id_) {
    if (new_id_ == fallback_id_) {
      // Old search engine is lost, the fallback search engine is the same as
      // the new one so no need to show this button.
      return string16();
    }
    if (new_name_.length() > kMaxDisplayedNameLength)
      return l10n_util::GetStringUTF16(IDS_CHANGE_SEARCH_ENGINE_NO_NAME);
    else
      return l10n_util::GetStringFUTF16(IDS_CHANGE_SEARCH_ENGINE, new_name_);
  } else if (old_id_) {
    // New setting is lost, offer to go to settings.
    return l10n_util::GetStringUTF16(IDS_SELECT_SEARCH_ENGINE);
  } else {
    // Both settings are lost: don't show this button.
    return string16();
  }
}

string16 DefaultSearchProviderChange::GetDiscardButtonText() const {
  if (old_id_) {
    if (new_name_.length() > kMaxDisplayedNameLength)
      return l10n_util::GetStringUTF16(IDS_KEEP_SETTING);
    else
      return l10n_util::GetStringFUTF16(IDS_KEEP_SEARCH_ENGINE, old_name_);
  } else {
    // Old setting is lost, offer to go to settings.
    return l10n_util::GetStringUTF16(IDS_SELECT_SEARCH_ENGINE);
  }
}

void DefaultSearchProviderChange::OnTemplateURLServiceChanged() {
  TemplateURLService* url_service = protector()->GetTemplateURLService();
  if (url_service->GetDefaultSearchProvider() != default_search_provider_) {
    VLOG(1) << "Default search provider has been changed by user";
    default_search_provider_ = NULL;
    url_service->RemoveObserver(this);
    // This will delete the Protector instance and |this|.
    protector()->DismissChange();
  }
}

const TemplateURL* DefaultSearchProviderChange::SetDefaultSearchProvider(
    int64 id,
    bool allow_fallback) {
  TemplateURLService* url_service = protector()->GetTemplateURLService();
  if (!url_service) {
    NOTREACHED() << "Can't get TemplateURLService object.";
    return NULL;
  }

  TemplateURLService::TemplateURLVector urls = url_service->GetTemplateURLs();
  const TemplateURL* url = NULL;
  if (id) {
    TemplateURLService::TemplateURLVector::const_iterator i =
        find_if(urls.begin(), urls.end(), TemplateURLHasId(id));
    if (i != urls.end())
      url = *i;
  }
  if (!url && allow_fallback) {
    // Fallback to the prepopulated default search provider.
    scoped_ptr<TemplateURL> new_url(
        TemplateURLPrepopulateData::GetPrepopulatedDefaultSearch(
            NULL  // Ignore any overrides in prefs.
        ));
    DCHECK(new_url.get());
    VLOG(1) << "Prepopulated search provider: " << new_url->short_name();

    // Check if this provider already exists and add it otherwise.
    TemplateURLService::TemplateURLVector::const_iterator i =
        find_if(urls.begin(), urls.end(), TemplateURLIsSame(new_url.get()));
    if (i != urls.end()) {
      VLOG(1) << "Provider already exists";
      url = *i;
    } else {
      VLOG(1) << "No match, adding new provider";
      url = new_url.get();
      url_service->Add(new_url.release());
      UMA_HISTOGRAM_ENUMERATION(
          kProtectorHistogramSearchProviderMissing,
          GetSearchProviderHistogramID(url),
          kProtectorMaxSearchProviderID);
    }
    // TODO(ivankr): handle keyword conflicts with existing providers.
  }

  if (url) {
    VLOG(1) << "Default search provider set to: " << url->short_name();
    url_service->SetDefaultSearchProvider(url);
  }
  return url;
}

void DefaultSearchProviderChange::OpenSearchEngineSettings() {
  protector()->OpenTab(
      GURL(std::string(chrome::kChromeUISettingsURL) +
           chrome::kSearchEnginesSubPage));
}

BaseSettingChange* CreateDefaultSearchProviderChange(
    const TemplateURL* actual,
    const TemplateURL* backup) {
  return new DefaultSearchProviderChange(backup, actual);
}

}  // namespace protector
