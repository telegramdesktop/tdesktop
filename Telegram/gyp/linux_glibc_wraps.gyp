# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'includes': [
    'common.gypi',
  ],
  'targets': [{
    'target_name': 'linux_glibc_wraps',
    'type': 'static_library',
    'sources': [
      '../SourceFiles/platform/linux/linux_glibc_wraps.c',
    ],
    'conditions': [[ '"<!(uname -p)" == "x86_64"', {
      'sources': [
        '../SourceFiles/platform/linux/linux_glibc_wraps_64.c',
      ],
    }, {
      'sources': [
        '../SourceFiles/platform/linux/linux_glibc_wraps_32.c',
      ],
    }]],
  }],
}
