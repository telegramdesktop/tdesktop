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
    'target_name': 'lib_lottie',
    'type': 'static_library',
    'includes': [
      'common.gypi',
      'openssl.gypi',
      'qt.gypi',
      'telegram_linux.gypi',
    ],
    'variables': {
      'src_loc': '../SourceFiles',
      'res_loc': '../Resources',
      'libs_loc': '../../../Libraries',
      'official_build_target%': '',
      'submodules_loc': '../ThirdParty',
      'rlottie_loc': '<(submodules_loc)/rlottie/inc',
      'lz4_loc': '<(submodules_loc)/lz4/lib',
    },
    'dependencies': [
      'crl.gyp:crl',
      'lib_base.gyp:lib_base',
      'lib_rlottie.gyp:lib_rlottie',
      'lib_ffmpeg.gyp:lib_ffmpeg',
      'lib_lz4.gyp:lib_lz4',
    ],
    'export_dependent_settings': [
      'crl.gyp:crl',
      'lib_base.gyp:lib_base',
      'lib_rlottie.gyp:lib_rlottie',
      'lib_ffmpeg.gyp:lib_ffmpeg',
      'lib_lz4.gyp:lib_lz4',
    ],
    'defines': [
      'LOT_BUILD',
    ],
    'include_dirs': [
      '<(src_loc)',
      '<(SHARED_INTERMEDIATE_DIR)',
      '<(libs_loc)/range-v3/include',
      '<(libs_loc)/zlib',
      '<(libs_loc)/ffmpeg',
      '<(rlottie_loc)',
      '<(lz4_loc)',
      '<(submodules_loc)/GSL/include',
      '<(submodules_loc)/variant/include',
      '<(submodules_loc)/crl/src',
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
