# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This is all.gyp file for Android to prevent breakage in Android and other
# platform; It will be churning a lot in the short term and eventually be merged
# into all.gyp.

{
  'targets': [
    {
      'target_name': 'All',
      'type': 'none',
      'dependencies': [
        '../content/content.gyp:content_shell_apk',
        'util/build_util.gyp:*',
        'android_builder_tests',
      ],
    }, # target_name: All
    {
      # The current list of tests for android.  This is temporary
      # until the full set supported.  If adding a new test here,
      # please also add it to build/android/run_tests.py, else the
      # test is not run.
      #
      # WARNING:
      # Do not add targets here without communicating the implications
      # on tryserver triggers and load.  Discuss with jrg please.
      'target_name': 'android_builder_tests',
      'type': 'none',
      'dependencies': [
        '../base/base.gyp:base_unittests',
        '../chrome/chrome.gyp:sync_unit_tests',
        '../content/content.gyp:content_unittests',
        '../sql/sql.gyp:sql_unittests',
        '../ipc/ipc.gyp:ipc_tests',
        '../net/net.gyp:net_unittests',
        '../ui/ui.gyp:ui_unittests',
        '../third_party/WebKit/Source/WebKit/chromium/All.gyp:*',
        # From here down: not added to run_tests.py yet.
        '../jingle/jingle.gyp:jingle_unittests',
        '../tools/android/fake_dns/fake_dns.gyp:fake_dns',
        '../tools/android/forwarder/forwarder.gyp:forwarder',
        '../media/media.gyp:media_unittests',
      ],
    },
    { 
      # Experimental / in-progress targets that are expected to fail
      # but we still try to compile them on bots (turning the stage
      # orange, not red).
      'target_name': 'android_experimental',
      'type': 'none',
      'dependencies': [
      ],
    },
    {
      # In-progress targets that are expected to fail and are NOT run
      # on any bot.
      'target_name': 'android_in_progress',
      'type': 'none',
      'dependencies': [
        '../content/content.gyp:content_browsertests',
        '../chrome/chrome.gyp:unit_tests',
        '../ui/ui.gyp:gfx_unittests',
      ],
    },
  ],  # targets
}
