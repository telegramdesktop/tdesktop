# This file is part of Telegram Desktop,
# the official desktop version of Telegram messaging app, see https://telegram.org
#
# Telegram Desktop is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# It is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# In addition, as a special exception, the copyright holders give permission
# to link the code of portions of this program with the OpenSSL library.
#
# Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
# Copyright (c) 2014 John Preston, https://desktop.telegram.org

{
  'conditions': [[ 'build_linux', {
    'variables': {
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
      [ '"<!(uname -p)" == "x86_64"', {
        # 32 bit version can't be linked with debug info or LTO,
        # virtual memory exhausted :(
        'cflags_c': [ '-g' ],
        'cflags_cc': [ '-g' ],
        'ldflags': [ '-g' ],
        'configurations': {
          'Release': {
            'cflags_c': [ '-flto' ],
            'cflags_cc': [ '-flto' ],
            'ldflags': [ '-flto' ],
          },
        },
      }, {
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
    'cmake_precompiled_header': '<(src_loc)/stdafx.h',
    'cmake_precompiled_header_script': 'PrecompiledHeader.cmake',
  }]],
}
