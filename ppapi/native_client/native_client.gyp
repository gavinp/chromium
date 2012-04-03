# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'includes': [
    '../../native_client/build/common.gypi',
  ],
  'conditions': [
    ['disable_nacl==0 and disable_nacl_untrusted==0', {
      'targets': [
        {
          'target_name': 'ppapi_lib',
          'type': 'none',
          'dependencies': [
             '../../native_client/src/untrusted/pthread/pthread.gyp:pthread_lib',
             '../../native_client/src/untrusted/irt_stub/irt_stub.gyp:ppapi_stub_lib',
          ],
          'copies': [
            {
              'destination': '<(SHARED_INTERMEDIATE_DIR)/tc_newlib/lib32',
              'files': [
                  '<(DEPTH)/native_client/src/untrusted/irt_stub/libppapi.a',
              ],
            },
            {
              'destination': '<(SHARED_INTERMEDIATE_DIR)/tc_newlib/lib64',
              'files': [
                  '<(DEPTH)/native_client/src/untrusted/irt_stub/libppapi.a',
              ],
            },
            {
              'destination': '<(SHARED_INTERMEDIATE_DIR)/tc_newlib/libarm',
              'files': [
                '<(DEPTH)/native_client/src/untrusted/irt_stub/libppapi.a',
              ],
            },
          ],
        },
        {
          'target_name': 'nacl_irt',
          'type': 'none',
          'variables': {
            'nexe_target': 'nacl_irt',
            'out64': '<(PRODUCT_DIR)/nacl_irt_x86_64.nexe',
            'out32': '<(PRODUCT_DIR)/nacl_irt_x86_32.nexe',
            'out_arm': '<(PRODUCT_DIR)/nacl_irt_arm.nexe',
            'build_glibc': 0,
            'build_newlib': 1,
            'include_dirs': [
              'lib/gl/include',
              '..',
            ],
            'link_flags': [
              '-Wl,--start-group',
              '-lirt_browser',
              '-lppruntime',
              '-lsrpc',
              '-limc_syscalls',
              '-lplatform',
              '-lgio',
              '-Wl,--end-group',
              '-lm',
            ],
            # See http://code.google.com/p/nativeclient/issues/detail?id=2691.
            # The PNaCl linker (gold) does not implement the "-Ttext-segment"
            # option.  However, with the linker for x86, the "-Ttext" option
            # does not affect the executable's base address.
            # TODO(olonho): simplify flags handling and avoid duplication
            # with NaCl logic.
            'conditions': [
              ['target_arch!="arm"',
               {
                 'link_flags': [
                   '-Wl,--section-start,.rodata=<(NACL_IRT_DATA_START)',
                   '-Wl,-Ttext-segment=<(NACL_IRT_TEXT_START)',
                 ]
               }, { # target_arch == "arm"
                 'link_flags': [
                   '-Wl,--section-start,.rodata=<(NACL_IRT_DATA_START)',
                   '-Wl,-Ttext=<(NACL_IRT_TEXT_START)',
                   '--pnacl-allow-native',
                   '-arch', 'arm',
                   '-Wt,-mtls-use-call',
                 ],
               },
             ],
            ],
            'sources': [
            ],
            'extra_args': [
              '--strip-debug',
            ],
            'extra_deps64': [
              '<(SHARED_INTERMEDIATE_DIR)/tc_newlib/lib64/libppruntime.a',
              '<(SHARED_INTERMEDIATE_DIR)/tc_newlib/lib64/libirt_browser.a',
              '<(SHARED_INTERMEDIATE_DIR)/tc_newlib/lib64/libsrpc.a',
              '<(SHARED_INTERMEDIATE_DIR)/tc_newlib/lib64/libplatform.a',
              '<(SHARED_INTERMEDIATE_DIR)/tc_newlib/lib64/libimc_syscalls.a',
              '<(SHARED_INTERMEDIATE_DIR)/tc_newlib/lib64/libgio.a',
            ],
            'extra_deps32': [
              '<(SHARED_INTERMEDIATE_DIR)/tc_newlib/lib32/libppruntime.a',
              '<(SHARED_INTERMEDIATE_DIR)/tc_newlib/lib32/libirt_browser.a',
              '<(SHARED_INTERMEDIATE_DIR)/tc_newlib/lib32/libsrpc.a',
              '<(SHARED_INTERMEDIATE_DIR)/tc_newlib/lib32/libplatform.a',
              '<(SHARED_INTERMEDIATE_DIR)/tc_newlib/lib32/libimc_syscalls.a',
              '<(SHARED_INTERMEDIATE_DIR)/tc_newlib/lib32/libgio.a',
            ],
            'extra_deps_arm': [
              '<(SHARED_INTERMEDIATE_DIR)/tc_newlib/libarm/libppruntime.a',
              '<(SHARED_INTERMEDIATE_DIR)/tc_newlib/libarm/libirt_browser.a',
              '<(SHARED_INTERMEDIATE_DIR)/tc_newlib/libarm/libsrpc.a',
              '<(SHARED_INTERMEDIATE_DIR)/tc_newlib/libarm/libplatform.a',
              '<(SHARED_INTERMEDIATE_DIR)/tc_newlib/libarm/libimc_syscalls.a',
              '<(SHARED_INTERMEDIATE_DIR)/tc_newlib/libarm/libgio.a',
            ],
          },
          'dependencies': [
            'src/shared/ppapi_proxy/ppapi_proxy_untrusted.gyp:ppruntime_lib',
            '../../native_client/src/untrusted/irt/irt.gyp:irt_browser_lib',
            '../../native_client/src/shared/srpc/srpc.gyp:srpc_lib',
            '../../native_client/src/shared/platform/platform.gyp:platform_lib',
            '../../native_client/src/untrusted/nacl/nacl.gyp:imc_syscalls_lib',
            '../../native_client/src/shared/gio/gio.gyp:gio_lib',
          ],
        },
      ],
    }],
  ],
}
