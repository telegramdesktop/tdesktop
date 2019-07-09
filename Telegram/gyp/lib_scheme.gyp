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
    'target_name': 'lib_scheme',
    'type': 'static_library',
    'hard_dependency': 1,
    'includes': [
      'common.gypi',
      'qt.gypi',
    ],
    'variables': {
      'src_loc': '../SourceFiles',
      'res_loc': '../Resources',
      'official_build_target%': '',
      'submodules_loc': '../ThirdParty',
    },
    'defines': [
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
      '<(submodules_loc)/GSL/include',
    ],
    'actions': [{
      'action_name': 'codegen_scheme',
      'inputs': [
        '<(src_loc)/codegen/scheme/codegen_scheme.py',
        '<(res_loc)/scheme.tl',
      ],
      'outputs': [
        '<(SHARED_INTERMEDIATE_DIR)/scheme.cpp',
        '<(SHARED_INTERMEDIATE_DIR)/scheme.h',
      ],
      'action': [
        'python', '<(src_loc)/codegen/scheme/codegen_scheme.py',
        '-o', '<(SHARED_INTERMEDIATE_DIR)', '<(res_loc)/scheme.tl',
      ],
      'message': 'codegen_scheme-ing scheme.tl..',
      'process_outputs_as_sources': 1,
    }],
  }],
}
