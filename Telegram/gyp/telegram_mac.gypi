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
      'INFOPLIST_FILE': '../Telegram.plist',
      'PRODUCT_BUNDLE_IDENTIFIER': 'com.tdesktop.Telegram',
      'TDESKTOP_VERSION': '0.10.2',
      'OTHER_LDFLAGS': [
        '-lcups',
        '-lbsm',
        '-lm',
        '-lssl',
        '-lcrypto',
        '/usr/local/lib/liblzma.a',
        '/usr/local/lib/libopenal.a',
        '/usr/local/lib/libopus.a',
        '/usr/local/lib/libexif.a',
        '-lavcodec',
        '-lavformat',
        '-lswscale',
        '-lswresample',
        '-lavutil',
        '/usr/local/lib/libiconv.a',
        '-lbase',
        '-lcrashpad_client',
        '-lcrashpad_util',
      ],
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
        'xcode_settings': {
          'GCC_OPTIMIZATION_LEVEL': '0',
        },
      },
      'Release': {
        'library_dirs': [
          '<(libs_loc)/crashpad/crashpad/out/Release',
        ],
        'xcode_settings': {
          'DEBUG_INFORMATION_FORMAT': 'dwarf-with-dsym',
          'LLVM_LTO': 'YES',
          'GCC_OPTIMIZATION_LEVEL': 'fast',
        },
      },
    },
    'mac_bundle': '1',
    'mac_bundle_resources': [
      '<!@(python -c "for s in \'<@(langpacks)\'.split(\' \'): print(\'<(res_loc)/langs/\' + s + \'.lproj/Localizable.strings\')")',
      '../Telegram/Images.xcassets'
    ],
    'postbuilds': [{
      'postbuild_name': 'Force Frameworks path',
      'action': [
        'mkdir', '-p', '${BUILT_PRODUCTS_DIR}/Telegram.app/Contents/Frameworks/'
      ],
    }, {
      'postbuild_name': 'Copy Updater to Frameworks',
      'action': [
        'cp',
        '${BUILT_PRODUCTS_DIR}/Updater',
        '${BUILT_PRODUCTS_DIR}/Telegram.app/Contents/Frameworks/',
      ],
    }, {
      'postbuild_name': 'Force Helpers path',
      'action': [
        'mkdir', '-p', '${BUILT_PRODUCTS_DIR}/Telegram.app/Contents/Helpers/'
      ],
    }, {
      'postbuild_name': 'Copy crashpad_client to Helpers',
      'action': [
        'cp',
        '<(libs_loc)/crashpad/crashpad/out/${CONFIGURATION}/crashpad_handler',
        '${BUILT_PRODUCTS_DIR}/Telegram.app/Contents/Helpers/',
      ],
    }],
  }]],
}
