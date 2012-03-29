// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logging.h"
#include "chrome/browser/automation/automation_event_observer.h"
#include "chrome/browser/automation/automation_event_queue.h"
#include "chrome/browser/automation/automation_provider.h"
#include "chrome/browser/automation/automation_provider_json.h"
#include "content/public/browser/dom_operation_notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"

AutomationEventObserver::AutomationEventObserver(
    AutomationEventQueue* event_queue, bool recurring)
    : event_queue_(event_queue),
      recurring_(recurring),
      observer_id_(-1),
      event_count_(0) {
  DCHECK(event_queue_ != NULL);
}

AutomationEventObserver::~AutomationEventObserver() {}

void AutomationEventObserver::NotifyEvent(DictionaryValue* value) {
  if (event_queue_) {
    event_queue_->NotifyEvent(
        new AutomationEventQueue::AutomationEvent(
            GetId(), value));
    event_count_++;
  }
}

void AutomationEventObserver::Init(int observer_id) {
  if (observer_id_ < 0) {
    observer_id_ = observer_id;
  }
}

int AutomationEventObserver::GetId() const {
  return observer_id_;
}

void AutomationEventObserver::RemoveIfDone() {
  if (!recurring_ && event_count_ > 0 && event_queue_) {
    event_queue_->RemoveObserver(GetId());
  }
}

DomRaisedEventObserver::DomRaisedEventObserver(
    AutomationEventQueue* event_queue,
    const std::string& event_name,
    int automation_id,
    bool recurring)
    : AutomationEventObserver(event_queue, recurring),
      event_name_(event_name),
      automation_id_(automation_id) {
  registrar_.Add(this, content::NOTIFICATION_DOM_OPERATION_RESPONSE,
                 content::NotificationService::AllSources());
}

DomRaisedEventObserver::~DomRaisedEventObserver() {}

void DomRaisedEventObserver::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  if (type == content::NOTIFICATION_DOM_OPERATION_RESPONSE) {
    content::Details<content::DomOperationNotificationDetails> dom_op_details(
        details);
    if ((dom_op_details->automation_id == automation_id_ ||
         automation_id_ == -1) &&
        (event_name_.length() == 0 ||
         event_name_.compare(dom_op_details->json) == 0)) {
      DictionaryValue* dict = new DictionaryValue;
      dict->SetString("type", "raised_event");
      dict->SetString("name", dom_op_details->json);
      dict->SetInteger("observer_id", GetId());
      NotifyEvent(dict);
    }
  }
  // Nothing should happen after RemoveIfDone() as it may delete the object.
  RemoveIfDone();
}
