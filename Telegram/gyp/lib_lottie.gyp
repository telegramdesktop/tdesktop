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
    'target_name': 'lib_lottie',
    'includes': [
      'helpers/common/library.gypi',
      'helpers/modules/openssl.gypi',
      'helpers/modules/qt.gypi',
    ],
    'variables': {
      'src_loc': '../SourceFiles',
      'res_loc': '../Resources',
    },
    'dependencies': [
      '<(submodules_loc)/lib_base/lib_base.gyp:lib_base',
      'lib_rlottie.gyp:lib_rlottie',
      'lib_ffmpeg.gyp:lib_ffmpeg',
      'lib_lz4.gyp:lib_lz4',
    ],
    'export_dependent_settings': [
      '<(submodules_loc)/lib_base/lib_base.gyp:lib_base',
      'lib_rlottie.gyp:lib_rlottie',
      'lib_ffmpeg.gyp:lib_ffmpeg',
      'lib_lz4.gyp:lib_lz4',
    ],
    'defines': [
      'LOT_BUILD',
    ],
    'include_dirs': [
      '<(src_loc)',
      '<(libs_loc)/zlib',
    ],
    'sources': [
      '<(src_loc)/lottie/lottie_animation.cpp',
      '<(src_loc)/lottie/lottie_animation.h',
      '<(src_loc)/lottie/lottie_cache.cpp',
      '<(src_loc)/lottie/lottie_cache.h',
      '<(src_loc)/lottie/lottie_common.cpp',
      '<(src_loc)/lottie/lottie_common.h',
      '<(src_loc)/lottie/lottie_frame_renderer.cpp',
      '<(src_loc)/lottie/lottie_frame_renderer.h',
      '<(src_loc)/lottie/lottie_multi_player.cpp',
      '<(src_loc)/lottie/lottie_multi_player.h',
      '<(src_loc)/lottie/lottie_player.h',
      '<(src_loc)/lottie/lottie_single_player.cpp',
      '<(src_loc)/lottie/lottie_single_player.h',
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
