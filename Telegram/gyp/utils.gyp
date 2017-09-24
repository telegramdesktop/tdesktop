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
  'includes': [
    'common.gypi',
  ],
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
      'mac_target': '10.10',
    },
    'includes': [
      'common_executable.gypi',
      'qt.gypi',
    ],
    'conditions': [
      [ 'build_win', {
        'libraries': [
          'libeay32',
          'ssleay32',
          'Crypt32',
          'zlibstat',
          'LzmaLib',
        ],
      }],
      [ 'build_linux', {
        'libraries': [
          'ssl',
          'crypto',
          'lzma',
        ],
      }],
      [ 'build_mac', {
        'include_dirs': [
          '<(libs_loc)/openssl-xcode/include'
        ],
        'library_dirs': [
          '<(libs_loc)/openssl-xcode',
        ],
        'xcode_settings': {
          'OTHER_LDFLAGS': [
            '-lssl',
            '-lcrypto',
            '-llzma',
          ],
        },
      }],
    ],
    'include_dirs': [
      '<(src_loc)',
      '<(libs_loc)/lzma/C',
      '<(libs_loc)/zlib',
    ],
    'sources': [
      '<(src_loc)/_other/packer.cpp',
      '<(src_loc)/_other/packer.h',
    ],
    'configurations': {
      'Debug': {
        'conditions': [
          [ 'build_win', {
            'include_dirs': [
              '<(libs_loc)/openssl/Debug/include',
            ],
            'library_dirs': [
              '<(libs_loc)/openssl/Debug/lib',
              '<(libs_loc)/lzma/C/Util/LzmaLib/Debug',
              '<(libs_loc)/zlib/contrib/vstudio/vc14/x86/ZlibStatDebug',
            ],
          }, {
            'include_dirs': [
              '/usr/local/include',
              '<(libs_loc)/openssl-xcode/include'
            ],
            'library_dirs': [
              '/usr/local/lib',
            ],
          }]
        ],
      },
      'Release': {
        'conditions': [
          [ 'build_win', {
            'include_dirs': [
              '<(libs_loc)/openssl/Release/include',
            ],
            'library_dirs': [
              '<(libs_loc)/openssl/Release/lib',
              '<(libs_loc)/lzma/C/Util/LzmaLib/Release',
              '<(libs_loc)/zlib/contrib/vstudio/vc14/x86/ZlibStatReleaseWithoutAsm',
            ],
          }, {
            'include_dirs': [
              '/usr/local/include',
              '<(libs_loc)/openssl-xcode/include'
            ],
            'library_dirs': [
              '/usr/local/lib',
            ],
          }]
        ],
      },
    },
  }],
}
