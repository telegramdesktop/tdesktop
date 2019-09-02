# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'includes': [
    'common.gypi',
  ],
  'variables': {
    'build_standard_win': 'c++14',
  },
  'targets': [{
    'target_name': 'lib_rlottie',
    'type': 'static_library',
    'includes': [
      'common.gypi',
      'telegram_linux.gypi',
    ],
    'variables': {
      'official_build_target%': '',
      'build_standard_win': 'c++14',
      'submodules_loc': '../ThirdParty',
      'libs_loc': '../../../Libraries',
      'rlottie_loc': '<(submodules_loc)/rlottie',
      'rlottie_src': '<(rlottie_loc)/src',
    },
    'defines': [
      '_USE_MATH_DEFINES',
      'RAPIDJSON_ASSERT=(void)',
      'LOT_BUILD',
    ],
    'include_dirs': [
      '<(rlottie_loc)/inc',
      '<(rlottie_src)/lottie',
      '<(rlottie_src)/vector',
      '<(rlottie_src)/vector/pixman',
      '<(rlottie_src)/vector/freetype',
    ],
    'sources': [
      '<(rlottie_loc)/inc/rlottie.h',
      '<(rlottie_loc)/inc/rlottie_capi.h',
      '<(rlottie_loc)/inc/rlottiecommon.h',

      '<(rlottie_src)/lottie/lottieanimation.cpp',
      '<(rlottie_src)/lottie/lottieitem.cpp',
      '<(rlottie_src)/lottie/lottieitem.h',
      '<(rlottie_src)/lottie/lottiekeypath.cpp',
      '<(rlottie_src)/lottie/lottiekeypath.h',
      '<(rlottie_src)/lottie/lottieloader.cpp',
      '<(rlottie_src)/lottie/lottieloader.h',
      '<(rlottie_src)/lottie/lottiemodel.cpp',
      '<(rlottie_src)/lottie/lottiemodel.h',
      '<(rlottie_src)/lottie/lottieparser.cpp',
      '<(rlottie_src)/lottie/lottieparser.h',
      '<(rlottie_src)/lottie/lottieproxymodel.cpp',
      '<(rlottie_src)/lottie/lottieproxymodel.h',

      '<(rlottie_src)/vector/freetype/v_ft_math.cpp',
      '<(rlottie_src)/vector/freetype/v_ft_math.h',
      '<(rlottie_src)/vector/freetype/v_ft_raster.cpp',
      '<(rlottie_src)/vector/freetype/v_ft_raster.h',
      '<(rlottie_src)/vector/freetype/v_ft_stroker.cpp',
      '<(rlottie_src)/vector/freetype/v_ft_stroker.h',
      '<(rlottie_src)/vector/freetype/v_ft_types.h',

      #'<(rlottie_src)/vector/pixman/pixman-arm-neon-asm.h',
      #'<(rlottie_src)/vector/pixman/pixman-arm-neon-asm.S',
      '<(rlottie_src)/vector/pixman/vregion.cpp',
      '<(rlottie_src)/vector/pixman/vregion.h',

      '<(rlottie_src)/vector/config.h',
      '<(rlottie_src)/vector/vbezier.cpp',
      '<(rlottie_src)/vector/vbezier.h',
      '<(rlottie_src)/vector/vbitmap.cpp',
      '<(rlottie_src)/vector/vbitmap.h',
      '<(rlottie_src)/vector/vbrush.cpp',
      '<(rlottie_src)/vector/vbrush.h',
      '<(rlottie_src)/vector/vcompositionfunctions.cpp',
      '<(rlottie_src)/vector/vcowptr.h',
      '<(rlottie_src)/vector/vdasher.cpp',
      '<(rlottie_src)/vector/vdasher.h',
      '<(rlottie_src)/vector/vdebug.cpp',
      '<(rlottie_src)/vector/vdebug.h',
      '<(rlottie_src)/vector/vdrawable.cpp',
      '<(rlottie_src)/vector/vdrawable.h',
      '<(rlottie_src)/vector/vdrawhelper.cpp',
      '<(rlottie_src)/vector/vdrawhelper.h',
      '<(rlottie_src)/vector/vdrawhelper_neon.cpp',
      '<(rlottie_src)/vector/vdrawhelper_sse2.cpp',
      '<(rlottie_src)/vector/velapsedtimer.cpp',
      '<(rlottie_src)/vector/velapsedtimer.h',
      '<(rlottie_src)/vector/vglobal.h',
      '<(rlottie_src)/vector/vimageloader.cpp',
      '<(rlottie_src)/vector/vimageloader.h',
      '<(rlottie_src)/vector/vinterpolator.cpp',
      '<(rlottie_src)/vector/vinterpolator.h',
      '<(rlottie_src)/vector/vline.h',
      '<(rlottie_src)/vector/vmatrix.cpp',
      '<(rlottie_src)/vector/vmatrix.h',
      '<(rlottie_src)/vector/vpainter.cpp',
      '<(rlottie_src)/vector/vpainter.h',
      '<(rlottie_src)/vector/vpath.cpp',
      '<(rlottie_src)/vector/vpath.h',
      '<(rlottie_src)/vector/vpathmesure.cpp',
      '<(rlottie_src)/vector/vpathmesure.h',
      '<(rlottie_src)/vector/vpoint.h',
      '<(rlottie_src)/vector/vraster.cpp',
      '<(rlottie_src)/vector/vraster.h',
      '<(rlottie_src)/vector/vrect.cpp',
      '<(rlottie_src)/vector/vrect.h',
      '<(rlottie_src)/vector/vrle.cpp',
      '<(rlottie_src)/vector/vrle.h',
      '<(rlottie_src)/vector/vstackallocator.h',
      '<(rlottie_src)/vector/vtaskqueue.h',
    ],
    'conditions': [[ 'build_macold', {
      'xcode_settings': {
        'OTHER_CPLUSPLUSFLAGS': [ '-nostdinc++' ],
      },
      'include_dirs': [
        '/usr/local/macold/include/c++/v1',
      ],
    }]],
    'msvs_settings': {
      'VCCLCompilerTool': {
        'AdditionalOptions': [
          '/w44244', # 'initializing': conversion from 'double' to 'float'
        ],
      },
    },
  }],
}
