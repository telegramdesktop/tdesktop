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
  'conditions': [
    [ 'build_linux', {
      'variables': {
        'linux_common_flags': [
          '-pipe',
          '-Wall',
          '-Werror',
          '-W',
          '-fPIC',
          '-Wno-unused-variable',
          '-Wno-unused-parameter',
          '-Wno-unused-function',
          '-Wno-switch',
          '-Wno-comment',
          '-Wno-unused-but-set-variable',
          '-Wno-missing-field-initializers',
          '-Wno-sign-compare',
        ],
      },
      'conditions': [
        [ '"<!(uname -p)" == "x86_64"', {
          'defines': [
            'Q_OS_LINUX64',
          ],
          'conditions': [
            [ '"<(official_build_target)" != "" and "<(official_build_target)" != "linux"', {
              'sources': [ '__Wrong_Official_Build_Target_<(official_build_target)_' ],
            }],
          ],
        }, {
          'defines': [
            'Q_OS_LINUX32',
          ],
          'conditions': [
            [ '"<(official_build_target)" != "" and "<(official_build_target)" != "linux32"', {
              'sources': [ '__Wrong_Official_Build_Target_<(official_build_target)_' ],
            }],
          ],
        }],
      ],
      'defines': [
        '_REENTRANT',
        'QT_STATICPLUGIN',
        'QT_PLUGIN',
      ],
      'cflags_c': [
        '<@(linux_common_flags)',
        '-std=gnu11',
      ],
      'cflags_cc': [
        '<@(linux_common_flags)',
        '-std=c++1z',
        '-Wno-register',
      ],
      'configurations': {
        'Debug': {
        },
      },
    }],
  ],
}
