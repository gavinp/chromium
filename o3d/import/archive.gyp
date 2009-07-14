# Copyright (c) 2009 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'chromium_code': 1,
  },
  'includes': [
    '../build/common.gypi',
  ],
  'target_defaults': {
    'include_dirs': [
      '..',
      '../..',
      '../../<(cgdir)/include',
      '../../<(gtestdir)',
    ],
  },
  'targets': [
    {
      'target_name': 'o3dArchive',
      'type': 'static_library',
      'dependencies': [
        '../../<(zlibdir)/zlib.gyp:zlib',
      ],
      'sources': [
        'cross/archive_processor.cc',
        'cross/archive_processor.h',
        'cross/archive_request.cc',
        'cross/archive_request.h',
        'cross/gz_compressor.cc',
        'cross/gz_compressor.h',
        'cross/gz_decompressor.cc',
        'cross/gz_decompressor.h',
        'cross/iarchive_generator.h',
        'cross/memory_buffer.h',
        'cross/memory_stream.cc',
        'cross/memory_stream.h',
        'cross/raw_data.cc',
        'cross/raw_data.h',
        'cross/tar_processor.cc',
        'cross/tar_processor.h',
        'cross/targz_generator.h',
        'cross/targz_processor.cc',
        'cross/targz_processor.h',
      ],
    },
    {
      'target_name': 'o3dArchiveTest',
      'type': 'none',
      'dependencies': [
        'o3dArchive',
      ],
      'direct_dependent_settings': {
        'sources': [
          'cross/gz_compressor_test.cc',
          'cross/gz_decompressor_test.cc',
          'cross/memory_buffer_test.cc',
          'cross/memory_stream_test.cc',
          'cross/raw_data_test.cc',
          'cross/tar_processor_test.cc',
          'cross/targz_processor_test.cc',
        ],
      },
    },
  ],
}
