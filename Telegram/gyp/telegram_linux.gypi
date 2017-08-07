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
      'linux_path_libexif_lib%': '<(libs_loc)/libexif-0.6.20/libexif/.libs',
      'linux_path_va%': '/usr/local',
      'linux_path_vdpau%': '/usr/local',
      'linux_path_breakpad%': '<(libs_loc)/breakpad',
      'linux_path_opus_include%': '<(libs_loc)/opus/include',
    },
    'include_dirs': [
      '/usr/local/include',
      '<(linux_path_ffmpeg)/include',
      '<(linux_path_openal)/include',
      '<(linux_path_breakpad)/include/breakpad',
      '<(linux_path_opus_include)',
    ],
    'library_dirs': [
      '/usr/local/lib',
      '<(linux_path_ffmpeg)/lib',
      '<(linux_path_openal)/lib',
      '<(linux_path_libexif_lib)',
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
    'conditions': [['not_need_gtk!="True"', {
        'cflags_cc': [
            '<!(pkg-config 2> /dev/null --cflags appindicator-0.1)',
            '<!(pkg-config 2> /dev/null --cflags gtk+-2.0)',
            '<!(pkg-config 2> /dev/null --cflags glib-2.0)',
            '<!(pkg-config 2> /dev/null --cflags dee-1.0)',
         ],
    }]],
    'configurations': {
      'Release': {
        'cflags': [
          '-Ofast',
          '-flto',
          '-fno-strict-aliasing',
        ],
        'cflags_cc': [
          '-Ofast',
          '-flto',
          '-fno-strict-aliasing',
        ],
        'ldflags': [
          '-Ofast',
          '-flto',
        ],
      },
    },
    'cmake_precompiled_header': '<(src_loc)/stdafx.h',
    'cmake_precompiled_header_script': 'PrecompiledHeader.cmake',
  }]],
}
