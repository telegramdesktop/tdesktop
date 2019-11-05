# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'includes': [
    'helpers/common/common.gypi',
  ],
  'targets': [{
    'target_name': 'lib_lz4',
    'includes': [
      'helpers/common/library.gypi',
    ],
    'variables': {
      'lz4_loc': '<(third_party_loc)/lz4/lib',
    },
    'defines': [
    ],
    'include_dirs': [
      '<(lz4_loc)',
    ],
    'direct_dependent_settings': {
      'include_dirs': [
        '<(lz4_loc)',
      ],
    },
    'sources': [
      '<(lz4_loc)/lz4.c',
      '<(lz4_loc)/lz4.h',
      '<(lz4_loc)/lz4frame.c',
      '<(lz4_loc)/lz4frame.h',
      '<(lz4_loc)/lz4frame_static.h',
      '<(lz4_loc)/lz4hc.c',
      '<(lz4_loc)/lz4hc.h',
      '<(lz4_loc)/xxhash.c',
      '<(lz4_loc)/xxhash.h',
    ],
  }],
}
