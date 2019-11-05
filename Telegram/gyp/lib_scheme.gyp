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
    'target_name': 'lib_scheme',
    'hard_dependency': 1,
    'includes': [
      'helpers/common/library.gypi',
      'helpers/modules/qt.gypi',
    ],
    'variables': {
      'src_loc': '../SourceFiles',
      'res_loc': '../Resources',
    },
    'defines': [
    ],
    'dependencies': [
      '<(submodules_loc)/lib_base/lib_base.gyp:lib_base',
      '<(submodules_loc)/lib_tl/lib_tl.gyp:lib_tl',
    ],
    'export_dependent_settings': [
      '<(submodules_loc)/lib_base/lib_base.gyp:lib_base',
      '<(submodules_loc)/lib_tl/lib_tl.gyp:lib_tl',
    ],
    'include_dirs': [
      '<(src_loc)',
      '<(SHARED_INTERMEDIATE_DIR)',
      '<(submodules_loc)/GSL/include',
    ],
    'direct_dependent_settings': {
      'include_dirs': [
        '<(SHARED_INTERMEDIATE_DIR)',
      ],
    },
    'actions': [{
      'action_name': 'codegen_scheme',
      'inputs': [
        '<(src_loc)/codegen/scheme/codegen_scheme.py',
        '<(submodules_loc)/lib_tl/tl/generate_tl.py',
        '<(res_loc)/tl/mtproto.tl',
        '<(res_loc)/tl/api.tl',
      ],
      'outputs': [
        '<(SHARED_INTERMEDIATE_DIR)/scheme.cpp',
        '<(SHARED_INTERMEDIATE_DIR)/scheme.h',
      ],
      'action': [
        'python', '<(src_loc)/codegen/scheme/codegen_scheme.py',
        '-o', '<(SHARED_INTERMEDIATE_DIR)/scheme',
        '<(res_loc)/tl/mtproto.tl',
        '<(res_loc)/tl/api.tl',
      ],
      'message': 'codegen_scheme-ing *.tl..',
      'process_outputs_as_sources': 1,
    }],
  }],
}
