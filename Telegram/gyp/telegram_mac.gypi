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
  'conditions': [[ 'build_mac', {
    'xcode_settings': {
      'GCC_PREFIX_HEADER': '<(src_loc)/stdafx.h',
      'GCC_PRECOMPILE_PREFIX_HEADER': 'YES',
    },
    'include_dirs': [
      '/usr/local/include',
      '<(libs_loc)/openssl-xcode/include'
    ],
    'library_dirs': [
      '/usr/local/lib',
      '<(libs_loc)/libexif-0.6.20/libexif/.libs',
      '<(libs_loc)/openssl-xcode',
    ],
    'configurations': {
      'Debug': {
        'library_dirs': [
          '<(libs_loc)/crashpad/crashpad/out/Debug',
        ],
      },
      'Release': {
        'library_dirs': [
          '<(libs_loc)/crashpad/crashpad/out/Release',
        ],
      },
    },
    'libraries': [
      '-lcups',
      '-lbsm',
      '-lm',
      '-lssl',
      '-lcrypto',
      '-llzma',
      '-lopenal',
      '-lopus',
      '-lexif',
      '-lavcodec',
      '-lavformat',
      '-lswscale',
      '-lswresample',
      '-lavutil',
      '-liconv',
      '-lbase',
      '-lcrashpad_client',
      '-lcrashpad_util',
    ],
  }]],
}
