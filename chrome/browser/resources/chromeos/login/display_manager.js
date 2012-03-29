// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Display manager for WebUI OOBE and login.
 */

// TODO(xiyuan): Find a better to share those constants.
const SCREEN_GAIA_SIGNIN = 'gaia-signin';
const SCREEN_ACCOUNT_PICKER = 'account-picker';

/* Accelerator identifiers. Must be kept in sync with webui_login_view.cc. */
const ACCELERATOR_ACCESSIBILITY = 'accessibility';
const ACCELERATOR_CANCEL = 'cancel';
const ACCELERATOR_ENROLLMENT = 'enrollment';
const ACCELERATOR_EXIT = 'exit';
const ACCELERATOR_VERSION = 'version';

/* Help topic identifiers. */
const HELP_TOPIC_ENTERPRISE_REPORTING = 2535613;

cr.define('cr.ui.login', function() {
  var Bubble = cr.ui.Bubble;

  /**
   * Constructor a display manager that manages initialization of screens,
   * transitions, error messages display.
   *
   * @constructor
   */
  function DisplayManager() {
  }

  DisplayManager.prototype = {
    /**
     * Registered screens.
     */
    screens_: [],

    /**
     * Current OOBE step, index in the screens array.
     * @type {number}
     */
    currentStep_: 0,

    /**
     * Whether version label can be toggled by ACCELERATOR_VERSION.
     * @type {boolean}
     */
    allowToggleVersion_: false,

    /**
     * Gets current screen element.
     * @type {HTMLElement}
     */
    get currentScreen() {
      return $(this.screens_[this.currentStep_]);
    },

    /**
     * Hides/shows header (Shutdown/Add User/Cancel buttons).
     * @param {boolean} hidden Whether header is hidden.
     */
    set headerHidden(hidden) {
      $('login-header-bar').hidden = hidden;
    },

    /**
     * Shows/hides version labels.
     * @param {boolean} show Whether labels should be visible by default. If
     *     false, visibility can be toggled by ACCELERATOR_VERSION.
     */
    showVersion: function(show) {
      $('version-labels').hidden = !show;
      this.allowToggleVersion_ = !show;
    },

    /**
     * Handle accelerators.
     * @param {string} name Accelerator name.
     */
    handleAccelerator: function(name) {
      if (name == ACCELERATOR_ACCESSIBILITY) {
        chrome.send('toggleAccessibility', []);
      } else if (name == ACCELERATOR_CANCEL) {
        if (this.currentScreen.cancel) {
          this.currentScreen.cancel();
        }
      } else if (name == ACCELERATOR_ENROLLMENT) {
        var currentStepId = this.screens_[this.currentStep_];
        if (currentStepId == SCREEN_GAIA_SIGNIN)
          chrome.send('toggleEnrollmentScreen', []);
      } else if (name == ACCELERATOR_EXIT) {
        if (this.currentScreen.exit) {
          this.currentScreen.exit();
        }
      } else if (name == ACCELERATOR_VERSION) {
        if (this.allowToggleVersion_)
          $('version-labels').hidden = !$('version-labels').hidden;
      }
    },

    /**
     * Appends buttons to the button strip.
     * @param {Array} buttons Array with the buttons to append.
     */
    appendButtons_: function(buttons) {
      if (buttons) {
        var buttonStrip = $('button-strip');
        for (var i = 0; i < buttons.length; ++i) {
          var button = buttons[i];
          buttonStrip.appendChild(button);
        }
      }
    },

    /**
     * Updates a step's css classes to reflect left, current, or right position.
     * @param {number} stepIndex step index.
     * @param {string} state one of 'left', 'current', 'right'.
     */
    updateStep_: function(stepIndex, state) {
      var stepId = this.screens_[stepIndex];
      var step = $(stepId);
      var header = $('header-' + stepId);
      var states = ['left', 'right', 'current'];
      for (var i = 0; i < states.length; ++i) {
        if (states[i] != state) {
          step.classList.remove(states[i]);
          header.classList.remove(states[i]);
        }
      }
      step.classList.add(state);
      header.classList.add(state);
    },

    /**
     * Switches to the next OOBE step.
     * @param {number} nextStepIndex Index of the next step.
     */
    toggleStep_: function(nextStepIndex, screenData) {
      var currentStepId = this.screens_[this.currentStep_];
      var nextStepId = this.screens_[nextStepIndex];
      var oldStep = $(currentStepId);
      var newStep = $(nextStepId);
      var newHeader = $('header-' + nextStepId);

      if (oldStep.onBeforeHide)
        oldStep.onBeforeHide();

      if (newStep.onBeforeShow)
        newStep.onBeforeShow(screenData);

      newStep.classList.remove('hidden');

      if (this.isOobeUI()) {
        // Start gliding animation for OOBE steps.
        if (nextStepIndex > this.currentStep_) {
          for (var i = this.currentStep_; i < nextStepIndex; ++i)
            this.updateStep_(i, 'left');
          this.updateStep_(nextStepIndex, 'current');
        } else if (nextStepIndex < this.currentStep_) {
          for (var i = this.currentStep_; i > nextStepIndex; --i)
            this.updateStep_(i, 'right');
          this.updateStep_(nextStepIndex, 'current');
        }
      } else {
        // Start fading animation for login display.
        oldStep.classList.add('faded');
        newStep.classList.remove('faded');
      }

      // Adjust inner container height based on new step's height.
      $('inner-container').style.height = newStep.offsetHeight + 'px';

      if (this.currentStep_ != nextStepIndex &&
          !oldStep.classList.contains('hidden')) {
        oldStep.addEventListener('webkitTransitionEnd', function f(e) {
          oldStep.removeEventListener('webkitTransitionEnd', f);
          if (oldStep.classList.contains('faded') ||
              oldStep.classList.contains('left') ||
              oldStep.classList.contains('right')) {
            oldStep.classList.add('hidden');
          }
        });
      } else {
        // First screen on OOBE launch.
        newHeader.classList.remove('right');
      }
      this.currentStep_ = nextStepIndex;
      $('oobe').className = nextStepId;
    },

    /**
     * Show screen of given screen id.
     * @param {Object} screen Screen params dict,
     *     e.g. {id: screenId, data: data}.
     */
    showScreen: function(screen) {
      var screenId = screen.id;

      // Show sign-in screen instead of account picker if pod row is empty.
      if (screenId == SCREEN_ACCOUNT_PICKER && $('pod-row').pods.length == 0) {
        // Manually hide 'add-user' header bar, because of the case when
        // 'Cancel' button is used on the offline login page.
        $('add-user-header-bar-item').hidden = true;
        Oobe.showSigninUI(true);
        return;
      }

      var data = screen.data;
      var index = this.getScreenIndex_(screenId);
      if (index >= 0)
        this.toggleStep_(index, data);
      $('error-message').update();
    },

    /**
     * Gets index of given screen id in screens_.
     * @param {string} screenId Id of the screen to look up.
     * @private
     */
    getScreenIndex_: function(screenId) {
      for (var i = 0; i < this.screens_.length; ++i) {
        if (this.screens_[i] == screenId)
          return i;
      }
      return -1;
    },

    /**
     * Register an oobe screen.
     * @param {Element} el Decorated screen element.
     */
    registerScreen: function(el) {
      var screenId = el.id;
      this.screens_.push(screenId);

      var header = document.createElement('span');
      header.id = 'header-' + screenId;
      header.className = 'header-section right';
      header.textContent = el.header ? el.header : '';
      $('header-sections').appendChild(header);

      var dot = document.createElement('div');
      dot.id = screenId + '-dot';
      dot.className = 'progdot';
      $('progress').appendChild(dot);

      this.appendButtons_(el.buttons);
    },

    /**
     * Updates localized content of the screens like headers, buttons and links.
     * Should be executed on language change.
     */
    updateLocalizedContent_: function() {
      $('button-strip').innerHTML = '';
      for (var i = 0, screenId; screenId = this.screens_[i]; ++i) {
        var screen = $(screenId);
        $('header-' + screenId).textContent = screen.header;
        this.appendButtons_(screen.buttons);
        if (screen.updateLocalizedContent)
          screen.updateLocalizedContent();
      }

      // This screen is a special case as it's not registered with the rest of
      // the screens.
      login.ErrorMessageScreen.updateLocalizedContent();

      // Trigger network drop-down to reload its state
      // so that strings are reloaded.
      // Will be reloaded if drowdown is actually shown.
      cr.ui.DropDown.refresh();
    },

    /**
     * Prepares screens to use in login display.
     */
    prepareForLoginDisplay_: function() {
      for (var i = 0, screenId; screenId = this.screens_[i]; ++i) {
        var screen = $(screenId);
        screen.classList.add('faded');
        screen.classList.remove('right');
        screen.classList.remove('left');
      }
    },

    /**
     * Returns true if Oobe UI is shown.
     */
    isOobeUI: function() {
      return !document.body.classList.contains('login-display');
    }
  };

  /**
   * Returns offset (top, left) of the element.
   * @param {!Element} element HTML element.
   * @return {!Object} The offset (top, left).
   */
  DisplayManager.getOffset = function(element) {
    var x = 0;
    var y = 0;
    while (element && !isNaN(element.offsetLeft) && !isNaN(element.offsetTop)) {
      x += element.offsetLeft - element.scrollLeft;
      y += element.offsetTop - element.scrollTop;
      element = element.offsetParent;
    }
    return { top: y, left: x };
  };

  /**
   * Returns position (top, left, right, bottom) of the element.
   * @param {!Element} element HTML element.
   * @return {!Object} Element position (top, left, right, bottom).
   */
  DisplayManager.getPosition = function(element) {
    var offset = DisplayManager.getOffset(element);
    return { top: offset.top,
             right: window.innerWidth - element.offsetWidth - offset.left,
             bottom: window.innerHeight - element.offsetHeight - offset.top,
             left: offset.left };
  };

  /**
   * Disables signin UI.
   */
  DisplayManager.disableSigninUI = function() {
    $('login-header-bar').disabled = true;
    $('pod-row').disabled = true;
  };

  /**
   * Shows signin UI.
   * @param {string} opt_email An optional email for signin UI.
   */
  DisplayManager.showSigninUI = function(opt_email) {
    $('add-user-button').hidden = true;
    $('cancel-add-user-button').hidden = false;
    chrome.send('showAddUser', [opt_email]);
  };

  /**
   * Resets sign-in input fields.
   * @param {boolean} forceOnline Whether online sign-in should be forced.
   *     If |forceOnline| is false previously used sign-in type will be used.
   */
  DisplayManager.resetSigninUI = function(forceOnline) {
    var currentScreenId = Oobe.getInstance().currentScreen.id;

    $(SCREEN_GAIA_SIGNIN).reset(
        currentScreenId == SCREEN_GAIA_SIGNIN, forceOnline);
    $('login-header-bar').disabled = false;
    $('pod-row').reset(currentScreenId == SCREEN_ACCOUNT_PICKER);
  };

  /**
   * Shows sign-in error bubble.
   * @param {number} loginAttempts Number of login attemps tried.
   * @param {string} message Error message to show.
   * @param {string} link Text to use for help link.
   * @param {number} helpId Help topic Id associated with help link.
   */
  DisplayManager.showSignInError = function(loginAttempts, message, link,
                                            helpId) {
    var error = document.createElement('div');

    var messageDiv = document.createElement('div');
    messageDiv.className = 'error-message';
    messageDiv.textContent = message;
    error.appendChild(messageDiv);

    if (link) {
      messageDiv.classList.add('error-message-padding');

      var helpLink = document.createElement('a');
      helpLink.href = '#';
      helpLink.textContent = link;
      helpLink.addEventListener('click', function(e) {
        chrome.send('launchHelpApp', [helpId]);
        e.preventDefault();
      });
      error.appendChild(helpLink);
    }

    var currentScreenId = Oobe.getInstance().currentScreen.id;
    $(currentScreenId).showErrorBubble(loginAttempts, error);
  };

  /**
   * Clears error bubble.
   */
  DisplayManager.clearErrors = function() {
    $('bubble').hide();
  };

  /**
   * Sets text content for a div with |labelId|.
   * @param {string} labelId Id of the label div.
   * @param {string} labelText Text for the label.
   */
  DisplayManager.setLabelText = function(labelId, labelText) {
    $(labelId).textContent = labelText;
  };

  /**
   * Sets the text content of the enterprise info message.
   * @param {string} messageText The message text.
   * @param {boolean} showReportingHint Whether to show the reporting warning.
   */
  DisplayManager.setEnterpriseInfo = function(messageText, showReportingHint) {
    $('enterprise-info-message').textContent = messageText;
    if (messageText) {
      $('enterprise-info-container').hidden = false;
      if (showReportingHint) {
        $('enterprise-reporting-hint-container').hidden = false;
        var link = $('enterprise-reporting-hint-link');
        link.addEventListener('click', function(e) {
          chrome.send('launchHelpApp', [HELP_TOPIC_ENTERPRISE_REPORTING]);
          e.preventDefault();
        });
      }
    }
  };

  // Export
  return {
    DisplayManager: DisplayManager
  };
});
