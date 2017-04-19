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
    'settings_win.gypi',
    'settings_mac.gypi',
    'settings_linux.gypi',
  ],
  'variables': {
    'variables': {
      'variables': {
        'variables': {
          'variables': {
            'build_os%': '<(OS)',
          },
          'build_os%': '<(build_os)',
          'conditions': [
            [ 'build_os == "win"', {
              'build_win': 1,
            }, {
              'build_win': 0,
            }],
            [ 'build_os == "mac"', {
              'build_mac': 1,
            }, {
              'build_mac': 0,
            }],
            [ 'build_os == "linux"', {
              'build_linux': 1,
            }, {
              'build_linux': 0,
            }],
          ],
        },
        'build_os%': '<(build_os)',
        'build_win%': '<(build_win)',
        'build_mac%': '<(build_mac)',
        'build_linux%': '<(build_linux)',
      },
      'build_os%': '<(build_os)',
      'build_win%': '<(build_win)',
      'build_mac%': '<(build_mac)',
      'build_linux%': '<(build_linux)',

      'official_build_target%': '',
    },
    'build_os%': '<(build_os)',
    'build_win%': '<(build_win)',
    'build_mac%': '<(build_mac)',
    'build_linux%': '<(build_linux)',
    'official_build_target%': '<(official_build_target)',

    # GYP does not support per-configuration libraries :(
    # So they will be emulated through additional link flags,
    # which will contain <(ld_lib_prefix)LibraryName<(ld_lib_postfix)
    'conditions': [
      [ 'build_win', {
        'ld_lib_prefix': '',
        'ld_lib_postfix': '.lib',
        'exe_ext': '.exe',
      }, {
        'ld_lib_prefix': '-l',
        'ld_lib_postfix': '',
        'exe_ext': '',
      }],
      [ '"<(official_build_target)" == "mac32"', {
        'mac_target%': '10.6',
        'build_macold': 1,
      }, {
        'mac_target%': '10.8',
        'build_macold': 0,
      }],
      [ '"<(official_build_target)" == "macstore"', {
        'build_macstore': 1,
      }, {
        'build_macstore': 0,
      }],
      [ '"<(official_build_target)" == "uwp"', {
        'build_uwp': 1,
      }, {
        'build_uwp': 0,
      }],
    ],
    'ld_lib_prefix': '<(ld_lib_prefix)',
    'ld_lib_postfix': '<(ld_lib_postfix)',
    'exe_ext': '<(exe_ext)',

    'library%': 'static_library',

  },

  'configurations': {
    'Debug': {
      'defines': [
        '_DEBUG',
      ],
    },
    'Release': {
      'defines': [
        'NDEBUG',
      ],
    },
  },
}
