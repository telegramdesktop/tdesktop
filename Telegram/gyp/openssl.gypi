# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'conditions': [
    [ 'build_win', {
	  'libraries': [
        '-llibeay32',
        '-lssleay32',
        '-lCrypt32',
	  ],
      'configurations': {
        'Debug': {
          'include_dirs': [
            '<(libs_loc)/openssl/inc32',
          ],
          'library_dirs': [
            '<(libs_loc)/openssl/out32.dbg',
          ],
        },
        'Release': {
          'include_dirs': [
            '<(libs_loc)/openssl/inc32',
          ],
          'library_dirs': [
            '<(libs_loc)/openssl/out32',
          ],
        },
      },
	}], [ 'build_macold', {
      'xcode_settings': {
        'OTHER_LDFLAGS': [
          '<(libs_loc)/macold/openssl/libssl.a',
          '<(libs_loc)/macold/openssl/libcrypto.a',
        ],
      },
      'include_dirs': [
        '<(libs_loc)/macold/openssl/include',
      ],
    }], [ 'build_mac', {
      'xcode_settings': {
        'OTHER_LDFLAGS': [
          '<(libs_loc)/openssl/libssl.a',
          '<(libs_loc)/openssl/libcrypto.a',
        ],
      },
      'include_dirs': [
        '<(libs_loc)/openssl/include',
      ],
    }],
  ],
}
