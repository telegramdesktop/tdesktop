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
    [ 'build_mac', {
      'variables': {
        'mac_frameworks': [
          'Cocoa',
          'CoreFoundation',
          'CoreServices',
          'CoreText',
          'CoreGraphics',
          'OpenGL',
          'AudioUnit',
          'ApplicationServices',
          'Foundation',
          'AGL',
          'Security',
          'SystemConfiguration',
          'Carbon',
          'AudioToolbox',
          'CoreAudio',
          'QuartzCore',
          'AppKit',
          'CoreWLAN',
          'IOKit',
        ],
        'mac_common_flags': [
          '-pipe',
          '-g',
          '-Wall',
          '-Werror',
          '-W',
          '-fPIE',
          '-Wno-unused-variable',
          '-Wno-unused-parameter',
          '-Wno-unused-function',
          '-Wno-switch',
          '-Wno-comment',
          '-Wno-missing-field-initializers',
          '-Wno-sign-compare',
        ],
      },
      'xcode_settings': {
        'SYMROOT': '../../out',
        'OTHER_CFLAGS': [
          '<@(mac_common_flags)',
        ],
        'OTHER_CPLUSPLUSFLAGS': [
          '<@(mac_common_flags)',
        ],
        'OTHER_LDFLAGS': [
          '<!@(python -c "for s in \'<@(mac_frameworks)\'.split(\' \'): print(\'-framework \' + s)")',
        ],
        'MACOSX_DEPLOYMENT_TARGET': '<(mac_target)',
        'COMBINE_HIDPI_IMAGES': 'YES',
        'COPY_PHASE_STRIP': 'NO',
        'CLANG_CXX_LANGUAGE_STANDARD': 'c++1z',
      },
      'configurations': {
        'Debug': {
          'xcode_settings': {
            'ENABLE_TESTABILITY': 'YES',
            'ONLY_ACTIVE_ARCH': 'YES',
          },
        },
      },
      'conditions': [
        [ '"<(official_build_target)" != "" and "<(official_build_target)" != "mac" and "<(official_build_target)" != "mac32" and "<(official_build_target)" != "macstore"', {
          'sources': [ '__Wrong_Official_Build_Target__' ],
        }],
      ],
    }],
    [ 'build_macold', {
      'xcode_settings': {
        'OTHER_CPLUSPLUSFLAGS': [
          '-Wno-inconsistent-missing-override',
        ],
        'OTHER_LDFLAGS': [
          '-w', # Suppress 'libstdc++ is deprecated' warning.
        ],
      },
    }, {
      'xcode_settings': {
        'CLANG_CXX_LIBRARY': 'libc++',
        'CLANG_ENABLE_OBJC_WEAK': 'YES',
        'OTHER_LDFLAGS': [
          '-framework', 'VideoToolbox',
          '-framework', 'VideoDecodeAcceleration',
          '-framework', 'AVFoundation',
          '-framework', 'CoreMedia',
        ],
      },
    }],
  ],
}
