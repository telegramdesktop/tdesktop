# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'includes': [
    '../ThirdParty/gyp_helpers/common/common.gypi',
  ],
  'targets': [{
    'target_name': 'lib_ffmpeg',
    'includes': [
      '../ThirdParty/gyp_helpers/common/library.gypi',
      '../ThirdParty/gyp_helpers/modules/qt.gypi',
    ],
    'variables': {
      'src_loc': '../SourceFiles',
      'res_loc': '../Resources',
    },
    'dependencies': [
      '../ThirdParty/lib_base/lib_base.gyp:lib_base',
    ],
    'export_dependent_settings': [
      '../ThirdParty/lib_base/lib_base.gyp:lib_base',
    ],
    'defines': [
    ],
    'include_dirs': [
      '<(src_loc)',
      '<(libs_loc)/ffmpeg',
    ],
    'sources': [
      '<(src_loc)/ffmpeg/ffmpeg_utility.cpp',
      '<(src_loc)/ffmpeg/ffmpeg_utility.h',
    ],
    'conditions': [[ '"<(official_build_target)" != ""', {
      'defines': [
        'TDESKTOP_OFFICIAL_TARGET=<(official_build_target)',
      ],
    }], [ 'build_macold', {
      'xcode_settings': {
        'OTHER_CPLUSPLUSFLAGS': [ '-nostdinc++' ],
      },
      'include_dirs': [
        '/usr/local/macold/include/c++/v1',
      ],
    }]],
  }],
}
