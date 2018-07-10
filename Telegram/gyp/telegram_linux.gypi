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
      'linux_path_ffmpeg%': '/usr/local',
      'linux_path_openal%': '/usr/local',
      'linux_path_va%': '/usr/local',
      'linux_path_vdpau%': '/usr/local',
      'linux_path_breakpad%': '/usr/local',
      'linux_path_opus_include%': '<(libs_loc)/opus/include',
      'linux_path_range%': '/usr/local',
    },
    'include_dirs': [
      '/usr/local/include',
      '<(linux_path_ffmpeg)/include',
      '<(linux_path_openal)/include',
      '<(linux_path_breakpad)/include/breakpad',
      '<(linux_path_opus_include)',
      '<(linux_path_range)/include',
    ],
    'library_dirs': [
      '/usr/local/lib',
      '<(linux_path_ffmpeg)/lib',
      '<(linux_path_openal)/lib',
      '<(linux_path_va)/lib',
      '<(linux_path_vdpau)/lib',
      '<(linux_path_breakpad)/lib',
    ],
    'libraries': [
      'breakpad_client',
      'composeplatforminputcontextplugin',
      'ibusplatforminputcontextplugin',
      'fcitxplatforminputcontextplugin',
      'himeplatforminputcontextplugin',
      'liblzma.a',
      'libopenal.a',
      'libavformat.a',
      'libavcodec.a',
      'libswresample.a',
      'libswscale.a',
      'libavutil.a',
      'libopus.a',
      'libva-x11.a',
      'libva-drm.a',
      'libva.a',
      'libvdpau.a',
      'libdrm.a',
      'libz.a',
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
          '<!(pkg-config 2> /dev/null --cflags appindicator-0.1)',
          '<!(pkg-config 2> /dev/null --cflags gtk+-2.0)',
          '<!(pkg-config 2> /dev/null --cflags glib-2.0)',
          '<!(pkg-config 2> /dev/null --cflags dee-1.0)',
        ],
      }]
    ],
  }]],
}
