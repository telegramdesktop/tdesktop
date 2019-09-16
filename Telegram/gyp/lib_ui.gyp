# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'includes': [
    'common/common.gypi',
  ],
  'targets': [{
    'target_name': 'lib_ui',
    'hard_dependency': 1,
    'includes': [
      'common/library.gypi',
      'modules/qt.gypi',
      'modules/qt_moc.gypi',
      'modules/pch.gypi',
      'modules/openssl.gypi',
      'codegen/styles_rule.gypi',
      'codegen/rules_ui.gypi',
    ],
    'dependencies': [
      'codegen.gyp:codegen_emoji',
      'codegen.gyp:codegen_style',
      'crl.gyp:crl',
    ],
    'variables': {
      'src_loc': '../SourceFiles',
      'res_loc': '../Resources',
      'official_build_target%': '',
      'submodules_loc': '../ThirdParty',
      'emoji_suggestions_loc': '<(submodules_loc)/emoji_suggestions',
      'style_files': [
        '<(res_loc)/colors.palette',
        '<(res_loc)/basic.style',
        '<(src_loc)/ui/widgets/widgets.style',
      ],
      'dependent_style_files': [
      ],
      'style_timestamp': '<(SHARED_INTERMEDIATE_DIR)/update_dependent_styles_ui.timestamp',
      'list_sources_command': 'python <(DEPTH)/list_sources.py --input <(DEPTH)/lib_ui/sources.txt --replace src_loc=<(src_loc)',
      'pch_source': '<(src_loc)/ui/ui_pch.cpp',
      'pch_header': '<(src_loc)/ui/ui_pch.h',
    },
    'defines': [
    ],
    'include_dirs': [
      '<(src_loc)',
      '<(SHARED_INTERMEDIATE_DIR)',
      '<(libs_loc)/range-v3/include',
      '<(submodules_loc)/GSL/include',
      '<(submodules_loc)/variant/include',
      '<(submodules_loc)/crl/src',
      '<(emoji_suggestions_loc)',
    ],
    'sources': [
      '<@(style_files)',
      '<!@(<(list_sources_command) <(qt_moc_list_sources_arg))',
      '<(DEPTH)/lib_ui/sources.txt',
    ],
    'sources!': [
      '<!@(<(list_sources_command) <(qt_moc_list_sources_arg) --exclude_for <(build_os))',
    ],
  }],
}
