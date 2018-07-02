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
    'target_name': 'lib_export',
    'type': 'static_library',
    'includes': [
      'common.gypi',
      'qt.gypi',
      'pch.gypi',
    ],
    'variables': {
      'src_loc': '../SourceFiles',
      'libs_loc': '../../../Libraries',
      'official_build_target%': '',
      'submodules_loc': '../ThirdParty',
      'pch_source': '<(src_loc)/export/export_pch.cpp',
      'pch_header': '<(src_loc)/export/export_pch.h',
    },
    'defines': [
    ],
    'dependencies': [
      'lib_scheme.gyp:lib_scheme',
      'crl.gyp:crl',
    ],
    'export_dependent_settings': [
      'lib_scheme.gyp:lib_scheme',
    ],
    'conditions': [[ 'build_macold', {
      'xcode_settings': {
        'OTHER_CPLUSPLUSFLAGS': [ '-nostdinc++' ],
      },
      'include_dirs': [
        '/usr/local/macold/include/c++/v1',
      ],
    }]],
    'include_dirs': [
      '<(src_loc)',
      '<(SHARED_INTERMEDIATE_DIR)',
      '<(libs_loc)/range-v3/include',
      '<(submodules_loc)/GSL/include',
      '<(submodules_loc)/variant/include',
      '<(submodules_loc)/crl/src',
    ],
    'sources': [
      '<(src_loc)/export/export_api_wrap.cpp',
      '<(src_loc)/export/export_api_wrap.h',
      '<(src_loc)/export/export_controller.cpp',
      '<(src_loc)/export/export_controller.h',
      '<(src_loc)/export/export_settings.cpp',
      '<(src_loc)/export/export_settings.h',
      '<(src_loc)/export/data/export_data_types.cpp',
      '<(src_loc)/export/data/export_data_types.h',
      '<(src_loc)/export/output/export_output_abstract.cpp',
      '<(src_loc)/export/output/export_output_abstract.h',
      '<(src_loc)/export/output/export_output_file.cpp',
      '<(src_loc)/export/output/export_output_file.h',
      '<(src_loc)/export/output/export_output_html.cpp',
      '<(src_loc)/export/output/export_output_html.h',
      '<(src_loc)/export/output/export_output_json.cpp',
      '<(src_loc)/export/output/export_output_json.h',
      '<(src_loc)/export/output/export_output_result.h',
      '<(src_loc)/export/output/export_output_stats.cpp',
      '<(src_loc)/export/output/export_output_stats.h',
      '<(src_loc)/export/output/export_output_text.cpp',
      '<(src_loc)/export/output/export_output_text.h',
    ],
  }],
}
