#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Basic sanity tests for the GTalk extension.

This module contains the basic set of sanity tests run on the
GTalk extension.
"""

import logging
import sys
import time
import traceback
import urllib2

import gtalk_base_test
import pyauto_gtalk  # must preceed pyauto
import pyauto


class BasicTest(gtalk_base_test.GTalkBaseTest):
  """Test for Google Talk Chrome Extension."""

  def _OpenRoster(self):
    """Download Talk extension and open the roster."""

    self.InstallGTalkExtension()

    # Wait for the background view to load.
    extension = self.GetGTalkExtensionInfo()
    background_view = self.WaitUntilExtensionViewLoaded(
        extension_id=extension['id'],
        view_type='EXTENSION_BACKGROUND_PAGE')
    self.assertTrue(background_view,
        msg='Failed to get background view: views = %s.' %
        self.GetBrowserInfo()['extension_views'])

    # Click browser action icon
    self.TriggerBrowserActionById(extension['id'])

    # Wait for viewer window to open.
    self.assertTrue(
        self.WaitUntil(self.GetViewerInfo),
        msg='Timed out waiting for viewer.html to open.')

    # Wait for all iframes to load.
    self.WaitUntilResult(True,
        lambda: self.RunInViewer(
            'window.document.getElementsByTagName("iframe") != null'),
            msg='Timed out waiting for iframes to load.')

    # Wait for viewer window to load the sign-in page.
    self.WaitUntilCondition(
        lambda: self.RunInViewer('window.location.href',
                                 '//iframe[1]'),
        lambda url: url and '/qsignin' in url,
        msg='Timed out waiting for /qsignin page.')

  def _SignIn(self):
    """Download the extension, open the roster, and sign in"""
    # Open the roster.
    self._OpenRoster()

    # Wait for /qsignin's BODY.
    self.WaitUntilResult(True,
        lambda: self.RunInViewer(
            'Boolean($BODY())', '//iframe[1]'),
        msg='Timed out waiting for document.body in /qsignin page.')

    # Wait for the "Sign In" link.
    self.WaitUntilResult(True,
        lambda: self.RunInViewer(
            'Boolean($FindByText($BODY(), "Sign In"))', '//iframe[1]'),
        msg='Timed out waiting for "Sign In" link in DOM.')

    # Click the "Sign In" link.
    self.assertTrue(self.RunInViewer(
        '$Click($FindByText($BODY(), "Sign In"))', '//iframe[1]'))

    # Wait for the login page to open.
    self.assertTrue(self.WaitUntil(self.GetLoginPageInfo),
        msg='Timed out waiting for login page to open.')

    # Wait for the login page's form element.
    self.WaitUntilResult(True,
        lambda: self.RunInLoginPage('Boolean(document.forms[0])'),
        msg='Timed out waiting for document.forms[0].')

    # Fill and submit the login form.
    credentials = self.GetPrivateInfo()['test_quasar']

    self.RunInLoginPage(
        'document.forms[0].Email.value="' + credentials['username'] + '"')
    self.RunInLoginPage(
        'document.forms[0].Passwd.value="' + credentials['password'] + '"')
    self.RunInLoginPage('document.forms[0].submit() || true')

  def RunBasicFunctionalityTest(self):
    """Run tests for basic functionality in GTalk."""

    # Install the extension, open the viewer, and sign in.
    self._SignIn()

    # Wait for the roster container iframe.
    self.WaitUntilResult(True,
        lambda: self.RunInViewer(
        'window.document.getElementById("popoutRoster") != null'),
        msg='Timed out waiting for roster container iframe.')

    self.WaitUntilResult(True,
        lambda: self.RunInViewer('Boolean(window.frames[0])', '//iframe[1]'),
        msg='Timed out waiting for roster iframe.')

    # Wait for the roster iframe to load.
    self.WaitUntilCondition(
        lambda: self.RunInRoster('window.location.href'),
        lambda url: url and '/proster' in url,
        msg='Timed out waiting for /proster url.')

    self.WaitUntilResult(True,
        lambda: self.RunInRoster(
        'window.document.getElementById(":rf") != null'),
        msg='Timed out waiting for send message to label in roster DOM.')

    # Wait for "chatpinger@appspot.com" to appear in the roster.
    self.WaitUntilResult(True,
        lambda: self.RunInRoster(
            'Boolean($FindByText($BODY(), "chatpinger@appspot.com"))'),
        msg='Timed out waiting for chatpinger@appspot.com in roster DOM.')

    # Works around for issue where mole doesn't open when clicked too quickly.
    time.sleep(1)

    # Click "chatpinger@appspot.com" to open a chat mole.
    self.RunInRoster('$Click($FindByText($BODY(), "chatpinger@appspot.com"))')

    # Wait until ready to check whether mole is open(temporary work around).
    time.sleep(1)

    # Wait for chat mole to open.
    self.assertTrue(self.WaitUntil(self.GetMoleInfo),
        msg='Timed out waiting for mole window to open.')

    self.WaitUntilResult(True,
        lambda: self.RunInViewer(
        'window.document.getElementsByTagName("iframe") != null'),
        msg='Timed out waiting for iframes to load.')

    # Wait for chat mole to load.
    self.WaitUntilResult(True,
        lambda: self.RunInMole('Boolean(window.location.href)'),
        msg='Timed out waiting for mole window location.')

    # Wait for the chat mole's input textarea to load.
    self.WaitUntilResult(True,
        lambda: self.RunInMole(
            'Boolean($FindByTagName($BODY(), "textarea", 0))'),
        msg='Timed out waiting for mole textarea.')

    # Type /ping in the mole's input widget.
    self.assertTrue(self.RunInMole(
        '$Type($FindByTagName($BODY(), "textarea", 0), "/ping")'),
        msg='Error typing in mole textarea.')

    # Type ENTER in the mole's input widget.
    self.assertTrue(self.RunInMole(
        '$Press($FindByTagName($BODY(),"textarea",0), $KEYS.ENTER)'),
        msg='Error sending ENTER in mole textarea.')

    # Wait for chat input to clear.
    self.WaitUntilResult(True,
        lambda: self.RunInMole(
            'Boolean($FindByTagName($BODY(),"textarea",0).value=="")'),
        msg='Timed out waiting for textarea to clear after ENTER.')

    # Wait for /ping to appear in the chat history.
    self.WaitUntilCondition(
        lambda: self.RunInMole('window.document.body.innerHTML'),
        lambda html: html and '/ping' in html,
        msg='Timed out waiting for /ping to appear in mole DOM.')

    # Wait for the echo "Ping!" to appear in the chat history.
    self.WaitUntilCondition(
        lambda: self.RunInMole('window.document.body.innerHTML'),
        lambda html: html and 'Ping!' in html,
        msg='Timed out waiting for "Ping!" reply to appear in mole DOM.')

    # Request a ping in 7 seconds.
    self.assertTrue(self.RunInMole(
        '$Type($FindByTagName($BODY(),"textarea",0), "/ping 7")'),
        msg='Error typing "ping /7" in mole textarea.')

    # Press Enter in chat input.
    self.assertTrue(self.RunInMole(
        '$Press($FindByTagName($BODY(),"textarea",0), $KEYS.ENTER)'),
        msg='Error sending ENTER after "ping /7" in mole textarea.')

    # Briefly show mole for visual examination.
    # Also works around issue where extension may show the first
    # Ping! notification before closing the mole.
    time.sleep(2)

    # Press escape to close the mole.
    self.assertTrue(self.RunInMole(
        '$Press($FindByTagName($BODY(),"textarea",0), $KEYS.ESC)'),
        msg='Error sending ESC after "ping /7" in mole textarea.')

    # Wait for the mole to close.
    self.assertTrue(self.WaitUntil(
        lambda: not(bool(self.GetMoleInfo()))),
        msg='Timed out waiting for chatpinger mole to close.')

    # Ensure "chatpinger2@appspot.com" is in the roster.
    self.WaitUntilResult(True,
        lambda: self.RunInRoster(
            'Boolean($FindByText($BODY(), "chatpinger2@appspot.com"))'),
        msg='Timed out waiting for chatpinger2@appspot.com in roster DOM.')

    # Click "chatpinger2@appspot.com" in the roster.
    self.RunInRoster('$Click($FindByText($BODY(), "chatpinger2@appspot.com"))')

    self.WaitUntilResult(True,
        lambda: self.RunInViewer(
        'window.document.getElementsByTagName("iframe") != null'),
        msg='Timed out waiting for iframes to load.')

    # Wait for a second chat mole to open.
    time.sleep(1)
    self.assertTrue(self.WaitUntil(lambda: bool(self.GetMoleInfo(1))),
        msg='Timed out waiting for second mole window to open.')

    # Disable the extension.
    extension = self.GetGTalkExtensionInfo()
    self.SetExtensionStateById(extension['id'], enable=False,
        allow_in_incognito=False)
    extension = self.GetGTalkExtensionInfo()
    self.assertFalse(extension['is_enabled'])

    # Verify all moles + windows are closed.
    self.assertTrue(self.WaitUntil(lambda: not(bool(self.GetViewerInfo()))),
        msg='Timed out waiting for viewer.html to close after disabling.')
    self.assertTrue(self.WaitUntil(lambda: not(bool(self.GetMoleInfo()))),
        msg='Timed out waiting for first mole to close after disabling.')
    self.assertTrue(self.WaitUntil(lambda: not(bool(self.GetMoleInfo(1)))),
        msg='Timed out waiting for second mole to close after disabling.')

  def testBasicFunctionality(self):
    """Run tests for basic functionality in GTalk with retries."""

    # Since this test goes against prod servers, we'll retry to mitigate
    # flakiness due to network issues.
    RETRIES = 5
    for tries in range(RETRIES):
      logging.info('Calling RunBasicFunctionalityTest. Try #%s/%s'
          % (tries + 1, RETRIES))
      try:
        self.RunBasicFunctionalityTest()
        logging.info('RunBasicFunctionalityTest succeeded. Tries: %s'
            % (tries + 1))
        break
      except Exception as e:
        logging.info("\n*** ERROR in RunBasicFunctionalityTest ***")
        exc_type, exc_value, exc_traceback = sys.exc_info()
        traceback.print_exception(exc_type, exc_value, exc_traceback)
        logging.info("\n")
        if tries < RETRIES - 1:
          self.NavigateToURL('http://accounts.google.com/Logout')
          logging.info('Retrying...')
        else:
          raise


if __name__ == '__main__':
  pyauto_gtalk.Main()
