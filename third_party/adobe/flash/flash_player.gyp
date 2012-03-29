# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'flapper_version_h_file%': 'flapper_version.h',
    'flapper_binary_files%': [],
    'conditions': [
      [ 'branding == "Chrome"', {
        'conditions': [
          [ 'OS == "linux" and target_arch == "x64"', {
            'flapper_version_h_file%': 'symbols/ppapi/linux_x64/flapper_version.h',
            'flapper_binary_files%': [
              'binaries/ppapi/linux_x64/libpepflashplayer.so',
              'binaries/ppapi/linux_x64/manifest.json',
            ],
          }],
          [ 'OS == "linux" and target_arch == "ia32"', {
            'flapper_version_h_file%': 'symbols/ppapi/linux/flapper_version.h',
            'flapper_binary_files%': [
              'binaries/ppapi/linux/libpepflashplayer.so',
              'binaries/ppapi/linux/manifest.json',
            ],
          }],
        ],
      }],
    ],
  },
  # Always provide a target, so we can put the logic about whether there's
  # anything to be done in this file (instead of a higher-level .gyp file).
  'targets': [
    {
      'target_name': 'flash_player',
      'type': 'none',
      'conditions': [
        [ 'branding == "Chrome"', {
          'copies': [{
            'destination': '<(PRODUCT_DIR)',
            'files': [],
            'conditions': [
              [ 'OS == "mac"', {
                'files': [
                  'binaries/mac/Flash Player Plugin for Chrome.plugin',
                  'binaries/mac/plugin.vch',
                ]
              }],
              [ 'OS == "win"', {
                'files': [
                  'binaries/win/FlashPlayerApp.exe',
                  'binaries/win/FlashPlayerCPLApp.cpl',
                  'binaries/win/gcswf32.dll',
                  'binaries/win/plugin.vch',
                  'symbols/win/gcswf32.pdb',
                ]
              }],
            ],
          }],
        }],
      ],
    },
    {
      'target_name': 'flapper_version_h',
      'type': 'none',
      'copies': [{
        'destination': '<(SHARED_INTERMEDIATE_DIR)',
        'files': [ '<(flapper_version_h_file)' ],
      }],
    },
    {
      'target_name': 'flapper_binaries',
      'type': 'none',
      'copies': [{
        'destination': '<(PRODUCT_DIR)/PepperFlash',
        'files': [ '<@(flapper_binary_files)' ],
      }],
    },
  ],
}
