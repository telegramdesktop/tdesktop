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
    'target_name': 'lib_ffmpeg',
    'type': 'static_library',
    'includes': [
      'common.gypi',
      'qt.gypi',
      'telegram_linux.gypi',
    ],
    'variables': {
      'src_loc': '../SourceFiles',
      'res_loc': '../Resources',
      'libs_loc': '../../../Libraries',
      'official_build_target%': '',
      'submodules_loc': '../ThirdParty',
    },
    'dependencies': [
      'crl.gyp:crl',
      'lib_base.gyp:lib_base',
    ],
    'export_dependent_settings': [
      'crl.gyp:crl',
      'lib_base.gyp:lib_base',
    ],
    'defines': [
    ],
    'include_dirs': [
      '<(src_loc)',
      '<(SHARED_INTERMEDIATE_DIR)',
      '<(libs_loc)/ffmpeg',
      '<(libs_loc)/range-v3/include',
      '<(submodules_loc)/GSL/include',
      '<(submodules_loc)/variant/include',
      '<(submodules_loc)/crl/src',
    ],
    'sources': [
      '<(src_loc)/ffmpeg/ffmpeg_utility.cpp',
      '<(src_loc)/ffmpeg/ffmpeg_utility.h',
    ],
    'conditions': [[ 'build_macold', {
      'xcode_settings': {
        'OTHER_CPLUSPLUSFLAGS': [ '-nostdinc++' ],
      },
      'include_dirs': [
        '/usr/local/macold/include/c++/v1',
      ],
    }]],
  }],
}
