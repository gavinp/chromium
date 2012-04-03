// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/gtk/password_generation_bubble_gtk.h"

#include "base/utf_string_conversions.h"
#include "chrome/browser/ui/gtk/bubble/bubble_gtk.h"
#include "chrome/browser/ui/gtk/theme_service_gtk.h"
#include "chrome/common/autofill_messages.h"
#include "content/public/browser/render_view_host.h"
#include "ui/base/gtk/gtk_hig_constants.h"

const int kContentBorder = 4;
const int kHorizontalSpacing = 4;

PasswordGenerationBubbleGtk::PasswordGenerationBubbleGtk(
    const gfx::Rect& anchor_rect,
    GtkWidget* anchor_widget,
    Profile* profile,
    content::RenderViewHost* render_view_host)
    : render_view_host_(render_view_host) {
  // TODO(gcasto): Localize text after we have finalized the UI.
  // crbug.com/118062
  GtkWidget* content = gtk_vbox_new(FALSE, 5);

  // We have two lines of content. The first is just the title,
  GtkWidget* title_line = gtk_hbox_new(FALSE,  0);
  GtkWidget* title = gtk_label_new("Password Suggestion");
  gtk_box_pack_start(GTK_BOX(title_line), title, FALSE, FALSE, 0);

  // The second contains the password in a text field and an accept button.
  GtkWidget* password_line = gtk_hbox_new(FALSE, kHorizontalSpacing);
  text_field_ = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(text_field_),
                     password_generator_.Generate().c_str());
  gtk_entry_set_max_length(GTK_ENTRY(text_field_), 15);
  GtkWidget* accept_button = gtk_button_new_with_label("Try It");
  gtk_box_pack_start(GTK_BOX(password_line), text_field_, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(password_line), accept_button, TRUE, TRUE, 0);

  gtk_container_set_border_width(GTK_CONTAINER(content), kContentBorder);
  gtk_box_pack_start(GTK_BOX(content), title_line, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(content), password_line, TRUE, TRUE, 0);

  bubble_ = BubbleGtk::Show(anchor_widget,
                            &anchor_rect,
                            content,
                            BubbleGtk::ARROW_LOCATION_TOP_LEFT,
                            true,  // match_system_theme
                            true,  // grab_input
                            ThemeServiceGtk::GetFrom(profile),
                            NULL);  // delegate

  g_signal_connect(content, "destroy",
                   G_CALLBACK(&OnDestroyThunk), this);
  g_signal_connect(accept_button, "clicked",
                   G_CALLBACK(&OnAcceptClickedThunk), this);
}

PasswordGenerationBubbleGtk::~PasswordGenerationBubbleGtk() {}

void PasswordGenerationBubbleGtk::OnDestroy(GtkWidget* widget) {
  // We are self deleting, we have a destroy signal setup to catch when we are
  // destroyed (via the BubbleGtk being destroyed), and delete ourself.
  delete this;
}

void PasswordGenerationBubbleGtk::OnAcceptClicked(GtkWidget* widget) {
  render_view_host_->Send(new AutofillMsg_GeneratedPasswordAccepted(
      render_view_host_->GetRoutingID(),
      UTF8ToUTF16(gtk_entry_get_text(GTK_ENTRY(text_field_)))));
  bubble_->Close();
}
