// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/message_box_view.h"

#include "base/i18n/rtl.h"
#include "base/message_loop.h"
#include "base/string_split.h"
#include "base/utf_string_conversions.h"
#include "ui/base/accessibility/accessible_view_state.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/layout/layout_constants.h"
#include "ui/views/views_delegate.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/client_view.h"

const int kDefaultMessageWidth = 320;

namespace {

// Paragraph separators are defined in
// http://www.unicode.org/Public/6.0.0/ucd/extracted/DerivedBidiClass.txt
//
// # Bidi_Class=Paragraph_Separator
//
// 000A          ; B # Cc       <control-000A>
// 000D          ; B # Cc       <control-000D>
// 001C..001E    ; B # Cc   [3] <control-001C>..<control-001E>
// 0085          ; B # Cc       <control-0085>
// 2029          ; B # Zp       PARAGRAPH SEPARATOR
bool IsParagraphSeparator(char16 c) {
  return ( c == 0x000A || c == 0x000D || c == 0x001C || c == 0x001D ||
           c == 0x001E || c == 0x0085 || c == 0x2029);
}

// Splits |str| into a vector of paragraphs. Append the results into |r|. Each
// paragraph separator is not included in the split result. If several
// paragraph separators are contiguous, or if |str| begins with or ends with
// paragraph separator, then an empty string is inserted.
void SplitStringIntoParagraphs(const string16& str,
                               std::vector<string16>* r) {
  size_t str_len = str.length();
  for (size_t i = 0, start = 0; i < str_len; ++i) {
    bool paragraph_separator = IsParagraphSeparator(str[i]);
    if (paragraph_separator || i == str_len - 1) {
      size_t len = i - start;
      if (!paragraph_separator)
        ++len;
      r->push_back(str.substr(start, len));
      start = i + 1;
    }
  }
}

}  // namespace

namespace views {

///////////////////////////////////////////////////////////////////////////////
// MessageBoxView, public:

MessageBoxView::MessageBoxView(int options,
                               const string16& message,
                               const string16& default_prompt,
                               int message_width)
    : prompt_field_(NULL),
      icon_(NULL),
      checkbox_(NULL),
      message_width_(message_width) {
  Init(options, message, default_prompt);
}

MessageBoxView::MessageBoxView(int options,
                               const string16& message,
                               const string16& default_prompt)
    : prompt_field_(NULL),
      icon_(NULL),
      checkbox_(NULL),
      message_width_(kDefaultMessageWidth) {
  Init(options, message, default_prompt);
}

MessageBoxView::~MessageBoxView() {}

string16 MessageBoxView::GetInputText() {
  return prompt_field_ ? prompt_field_->text() : string16();
}

bool MessageBoxView::IsCheckBoxSelected() {
  return checkbox_ ? checkbox_->checked() : false;
}

void MessageBoxView::SetIcon(const SkBitmap& icon) {
  if (!icon_)
    icon_ = new ImageView();
  icon_->SetImage(icon);
  icon_->SetBounds(0, 0, icon.width(), icon.height());
  ResetLayoutManager();
}

void MessageBoxView::SetCheckBoxLabel(const string16& label) {
  if (!checkbox_)
    checkbox_ = new Checkbox(label);
  else
    checkbox_->SetText(label);
  ResetLayoutManager();
}

void MessageBoxView::SetCheckBoxSelected(bool selected) {
  if (!checkbox_)
    return;
  checkbox_->SetChecked(selected);
}

void MessageBoxView::GetAccessibleState(ui::AccessibleViewState* state) {
  state->role = ui::AccessibilityTypes::ROLE_ALERT;
}

///////////////////////////////////////////////////////////////////////////////
// MessageBoxView, View overrides:

void MessageBoxView::ViewHierarchyChanged(bool is_add,
                                          View* parent,
                                          View* child) {
  if (child == this && is_add) {
    if (prompt_field_)
      prompt_field_->SelectAll();

    GetWidget()->NotifyAccessibilityEvent(
        this, ui::AccessibilityTypes::EVENT_ALERT, true);
  }
}

bool MessageBoxView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  // We only accepts Ctrl-C.
  DCHECK(accelerator.key_code() == 'C' && accelerator.IsCtrlDown());

  // We must not intercept Ctrl-C when we have a text box and it's focused.
  if (prompt_field_ && prompt_field_->HasFocus())
    return false;

  if (!ViewsDelegate::views_delegate)
    return false;

  ui::Clipboard* clipboard = ViewsDelegate::views_delegate->GetClipboard();
  if (!clipboard)
    return false;

  ui::ScopedClipboardWriter scw(clipboard, ui::Clipboard::BUFFER_STANDARD);
  string16 text = message_labels_[0]->text();
  for (size_t i = 1; i < message_labels_.size(); ++i)
    text += message_labels_[i]->text();
  scw.WriteText(text);
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// MessageBoxView, private:

void MessageBoxView::Init(int options,
                          const string16& message,
                          const string16& prompt) {
  if (options & DETECT_DIRECTIONALITY) {
    std::vector<string16> texts;
    SplitStringIntoParagraphs(message, &texts);
    // If the text originates from a web page, its alignment is based on its
    // first character with strong directionality.
    base::i18n::TextDirection message_direction =
        base::i18n::GetFirstStrongCharacterDirection(message);
    Label::Alignment alignment =
        (message_direction == base::i18n::RIGHT_TO_LEFT) ?
        Label::ALIGN_RIGHT : Label::ALIGN_LEFT;
    for (size_t i = 0; i < texts.size(); ++i) {
      Label* message_label = new Label(texts[i]);
      message_label->SetMultiLine(true);
      message_label->SetAllowCharacterBreak(true);
      message_label->set_directionality_mode(Label::AUTO_DETECT_DIRECTIONALITY);
      message_label->SetHorizontalAlignment(alignment);
      message_labels_.push_back(message_label);
    }
  } else {
    Label* message_label = new Label(message);
    message_label->SetMultiLine(true);
    message_label->SetAllowCharacterBreak(true);
    message_label->SetHorizontalAlignment(Label::ALIGN_LEFT);
    message_labels_.push_back(message_label);
  }

  if (options & HAS_PROMPT_FIELD) {
    prompt_field_ = new Textfield;
    prompt_field_->SetText(prompt);
  }

  ResetLayoutManager();
}

void MessageBoxView::ResetLayoutManager() {
  // Initialize the Grid Layout Manager used for this dialog box.
  GridLayout* layout = GridLayout::CreatePanel(this);
  SetLayoutManager(layout);

  gfx::Size icon_size;
  if (icon_)
    icon_size = icon_->GetPreferredSize();

  // Add the column set for the message displayed at the top of the dialog box.
  // And an icon, if one has been set.
  const int message_column_view_set_id = 0;
  ColumnSet* column_set = layout->AddColumnSet(message_column_view_set_id);
  if (icon_) {
    column_set->AddColumn(GridLayout::LEADING, GridLayout::LEADING, 0,
                          GridLayout::FIXED, icon_size.width(),
                          icon_size.height());
    column_set->AddPaddingColumn(0, kUnrelatedControlHorizontalSpacing);
  }
  column_set->AddColumn(GridLayout::FILL, GridLayout::FILL, 1,
                        GridLayout::FIXED, message_width_, 0);

  // Column set for prompt Textfield, if one has been set.
  const int textfield_column_view_set_id = 1;
  if (prompt_field_) {
    column_set = layout->AddColumnSet(textfield_column_view_set_id);
    if (icon_) {
      column_set->AddPaddingColumn(
          0, icon_size.width() + kUnrelatedControlHorizontalSpacing);
    }
    column_set->AddColumn(GridLayout::FILL, GridLayout::FILL, 1,
                          GridLayout::USE_PREF, 0, 0);
  }

  // Column set for checkbox, if one has been set.
  const int checkbox_column_view_set_id = 2;
  if (checkbox_) {
    column_set = layout->AddColumnSet(checkbox_column_view_set_id);
    if (icon_) {
      column_set->AddPaddingColumn(
          0, icon_size.width() + kUnrelatedControlHorizontalSpacing);
    }
    column_set->AddColumn(GridLayout::FILL, GridLayout::FILL, 1,
                          GridLayout::USE_PREF, 0, 0);
  }

  for (size_t i = 0; i < message_labels_.size(); ++i) {
    layout->StartRow(i, message_column_view_set_id);
    if (icon_) {
      if (i == 0)
        layout->AddView(icon_);
      else
        layout->SkipColumns(1);
    }
    layout->AddView(message_labels_[i]);
  }

  if (prompt_field_) {
    layout->AddPaddingRow(0, kRelatedControlVerticalSpacing);
    layout->StartRow(0, textfield_column_view_set_id);
    layout->AddView(prompt_field_);
  }

  if (checkbox_) {
    layout->AddPaddingRow(0, kRelatedControlVerticalSpacing);
    layout->StartRow(0, checkbox_column_view_set_id);
    layout->AddView(checkbox_);
  }

  layout->AddPaddingRow(0, kRelatedControlVerticalSpacing);
}

}  // namespace views
