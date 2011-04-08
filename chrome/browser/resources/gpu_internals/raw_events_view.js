// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 *
 * @fileoverview Displays the traced data in raw format. Its primarily
 * usefulness is to allow users to copy-paste their data in an easy to
 * read format for bug reports.
 *
 */
cr.define('gpu', function() {
  /**
   * Provides information on the GPU process and underlying graphics hardware.
   * @constructor
   * @extends {gpu.Tab}
   */
  var RawEventsView = cr.ui.define(gpu.Tab);

  RawEventsView.prototype = {
    __proto__: gpu.Tab.prototype,

    decorate: function() {
      tracingController.addEventListener('traceBegun',
                                         this.setNeedsRefresh_.bind(this));
      tracingController.addEventListener('traceEnded',
                                         this.setNeedsRefresh_.bind(this));
      this.addEventListener('selectedChange', this.onViewSelectedChange_);

      this.setNeedsRefresh_();
    },

    onViewSelectedChange_: function() {
      if (this.selected) {
        if (!tracingController.traceEvents.length) {
          tracingController.beginTracing();
        }
        if (this.needsRefreshOnShow_) {
          this.refresh();
        }
      }
    },

    setNeedsRefresh_: function() {
      if (!this.selected) {
        this.needsRefreshOnShow_ = true;
        return;
      } else {
        this.refresh();
      }
    },

    /**
     * Updates the view based on its currently known data
     */
    refresh: function() {
      this.needsRefreshOnShow_ = false;

      var dataElement = this.querySelector('.raw-events-view-data');
      if (tracingController.isTracingEnabled) {
        var tmp = 'Still tracing. ' +
            'Uncheck the enable tracing button to see traced data.';
        dataElement.textContent = tmp;
      } else if (!tracingController.traceEvents.length) {
        dataElement.textContent =
            'No trace data collected. Collect data first.';
      } else {
        var events = tracingController.traceEvents;
        dataElement.textContent = JSON.stringify(events);

        var selection = window.getSelection();
        selection.removeAllRanges();
        var range = document.createRange();
        range.selectNodeContents(dataElement);
        selection.addRange(range);
      }
    }

  };

  return {
    RawEventsView: RawEventsView
  };
});
