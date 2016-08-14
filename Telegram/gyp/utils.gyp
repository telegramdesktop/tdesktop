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
  'targets': [{
    'target_name': 'Updater',
    'variables': {
      'libs_loc': '../../../Libraries',
      'src_loc': '../SourceFiles',
      'res_loc': '../Resources',
    },
    'includes': [
      'common_executable.gypi',
    ],

    'include_dirs': [
      '<(src_loc)',
    ],
    'sources': [
      '<(src_loc)/_other/updater.cpp',
      '<(src_loc)/_other/updater.h',
      '<(src_loc)/_other/updater_linux.cpp',
      '<(src_loc)/_other/updater_osx.m',
    ],
    'conditions': [
      [ 'build_win', {
        'sources': [
          '<(res_loc)/winrc/Updater.rc',
        ],
      }],
      [ '"<(build_linux)" != "1"', {
        'sources!': [
          '<(src_loc)/_other/updater_linux.cpp',
        ],
      }],
      [ '"<(build_mac)" != "1"', {
        'sources!': [
          '<(src_loc)/_other/updater_osx.m',
        ],
      }],
      [ '"<(build_win)" != "1"', {
        'sources!': [
          '<(src_loc)/_other/updater.cpp',
        ],
      }],
    ],
  }, {
    'target_name': 'Packer',
    'variables': {
      'libs_loc': '../../../Libraries',
      'src_loc': '../SourceFiles',
    },
    'includes': [
      'common_executable.gypi',
      'qt.gypi',
    ],
    'libraries': [
      'libeay32',
      'ssleay32',
      'Crypt32',
      'zlibstat',
      'LzmaLib',
    ],

    'include_dirs': [
      '<(src_loc)',
      '<(libs_loc)/lzma/C',
      '<(libs_loc)/zlib-1.2.8',
    ],
    'sources': [
      '<(src_loc)/_other/packer.cpp',
      '<(src_loc)/_other/packer.h',
    ],
    'configurations': {
      'Debug': {
        'include_dirs': [
          '<(libs_loc)/openssl_debug/Debug/include',
        ],
        'library_dirs': [
          '<(libs_loc)/lzma/C/Util/LzmaLib/Debug',
          '<(libs_loc)/zlib-1.2.8/contrib/vstudio/vc11/x86/ZlibStatDebug',
          '<(libs_loc)/openssl_debug/Debug/lib',
        ],
      },
      'Release': {
        'include_dirs': [
          '<(libs_loc)/openssl/Release/include',
        ],
        'library_dirs': [
          '<(libs_loc)/lzma/C/Util/LzmaLib/Release',
          '<(libs_loc)/zlib-1.2.8/contrib/vstudio/vc11/x86/ZlibStatRelease',
          '<(libs_loc)/openssl/Release/lib',
        ],
      },
    },
  }],
}
