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
      'pch.gypi',
    ],
    'variables': {
      'src_loc': '../SourceFiles',
      'res_loc': '../Resources',
      'libs_loc': '../../../Libraries',
      'official_build_target%': '',
      'submodules_loc': '../ThirdParty',
      'lottie_loc': '<(submodules_loc)/qtlottie/src',
      'lottie_helper_loc': '<(submodules_loc)/qtlottie_helper',
      'pch_source': '<(src_loc)/lottie/lottie_pch.cpp',
      'pch_header': '<(src_loc)/lottie/lottie_pch.h',
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
      'BODYMOVIN_LIBRARY',
    ],
    'include_dirs': [
      '<(src_loc)',
      '<(SHARED_INTERMEDIATE_DIR)',
      '<(libs_loc)/range-v3/include',
      '<(libs_loc)/zlib',
      '<(lottie_loc)',
      '<(lottie_loc)/bodymovin',
      '<(lottie_loc)/imports',
      '<(lottie_helper_loc)',
      '<(submodules_loc)/GSL/include',
      '<(submodules_loc)/variant/include',
      '<(submodules_loc)/crl/src',
      '<(submodules_loc)/rapidjson/include',
    ],
    'sources': [
      # interface for tdesktop
      '<(src_loc)/lottie/lottie_animation.cpp',
      '<(src_loc)/lottie/lottie_animation.h',
      '<(src_loc)/lottie/lottie_common.h',
      '<(src_loc)/lottie/lottie_frame_renderer.cpp',
      '<(src_loc)/lottie/lottie_frame_renderer.h',

      # taken from qtlottie/src/bodymovin/bodymovin.pro
      '<(lottie_loc)/bodymovin/bmbase.cpp',
      '<(lottie_loc)/bodymovin/bmlayer.cpp',
      '<(lottie_loc)/bodymovin/bmshape.cpp',
      '<(lottie_loc)/bodymovin/bmshapelayer.cpp',
      '<(lottie_loc)/bodymovin/bmrect.cpp',
      '<(lottie_loc)/bodymovin/bmfill.cpp',
      '<(lottie_loc)/bodymovin/bmgfill.cpp',
      '<(lottie_loc)/bodymovin/bmgroup.cpp',
      '<(lottie_loc)/bodymovin/bmstroke.cpp',
      '<(lottie_loc)/bodymovin/bmbasictransform.cpp',
      '<(lottie_loc)/bodymovin/bmshapetransform.cpp',
      '<(lottie_loc)/bodymovin/bmellipse.cpp',
      '<(lottie_loc)/bodymovin/bmround.cpp',
      '<(lottie_loc)/bodymovin/bmfreeformshape.cpp',
      '<(lottie_loc)/bodymovin/bmtrimpath.cpp',
      '<(lottie_loc)/bodymovin/bmpathtrimmer.cpp',
      '<(lottie_loc)/bodymovin/freeformshape.cpp',
      '<(lottie_loc)/bodymovin/renderer.cpp',
      '<(lottie_loc)/bodymovin/trimpath.cpp',
      '<(lottie_loc)/bodymovin/bmfilleffect.cpp',
      '<(lottie_loc)/bodymovin/bmrepeater.cpp',
      '<(lottie_loc)/bodymovin/bmrepeatertransform.cpp',
      '<(lottie_loc)/bodymovin/beziereasing.cpp',

      '<(lottie_loc)/bodymovin/beziereasing.h',
      '<(lottie_loc)/bodymovin/bmbase.h',
      '<(lottie_loc)/bodymovin/bmbasictransform.h',
      '<(lottie_loc)/bodymovin/bmellipse.h',
      '<(lottie_loc)/bodymovin/bmfill.h',
      '<(lottie_loc)/bodymovin/bmfilleffect.h',
      '<(lottie_loc)/bodymovin/bmfreeformshape.h',
      '<(lottie_loc)/bodymovin/bmgfill.h',
      '<(lottie_loc)/bodymovin/bmgroup.h',
      '<(lottie_loc)/bodymovin/bmlayer.h',
      '<(lottie_loc)/bodymovin/bmproperty.h',
      '<(lottie_loc)/bodymovin/bmrect.h',
      '<(lottie_loc)/bodymovin/bmrepeater.h',
      '<(lottie_loc)/bodymovin/bmrepeatertransform.h',
      '<(lottie_loc)/bodymovin/bmround.h',
      '<(lottie_loc)/bodymovin/bmshape.h',
      '<(lottie_loc)/bodymovin/bmshapelayer.h',
      '<(lottie_loc)/bodymovin/bmshapetransform.h',
      '<(lottie_loc)/bodymovin/bmstroke.h',
      '<(lottie_loc)/bodymovin/bmtrimpath.h',
      '<(lottie_loc)/bodymovin/freeformshape.h',
      '<(lottie_loc)/bodymovin/trimpath.h',
      '<(lottie_loc)/bodymovin/renderer.h',
      '<(lottie_loc)/bodymovin/bmpathtrimmer.h',

      # taken from qtlottie/src/imports/imports.pro
      '<(lottie_loc)/imports/rasterrenderer/rasterrenderer.cpp',

      '<(lottie_loc)/imports/rasterrenderer/rasterrenderer.h',

      # added to qtlottie/src/bodymovin/bodymovin.pro
      '<(lottie_loc)/bodymovin/bmasset.cpp',
      '<(lottie_loc)/bodymovin/bmprecompasset.cpp',
      '<(lottie_loc)/bodymovin/bmnulllayer.cpp',
      '<(lottie_loc)/bodymovin/bmprecomplayer.cpp',
      '<(lottie_loc)/bodymovin/bmscene.cpp',
      '<(lottie_loc)/bodymovin/bmmasks.cpp',
      '<(lottie_loc)/bodymovin/bmmaskshape.cpp',

      '<(lottie_loc)/bodymovin/bmasset.h',
      '<(lottie_loc)/bodymovin/bmprecompasset.h',
      '<(lottie_loc)/bodymovin/bmnulllayer.h',
      '<(lottie_loc)/bodymovin/bmprecomplayer.h',
      '<(lottie_loc)/bodymovin/bmscene.h',
      '<(lottie_loc)/bodymovin/bmmasks.h',
      '<(lottie_loc)/bodymovin/bmmaskshape.h',

	  '<(lottie_loc)/bodymovin/json.h',
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
