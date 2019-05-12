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
    'variables': {
      'moc_to_sources%': '1',
    },
    'moc_to_sources%': '<(moc_to_sources)',
  },
  'targets': [{
    'target_name': 'lib_lottie',
    'type': 'static_library',
    'includes': [
      'common.gypi',
      'openssl.gypi',
      'qt.gypi',
      'qt_moc.gypi',
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
      '<(lottie_loc)',
      '<(lottie_loc)/bodymovin',
      '<(lottie_loc)/imports',
      '<(lottie_helper_loc)',
      '<(submodules_loc)/GSL/include',
      '<(submodules_loc)/variant/include',
      '<(submodules_loc)/crl/src',
    ],
    'sources': [
      # interface for tdesktop
      '<(src_loc)/lottie/lottie_animation.cpp',
      '<(src_loc)/lottie/lottie_animation.h',
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
      '<(lottie_loc)/bodymovin/lottierenderer.cpp',
      '<(lottie_loc)/bodymovin/trimpath.cpp',
      '<(lottie_loc)/bodymovin/bmfilleffect.cpp',
      '<(lottie_loc)/bodymovin/bmrepeater.cpp',
      '<(lottie_loc)/bodymovin/bmrepeatertransform.cpp',
      '<(lottie_loc)/bodymovin/beziereasing.cpp',

      '<(lottie_loc)/bodymovin/beziereasing_p.h',
      '<(lottie_loc)/bodymovin/bmbase_p.h',
      '<(lottie_loc)/bodymovin/bmbasictransform_p.h',
      '<(lottie_loc)/bodymovin/bmconstants_p.h',
      '<(lottie_loc)/bodymovin/bmellipse_p.h',
      '<(lottie_loc)/bodymovin/bmfill_p.h',
      '<(lottie_loc)/bodymovin/bmfilleffect_p.h',
      '<(lottie_loc)/bodymovin/bmfreeformshape_p.h',
      '<(lottie_loc)/bodymovin/bmgfill_p.h',
      '<(lottie_loc)/bodymovin/bmgroup_p.h',
      '<(lottie_loc)/bodymovin/bmlayer_p.h',
      '<(lottie_loc)/bodymovin/bmproperty_p.h',
      '<(lottie_loc)/bodymovin/bmrect_p.h',
      '<(lottie_loc)/bodymovin/bmrepeater_p.h',
      '<(lottie_loc)/bodymovin/bmrepeatertransform_p.h',
      '<(lottie_loc)/bodymovin/bmround_p.h',
      '<(lottie_loc)/bodymovin/bmshape_p.h',
      '<(lottie_loc)/bodymovin/bmshapelayer_p.h',
      '<(lottie_loc)/bodymovin/bmshapetransform_p.h',
      '<(lottie_loc)/bodymovin/bmspatialproperty_p.h',
      '<(lottie_loc)/bodymovin/bmstroke_p.h',
      '<(lottie_loc)/bodymovin/bmtrimpath_p.h',
      '<(lottie_loc)/bodymovin/trimpath_p.h',
      '<(lottie_loc)/bodymovin/lottierenderer_p.h',
      '<(lottie_loc)/bodymovin/bmpathtrimmer_p.h',
      '<(lottie_loc)/bodymovin/bmglobal.h',

      # taken from qtlottie/src/imports/imports.pro
      '<(lottie_loc)/imports/rasterrenderer/lottierasterrenderer.cpp',

      '<(lottie_loc)/imports/rasterrenderer/lottierasterrenderer.h',

      # added to qtlottie/src/bodymovin/bodymovin.pro
      '<(lottie_loc)/bodymovin/bmasset.cpp',
      '<(lottie_loc)/bodymovin/bmprecompasset.cpp',
      '<(lottie_loc)/bodymovin/bmnulllayer.cpp',
      '<(lottie_loc)/bodymovin/bmprecomplayer.cpp',
      '<(lottie_loc)/bodymovin/bmscene.cpp',
      '<(lottie_loc)/bodymovin/bmmasks.cpp',
      '<(lottie_loc)/bodymovin/bmmaskshape.cpp',

      '<(lottie_loc)/bodymovin/bmasset_p.h',
      '<(lottie_loc)/bodymovin/bmprecompasset_p.h',
      '<(lottie_loc)/bodymovin/bmnulllayer_p.h',
      '<(lottie_loc)/bodymovin/bmprecomplayer_p.h',
      '<(lottie_loc)/bodymovin/bmscene_p.h',
      '<(lottie_loc)/bodymovin/bmmasks_p.h',
      '<(lottie_loc)/bodymovin/bmmaskshape_p.h',
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
