# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'conditions': [[ 'build_linux', {
    'variables': {
      'variables': {
        'build_defines%': '',
      },
      'not_need_gtk%': '<!(python -c "print(\'TDESKTOP_DISABLE_GTK_INTEGRATION\' in \'<(build_defines)\')")',
      'pkgconfig_libs': [
# In order to work libxkbcommon must be linked statically,
# PKGCONFIG links it like "-L/usr/local/lib -lxkbcommon"
# which makes a dynamic link which leads to segfault in
# QApplication() -> createPlatformIntegration -> QXcbIntegrationPlugin::create
        #'xkbcommon',
      ],
    },
    'libraries': [
      '-Wl,-Bstatic',
      '-lbreakpad_client',
      '-llzma',
      '-lopenal',
      '-lavformat',
      '-lavcodec',
      '-lswresample',
      '-lswscale',
      '-lavutil',
      '-lopus',
      '-lva-x11',
      '-lva-drm',
      '-lva',
      '-lvdpau',
      '-ldrm',
      '-lz',
      '-lXi',
      '-lXext',
      '-lXfixes',
      '-lXrender',
      '<(linux_lib_ssl)',
      '<(linux_lib_crypto)',
#      '<!(pkg-config 2> /dev/null --libs <@(pkgconfig_libs))',
    ],
    'cflags_cc': [
      '-Wno-strict-overflow',
      '-Wno-maybe-uninitialized',
    ],
    'ldflags': [
      '-Wl,-wrap,aligned_alloc',
      '-Wl,-wrap,secure_getenv',
      '-Wl,-wrap,clock_gettime',
      '-Wl,--no-as-needed,-lrt',
      '-Wl,-Bstatic',
    ],
    'configurations': {
      'Release': {
        'cflags_c': [
          '-Ofast',
          '-fno-strict-aliasing',
        ],
        'cflags_cc': [
          '-Ofast',
          '-fno-strict-aliasing',
        ],
        'ldflags': [
          '-Ofast',
        ],
      },
    },
    'conditions': [
      [ '"<!(uname -p)" != "x86_64"', {
        'ldflags': [
          '-Wl,-wrap,__divmoddi4',
        ],
      }], ['not_need_gtk!="True"', {
        'cflags_cc': [
          '<!(pkg-config 2> /dev/null --cflags gtk+-2.0)',
          '<!(pkg-config 2> /dev/null --cflags glib-2.0)',
          '<!(pkg-config 2> /dev/null --cflags dee-1.0)',
        ],
      }], ['<!(pkg-config ayatana-appindicator3-0.1; echo $?) == 0', {
        'cflags_cc': [ '<!(pkg-config --cflags ayatana-appindicator3-0.1)' ],
        'defines': [ 'TDESKTOP_USE_AYATANA_INDICATORS' ],
      }], ['<!(pkg-config ayatana-appindicator-0.1; echo $?) == 0', {
        'cflags_cc': [ '<!(pkg-config --cflags ayatana-appindicator-0.1)' ],
        'defines': [ 'TDESKTOP_USE_AYATANA_INDICATORS' ],
      }], ['<!(pkg-config appindicator3-0.1; echo $?) == 0', {
        'cflags_cc': [ '<!(pkg-config --cflags appindicator3-0.1)' ],
      }], ['<!(pkg-config appindicator-0.1; echo $?) == 0', {
        'cflags_cc': [ '<!(pkg-config --cflags appindicator-0.1)' ],
      }]
    ],
  }]],
}
