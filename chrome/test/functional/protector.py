#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import pyauto_functional  # Must be imported first
import pyauto
import test_utils

import json
import logging
import os


class BaseProtectorTest(pyauto.PyUITest):
  """Base class for Protector test cases."""

  _DEFAULT_SEARCH_ID_KEY = 'Default Search Provider ID'

  # Possible values for session.restore_on_startup pref:
  _SESSION_STARTUP_HOMEPAGE = 0  # For migration testing only.
  _SESSION_STARTUP_LAST = 1
  _SESSION_STARTUP_URLS = 4
  _SESSION_STARTUP_NTP = 5

  def setUp(self):
    pyauto.PyUITest.setUp(self)
    # Get the profile path of the first profile.
    profiles = self.GetMultiProfileInfo()
    self.assertTrue(profiles['profiles'])
    self._profile_path = profiles['profiles'][0]['path']
    self.assertTrue(self._profile_path)
    # Set to the keyword of the new default search engine after a successful
    # _GetDefaultSearchEngine call.
    self._new_default_search_keyword = None

  def _GetDefaultSearchEngine(self):
    """Returns the default search engine, if any; None otherwise.

    Returns:
       Dictionary describing the default search engine. See GetSearchEngineInfo
       for an example.
    """
    for search_engine in self.GetSearchEngineInfo():
      if search_engine['is_default']:
        return search_engine
    return None

  def _OpenDatabase(self, db_name):
    """Opens a given SQLite database in the default profile.

    Args:
      db_name: name of database file (relative to the profile directory).

    Returns:
      An sqlite3.Connection instance.

    Raises:
      ImportError if sqlite3 module is not found.
    """
    db_path = os.path.join(self._profile_path, db_name)
    logging.info('Opening DB: %s' % db_path)
    import sqlite3
    db_conn = sqlite3.connect(db_path)
    db_conn.isolation_level = None
    self.assertTrue(db_conn)
    return db_conn

  def _FetchSingleValue(self, conn, query, parameters=None):
    """Executes an SQL query that should select a single row with a single
    column and returns its value.

    Args:
      conn: sqlite3.Connection instance.
      query: SQL query (may contain placeholders).
      parameters: parameters to substitute for query.

    Returns:
      Value of the column fetched.
    """
    cursor = conn.cursor()
    cursor.execute(query, parameters)
    row = cursor.fetchone()
    self.assertTrue(row)
    self.assertEqual(1, len(row))
    return row[0]

  def _UpdateSingleRow(self, conn, query, parameters=None):
    """Executes an SQL query that should update a single row.

    Args:
      conn: sqlite3.Connection instance.
      query: SQL query (may contain placeholders).
      parameters: parameters to substitute for query.
    """
    cursor = conn.cursor()
    cursor.execute(query, parameters)
    self.assertEqual(1, cursor.rowcount)

  def _ChangeDefaultSearchEngine(self):
    """Replaces the default search engine in Web Data database with another one.

    Keywords of the new default search engine is saved to
    self._new_default_search_keyword.
    """
    web_database = self._OpenDatabase('Web Data')
    default_id = int(self._FetchSingleValue(
        web_database,
        'SELECT value FROM meta WHERE key = ?',
        (self._DEFAULT_SEARCH_ID_KEY,)))
    self.assertTrue(default_id)
    new_default_id = int(self._FetchSingleValue(
        web_database,
        'SELECT id FROM keywords WHERE id != ? LIMIT 1',
        (default_id,)))
    self.assertTrue(new_default_id)
    self.assertNotEqual(default_id, new_default_id)
    self._UpdateSingleRow(web_database,
                          'UPDATE meta SET value = ? WHERE key = ?',
                          (new_default_id, self._DEFAULT_SEARCH_ID_KEY))
    self._new_default_search_keyword = self._FetchSingleValue(
        web_database,
        'SELECT keyword FROM keywords WHERE id = ?',
        (new_default_id,))
    logging.info('Update default search ID: %d -> %d (%s)' %
                 (default_id, new_default_id, self._new_default_search_keyword))
    web_database.close()

  def _LoadPreferences(self):
    """Reads the contents of Preferences file.

    Returns: dict() with user preferences as returned by PrefsInfo.Prefs().
    """
    prefs_path = os.path.join(self._profile_path, 'Preferences')
    logging.info('Opening prefs: %s' % prefs_path)
    with open(prefs_path) as f:
      return json.load(f)

  def _WritePreferences(self, prefs):
    """Writes new contents to the Preferences file.

    Args:
      prefs: dict() with new user preferences as returned by PrefsInfo.Prefs().
    """
    with open(os.path.join(self._profile_path, 'Preferences'), 'w') as f:
      json.dump(prefs, f)

  def _InvalidatePreferencesBackup(self):
    """Makes the Preferences backup invalid by clearing the signature."""
    prefs = self._LoadPreferences()
    prefs['backup']['_signature'] = 'INVALID'
    self._WritePreferences(prefs)

  def _ChangeSessionStartupPrefs(self, startup_type, startup_urls=None,
                                 homepage=None):
    """Changes the session startup type and the list of URLs to load on startup.

    Args:
      startup_type: int with one of _SESSION_STARTUP_* values.
      startup_urls: list(str) with a list of URLs; if None, is left unchanged.
      homepage: unless None, the new value for homepage.
    """
    prefs = self._LoadPreferences()
    prefs['session']['restore_on_startup'] = startup_type
    if startup_urls is not None:
      prefs['session']['urls_to_restore_on_startup'] = startup_urls
    if homepage is not None:
      prefs['homepage'] = homepage
    self._WritePreferences(prefs)

  def _ChangePinnedTabsPrefs(self, pinned_tabs):
    """Changes the list of pinned tabs.

    Args:
      pinned_tabs: list(str) with a list of pinned tabs URLs.
    """
    prefs = self._LoadPreferences()
    prefs['pinned_tabs'] = []
    for tab in pinned_tabs:
      prefs['pinned_tabs'].append({'url': tab})
    self._WritePreferences(prefs)

  def _AssertSingleTabOpen(self, url):
    """Asserts that a single tab with given url is open.

    Args:
      url: URL of the single tab.
    """
    info = self.GetBrowserInfo()
    self.assertEqual(1, len(info['windows']))  # one window
    self.assertEqual(1, len(info['windows'][0]['tabs']))  # one tab
    self.assertEqual(url, info['windows'][0]['tabs'][0]['url'])

  def testNoChangeOnCleanProfile(self):
    """Test that no change is reported on a clean profile."""
    self.assertFalse(self.GetProtectorState()['showing_change'])


class ProtectorSearchEngineTest(BaseProtectorTest):
  """Test suite for search engine change detection with Protector enabled."""

  def testDetectSearchEngineChangeAndApply(self):
    """Test for detecting and applying a default search engine change."""
    # Get current search engine.
    old_default_search = self._GetDefaultSearchEngine()
    self.assertTrue(old_default_search)
    # Close browser, change the search engine and start it again.
    self.RestartBrowser(clear_profile=False,
                        pre_launch_hook=self._ChangeDefaultSearchEngine)
    # The change must be detected by Protector.
    self.assertTrue(self.GetProtectorState()['showing_change'])
    default_search = self._GetDefaultSearchEngine()
    # Protector must restore the old search engine.
    self.assertEqual(old_default_search, default_search)
    self.ApplyProtectorChange()
    # Now the search engine must have changed to the new one.
    default_search = self._GetDefaultSearchEngine()
    self.assertNotEqual(old_default_search, default_search)
    self.assertEqual(self._new_default_search_keyword,
                     default_search['keyword'])
    # No longer showing the change.
    self.assertFalse(self.GetProtectorState()['showing_change'])

  def testDetectSearchEngineChangeAndDiscard(self):
    """Test for detecting and discarding a default search engine change."""
    # Get current search engine.
    old_default_search = self._GetDefaultSearchEngine()
    self.assertTrue(old_default_search)
    # Close browser, change the search engine and start it again.
    self.RestartBrowser(clear_profile=False,
                        pre_launch_hook=self._ChangeDefaultSearchEngine)
    # The change must be detected by Protector.
    self.assertTrue(self.GetProtectorState()['showing_change'])
    default_search = self._GetDefaultSearchEngine()
    # Protector must restore the old search engine.
    self.assertEqual(old_default_search, default_search)
    self.DiscardProtectorChange()
    # Old search engine remains active.
    default_search = self._GetDefaultSearchEngine()
    self.assertEqual(old_default_search, default_search)
    # No longer showing the change.
    self.assertFalse(self.GetProtectorState()['showing_change'])

  def testSearchEngineChangeDismissedOnEdit(self):
    """Test that default search engine change is dismissed when default search
    engine is changed by user.
    """
    # Get current search engine.
    old_default_search = self._GetDefaultSearchEngine()
    self.assertTrue(old_default_search)
    # Close browser, change the search engine and start it again.
    self.RestartBrowser(clear_profile=False,
                        pre_launch_hook=self._ChangeDefaultSearchEngine)
    # The change must be detected by Protector.
    self.assertTrue(self.GetProtectorState()['showing_change'])
    # Change default search engine.
    self.MakeSearchEngineDefault(self._new_default_search_keyword)
    # Change is successful.
    default_search = self._GetDefaultSearchEngine()
    self.assertEqual(self._new_default_search_keyword,
                     default_search['keyword'])
    # No longer showing the change.
    self.assertFalse(self.GetProtectorState()['showing_change'])
    search_urls = [engine['url'] for engine in self.GetSearchEngineInfo()]
    # Verify there are no duplicate search engines:
    self.assertEqual(len(search_urls), len(set(search_urls)))

  def testSearchEngineChangeWithMultipleWindows(self):
    """Test that default search engine change is detected in multiple
    browser windows.
    """
    # Get current search engine.
    old_default_search = self._GetDefaultSearchEngine()
    self.assertTrue(old_default_search)
    # Close browser, change the search engine and start it again.
    self.RestartBrowser(clear_profile=False,
                        pre_launch_hook=self._ChangeDefaultSearchEngine)
    # The change must be detected by Protector in first window
    self.OpenNewBrowserWindow(True)
    self.assertTrue(self.GetProtectorState(window_index=0)['showing_change'])
    # Open another Browser Window
    self.OpenNewBrowserWindow(True)
    # The change must be detected by Protector in second window
    self.assertTrue(self.GetProtectorState(window_index=1)['showing_change'])

  def testSearchEngineChangeDiscardedOnRelaunchingBrowser(self):
    """Verify that relaunching the browser while Protector is showing a change
    discards it.
    """
    # Get current search engine.
    old_default_search = self._GetDefaultSearchEngine()
    self.assertTrue(old_default_search)
    # Close browser, change the search engine and start it again.
    self.RestartBrowser(clear_profile=False,
                        pre_launch_hook=self._ChangeDefaultSearchEngine)
    # The change must be detected by Protector.
    self.assertTrue(self.GetProtectorState()['showing_change'])
    default_search = self._GetDefaultSearchEngine()
    self.assertEqual(old_default_search, default_search)
    # After relaunching the browser, old search engine still must be active.
    self.RestartBrowser(clear_profile=False)
    default_search = self._GetDefaultSearchEngine()
    self.assertEqual(old_default_search, default_search)
    # No longer showing the change.
    self.assertFalse(self.GetProtectorState()['showing_change'])

# TODO(ivankr): more hijacking cases (remove the current default search engine,
# add new search engines to the list, invalidate backup, etc).


class ProtectorPreferencesTest(BaseProtectorTest):
  """Generic test suite for Preferences protection."""

  def testPreferencesBackupInvalid(self):
    """Test for detecting invalid Preferences backup."""
    # Set startup prefs to open specific URLs.
    self.SetPrefs(pyauto.kRestoreOnStartup, self._SESSION_STARTUP_URLS)
    self.SetPrefs(pyauto.kURLsToRestoreOnStartup, ['http://www.google.com/'])
    self.RestartBrowser(
        clear_profile=False,
        pre_launch_hook=self._InvalidatePreferencesBackup)
    # The change must be detected by Protector.
    self.assertTrue(self.GetProtectorState()['showing_change'])
    # Startup settings are reset to default (NTP).
    self.assertEqual(self._SESSION_STARTUP_NTP,
                     self.GetPrefsInfo().Prefs(pyauto.kRestoreOnStartup))
    # Verify that previous startup URL has not been opened.
    self._AssertSingleTabOpen('chrome://newtab/')
    # Click "Edit Settings...".
    self.DiscardProtectorChange()
    # Verify that a new tab with settings is opened.
    info = self.GetBrowserInfo()
    self.assertEqual(1, len(info['windows']))  # one window
    self.assertEqual(2, len(info['windows'][0]['tabs']))  # 2 tabs
    self.assertEqual('chrome://chrome/settings/',
                     info['windows'][0]['tabs'][1]['url'])
    # No longer showing the change.
    self.assertFalse(self.GetProtectorState()['showing_change'])
    self.RestartBrowser(clear_profile=False)
    # Not showing the change after a restart
    self.assertFalse(self.GetProtectorState()['showing_change'])

  def testPreferencesBackupInvalidRestoreLastSession(self):
    """Test that session restore setting is not reset if backup is invalid."""
    # Set startup prefs to restore the last session.
    self.SetPrefs(pyauto.kRestoreOnStartup, self._SESSION_STARTUP_LAST)
    previous_url = 'chrome://version/'
    self.NavigateToURL(previous_url)
    self.RestartBrowser(
        clear_profile=False,
        pre_launch_hook=self._InvalidatePreferencesBackup)
    # The change must be detected by Protector.
    self.assertTrue(self.GetProtectorState()['showing_change'])
    # Startup settings are left unchanged.
    self.assertEqual(self._SESSION_STARTUP_LAST,
                     self.GetPrefsInfo().Prefs(pyauto.kRestoreOnStartup))
    # Session has been restored.
    self._AssertSingleTabOpen(previous_url)

  def testPreferencesBackupInvalidChangeDismissedOnEdit(self):
    """Test that editing protected prefs dismisses the invalid backup bubble."""
    self.RestartBrowser(
        clear_profile=False,
        pre_launch_hook=self._InvalidatePreferencesBackup)
    # The change must be detected by Protector.
    self.assertTrue(self.GetProtectorState()['showing_change'])
    # Change some protected setting manually.
    self.SetPrefs(pyauto.kHomePage, 'http://example.com/')
    # No longer showing the change.
    self.assertFalse(self.GetProtectorState()['showing_change'])


class ProtectorSessionStartupTest(BaseProtectorTest):
  """Test suite for session startup changes detection with Protector enabled.
  """
  def testDetectSessionStartupChangeAndApply(self):
    """Test for detecting and applying a session startup pref change."""
    # Set startup prefs to restoring last open tabs.
    self.SetPrefs(pyauto.kRestoreOnStartup, self._SESSION_STARTUP_LAST)
    previous_url = 'chrome://version/'
    self.NavigateToURL(previous_url)
    # Restart browser with startup prefs set to open google.com.
    self.RestartBrowser(
        clear_profile=False,
        pre_launch_hook=lambda: self._ChangeSessionStartupPrefs(
            self._SESSION_STARTUP_URLS,
            startup_urls=['http://www.google.com']))
    # The change must be detected by Protector.
    self.assertTrue(self.GetProtectorState()['showing_change'])
    # Protector must restore old preference values.
    self.assertEqual(self._SESSION_STARTUP_LAST,
                     self.GetPrefsInfo().Prefs(pyauto.kRestoreOnStartup))
    # Verify that open tabs are consistent with restored prefs.
    self._AssertSingleTabOpen(previous_url)
    self.ApplyProtectorChange()
    # Now the new preference values are active.
    self.assertEqual(self._SESSION_STARTUP_URLS,
                     self.GetPrefsInfo().Prefs(pyauto.kRestoreOnStartup))
    # No longer showing the change.
    self.assertFalse(self.GetProtectorState()['showing_change'])

  def testDetectSessionStartupChangeAndApply(self):
    """Test for detecting and discarding a session startup pref change."""
    # Set startup prefs to restoring last open tabs.
    self.SetPrefs(pyauto.kRestoreOnStartup, self._SESSION_STARTUP_LAST)
    # Restart browser with startup prefs set to open google.com.
    self.RestartBrowser(
        clear_profile=False,
        pre_launch_hook=lambda: self._ChangeSessionStartupPrefs(
            self._SESSION_STARTUP_URLS,
            startup_urls=['http://www.google.com']))
    # The change must be detected by Protector.
    self.assertTrue(self.GetProtectorState()['showing_change'])
    # Old preference values restored.
    self.assertEqual(self._SESSION_STARTUP_LAST,
                     self.GetPrefsInfo().Prefs(pyauto.kRestoreOnStartup))
    self.DiscardProtectorChange()
    # Old preference values are still active.
    self.assertEqual(self._SESSION_STARTUP_LAST,
                     self.GetPrefsInfo().Prefs(pyauto.kRestoreOnStartup))
    # No longer showing the change.
    self.assertFalse(self.GetProtectorState()['showing_change'])

  def testSessionStartupChangeDismissedOnEdit(self):
    """Test for that editing startup prefs manually dismissed the change."""
    # Set startup prefs to restoring last open tabs.
    self.SetPrefs(pyauto.kRestoreOnStartup, self._SESSION_STARTUP_LAST)
    # Restart browser with startup prefs set to open google.com.
    self.RestartBrowser(
        clear_profile=False,
        pre_launch_hook=lambda: self._ChangeSessionStartupPrefs(
            self._SESSION_STARTUP_URLS,
            startup_urls=['http://www.google.com']))
    # The change must be detected by Protector.
    self.assertTrue(self.GetProtectorState()['showing_change'])
    # Change the setting manually.
    self.SetPrefs(pyauto.kRestoreOnStartup, self._SESSION_STARTUP_NTP)
    # No longer showing the change.
    self.assertFalse(self.GetProtectorState()['showing_change'])

  def testSessionStartupPrefMigration(self):
    """Test migration from old session.restore_on_startup values (homepage)."""
    # Set startup prefs to restoring last open tabs.
    self.SetPrefs(pyauto.kRestoreOnStartup, self._SESSION_STARTUP_LAST)
    self.SetPrefs(pyauto.kURLsToRestoreOnStartup, [])
    new_homepage = 'http://www.google.com/'
    # Restart browser with startup prefs set to open homepage (google.com).
    self.RestartBrowser(
        clear_profile=False,
        pre_launch_hook=lambda: self._ChangeSessionStartupPrefs(
            self._SESSION_STARTUP_HOMEPAGE,
            homepage=new_homepage))
    # The change must be detected by Protector.
    self.assertTrue(self.GetProtectorState()['showing_change'])
    # Protector must restore old preference values.
    self.assertEqual(self._SESSION_STARTUP_LAST,
                     self.GetPrefsInfo().Prefs(pyauto.kRestoreOnStartup))
    self.assertEqual([],
                     self.GetPrefsInfo().Prefs(pyauto.kURLsToRestoreOnStartup))
    self.ApplyProtectorChange()
    # Now the new preference values are active.
    self.assertEqual(self._SESSION_STARTUP_URLS,
                     self.GetPrefsInfo().Prefs(pyauto.kRestoreOnStartup))
    # Homepage migrated to the list of startup URLs.
    self.assertEqual([new_homepage],
                     self.GetPrefsInfo().Prefs(pyauto.kURLsToRestoreOnStartup))
    # No longer showing the change.
    self.assertFalse(self.GetProtectorState()['showing_change'])

  def testDetectPinnedTabsChangeAndApply(self):
    """Test for detecting and applying a change to pinned tabs."""
    pinned_urls = ['chrome://version/', 'chrome://credits/']
    self.RestartBrowser(
        clear_profile=False,
        pre_launch_hook=lambda: self._ChangePinnedTabsPrefs(pinned_urls))
    # The change must be detected by Protector.
    self.assertTrue(self.GetProtectorState()['showing_change'])
    # Protector must restore old preference values.
    self.assertEqual([],
                     self.GetPrefsInfo().Prefs(pyauto.kPinnedTabs))
    # No pinned tabs are open, only NTP.
    info = self.GetBrowserInfo()
    self._AssertSingleTabOpen('chrome://newtab/')
    self.ApplyProtectorChange()
    # No longer showing the change.
    self.assertFalse(self.GetProtectorState()['showing_change'])
    # Pinned tabs should have been opened now in the correct order.
    info = self.GetBrowserInfo()
    self.assertEqual(1, len(info['windows']))  # one window
    self.assertEqual(3, len(info['windows'][0]['tabs']))  # 3 tabs
    self.assertEqual(pinned_urls[0], info['windows'][0]['tabs'][0]['url'])
    self.assertEqual(pinned_urls[1], info['windows'][0]['tabs'][1]['url'])
    self.assertTrue(info['windows'][0]['tabs'][0]['pinned'])  # 1st tab pinned
    self.assertTrue(info['windows'][0]['tabs'][1]['pinned'])  # 2nd tab pinned
    self.RestartBrowser(clear_profile=False)
    # Not showing the change after a restart
    self.assertFalse(self.GetProtectorState()['showing_change'])
    # Same pinned tabs are open.
    info = self.GetBrowserInfo()
    self.assertEqual(1, len(info['windows']))  # one window
    self.assertEqual(3, len(info['windows'][0]['tabs']))  # 3 tabs
    self.assertEqual(pinned_urls[0], info['windows'][0]['tabs'][0]['url'])
    self.assertEqual(pinned_urls[1], info['windows'][0]['tabs'][1]['url'])
    self.assertTrue(info['windows'][0]['tabs'][0]['pinned'])  # 1st tab pinned
    self.assertTrue(info['windows'][0]['tabs'][1]['pinned'])  # 2nd tab pinned

  def testDetectPinnedTabsChangeAndDiscard(self):
    """Test for detecting and discarding a change to pinned tabs."""
    pinned_url = 'chrome://version/'
    self.RestartBrowser(
        clear_profile=False,
        pre_launch_hook=lambda: self._ChangePinnedTabsPrefs([pinned_url]))
    # The change must be detected by Protector.
    self.assertTrue(self.GetProtectorState()['showing_change'])
    # Protector must restore old preference values.
    self.assertEqual([],
                     self.GetPrefsInfo().Prefs(pyauto.kPinnedTabs))
    # No pinned tabs are open, only NTP.
    info = self.GetBrowserInfo()
    self._AssertSingleTabOpen('chrome://newtab/')
    self.DiscardProtectorChange()
    # No longer showing the change.
    self.assertFalse(self.GetProtectorState()['showing_change'])
    # Pinned tabs are not opened after another restart.
    self.RestartBrowser(clear_profile=False)
    self._AssertSingleTabOpen('chrome://newtab/')
    # Not showing the change after a restart.
    self.assertFalse(self.GetProtectorState()['showing_change'])


class ProtectorDisabledTest(BaseProtectorTest):
  """Test suite for Protector in disabled state."""

  def ExtraChromeFlags(self):
    """Ensures Protector is disabled.

    Returns:
      A list of extra flags to pass to Chrome when it is launched.
    """
    return super(ProtectorDisabledTest, self).ExtraChromeFlags() + [
        '--no-protector'
        ]

  def testInfobarIsPresent(self):
    """Verify that an infobar is present when running Chrome with --no-protector
    flag.
    """
    self.assertTrue(self.GetBrowserInfo()['windows'][0]['tabs'][0]['infobars'])

  def testNoSearchEngineChangeReported(self):
    """Test that the default search engine change is neither reported to user
    nor reverted.
    """
    # Get current search engine.
    old_default_search = self._GetDefaultSearchEngine()
    self.assertTrue(old_default_search)
    # Close browser, change the search engine and start it again.
    self.RestartBrowser(clear_profile=False,
                        pre_launch_hook=self._ChangeDefaultSearchEngine)
    # The change must not be reported by Protector.
    self.assertFalse(self.GetProtectorState()['showing_change'])
    default_search = self._GetDefaultSearchEngine()
    # The new search engine must be active.
    self.assertEqual(self._new_default_search_keyword,
                     default_search['keyword'])

  def testNoPreferencesBackupInvalidReported(self):
    """Test that invalid Preferences backup is not reported."""
    self.SetPrefs(pyauto.kRestoreOnStartup, self._SESSION_STARTUP_URLS)
    new_url = 'chrome://version/'
    self.SetPrefs(pyauto.kURLsToRestoreOnStartup, [new_url])
    self.RestartBrowser(
        clear_profile=False,
        pre_launch_hook=self._InvalidatePreferencesBackup)
    # The change must not be reported by Protector.
    self.assertFalse(self.GetProtectorState()['showing_change'])
    # New preference values must be active.
    self.assertEqual(self._SESSION_STARTUP_URLS,
                     self.GetPrefsInfo().Prefs(pyauto.kRestoreOnStartup))
    # Verify that open tabs are consistent with new prefs.
    info = self.GetBrowserInfo()
    self.assertEqual(1, len(info['windows']))  # one window
    self.assertEqual(1, len(info['windows'][0]['tabs']))  # one tab
    self.assertEqual(new_url, info['windows'][0]['tabs'][0]['url'])

  def testNoSessionStartupChangeReported(self):
    """Test that the session startup change is neither reported nor reverted."""
    # Set startup prefs to restoring last open tabs.
    self.SetPrefs(pyauto.kRestoreOnStartup, self._SESSION_STARTUP_LAST)
    new_url = 'chrome://version/'
    self.NavigateToURL('http://www.google.com/')
    # Restart browser with startup prefs set to open google.com.
    self.RestartBrowser(
        clear_profile=False,
        pre_launch_hook=lambda: self._ChangeSessionStartupPrefs(
            self._SESSION_STARTUP_URLS,
            startup_urls=[new_url]))
    # The change must not be reported by Protector.
    self.assertFalse(self.GetProtectorState()['showing_change'])
    # New preference values must be active.
    self.assertEqual(self._SESSION_STARTUP_URLS,
                     self.GetPrefsInfo().Prefs(pyauto.kRestoreOnStartup))
    # Verify that open tabs are consistent with new prefs.
    info = self.GetBrowserInfo()
    self.assertEqual(1, len(info['windows']))  # one window
    self.assertEqual(1, len(info['windows'][0]['tabs']))  # one tab
    self.assertEqual(new_url, info['windows'][0]['tabs'][0]['url'])


if __name__ == '__main__':
  pyauto_functional.Main()
