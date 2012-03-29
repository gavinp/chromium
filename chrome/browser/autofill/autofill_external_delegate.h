// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AUTOFILL_AUTOFILL_EXTERNAL_DELEGATE_H_
#define CHROME_BROWSER_AUTOFILL_AUTOFILL_EXTERNAL_DELEGATE_H_
#pragma once

#include <vector>

#include "base/compiler_specific.h"
#include "base/string16.h"
#include "webkit/forms/form_data.h"
#include "webkit/forms/form_field.h"

class AutofillManager;
class TabContentsWrapper;

namespace gfx {
class Rect;
}

// TODO(csharp): A lot of the logic in this class is copied from autofillagent.
// Once Autofill is moved out of WebKit this class should be the only home for
// this logic. See http://crbug.com/51644

// Delegate for external processing of Autocomplete and Autofill
// display and selection.
class AutofillExternalDelegate {
 public:
  virtual ~AutofillExternalDelegate();

  // When using an external Autofill delegate.  Allows Chrome to tell
  // WebKit which Autofill selection has been chosen.
  // TODO(jrg): add feedback mechanism for hover on relevant platforms.
  virtual void SelectAutofillSuggestionAtIndex(int unique_id, int list_index);

  // Records and associates a query_id with web form data.  Called
  // when the renderer posts an Autofill query to the browser. |bounds|
  // is window relative. |display_warning_if_disabled| tells us if we should
  // display warnings (such as autofill is disabled, but had suggestions).
  // We might not want to display the warning if a website has disabled
  // Autocomplete because they have their own popup, and showing our popup
  // on to of theirs would be a poor user experience.
  virtual void OnQuery(int query_id,
                       const webkit::forms::FormData& form,
                       const webkit::forms::FormField& field,
                       const gfx::Rect& bounds,
                       bool display_warning_if_disabled);

  // Records query results and correctly formats them before sending them off
  // to be displayed.  Called when an Autofill query result is available.
  virtual void OnSuggestionsReturned(
      int query_id,
      const std::vector<string16>& autofill_values,
      const std::vector<string16>& autofill_labels,
      const std::vector<string16>& autofill_icons,
      const std::vector<int>& autofill_unique_ids);

  // Inform the delegate that the text field editing has ended, this is
  // used to help record the metrics of when a new popup is shown.
  void DidEndTextFieldEditing();

  // Inform the delegate that an autofill suggestion have been chosen. Returns
  // true if the suggestion was selected.
  bool DidAcceptAutofillSuggestions(const string16& value,
                                    int unique_id,
                                    unsigned index);

  // Informs the delegate that the Autofill previewed form should be cleared.
  virtual void ClearPreviewedForm();

  // Hide the Autofill poup.
  virtual void HideAutofillPopup();

  // Platforms that wish to implement an external Autofill delegate
  // MUST implement this.  The 1st arg is the tab contents that owns
  // this delegate; the second is the Autofill manager owned by the
  // tab contents.
  static AutofillExternalDelegate* Create(TabContentsWrapper*,
                                          AutofillManager*);
 protected:
  explicit AutofillExternalDelegate(TabContentsWrapper* tab_contents_wrapper,
                                    AutofillManager* autofill_manager);

  // Displays the the Autofill results to the user with an external
  // Autofill popup that lives completely in the browser.  The suggestions
  // have be correctly formatted by this point.
  virtual void ApplyAutofillSuggestions(
      const std::vector<string16>& autofill_values,
      const std::vector<string16>& autofill_labels,
      const std::vector<string16>& autofill_icons,
      const std::vector<int>& autofill_unique_ids,
      int separator_index) = 0;

  // Handle instance specific OnQueryCode.
  virtual void OnQueryPlatformSpecific(int query_id,
                                       const webkit::forms::FormData& form,
                                       const webkit::forms::FormField& field,
                                       const gfx::Rect& bounds) = 0;

  // Handle platform-dependent hiding.
  virtual void HideAutofillPopupInternal() = 0;

 private:
  // Fills the form with the Autofill data corresponding to |unique_id|.
  // If |is_preview| is true then this is just a preview to show the user what
  // would be selected and if |is_preview| is false then the user has selected
  // this data.
  void FillAutofillFormData(int unique_id, bool is_preview);

  TabContentsWrapper* tab_contents_wrapper_;  // weak; owns me.
  AutofillManager* autofill_manager_;  // weak.

  // The ID of the last request sent for form field Autofill.  Used to ignore
  // out of date responses.
  int autofill_query_id_;

  // The current form and field selected by Autofill.
  webkit::forms::FormData autofill_query_form_;
  webkit::forms::FormField autofill_query_field_;

  // Should we display a warning if Autofill is disabled?
  bool display_warning_if_disabled_;

  // Have we already shown Autofill suggestions for the field the user is
  // currently editing?  Used to keep track of state for metrics logging.
  bool has_shown_autofill_popup_for_current_edit_;

  // The menu index of the "Clear" menu item.
  int suggestions_clear_index_;

  // The menu index of the "Autofill options..." menu item.
  int suggestions_options_index_;

  DISALLOW_COPY_AND_ASSIGN(AutofillExternalDelegate);
};

#endif  // CHROME_BROWSER_AUTOFILL_AUTOFILL_EXTERNAL_DELEGATE_H_
