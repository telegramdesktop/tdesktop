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
    'target_name': 'lib_ui',
    'type': 'static_library',
    'hard_dependency': 1,
    'includes': [
      'common.gypi',
      'qt.gypi',
      'codegen_styles_rule.gypi',
      'codegen_rules_ui.gypi',
      'pch.gypi',
    ],
    'dependencies': [
      'codegen.gyp:codegen_emoji',
      'codegen.gyp:codegen_style',
      'crl.gyp:crl',
    ],
    'variables': {
      'variables': {
        'libs_loc': '../../../Libraries',
      },
      'libs_loc': '<(libs_loc)',
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
      '<(src_loc)/ui/effects/animation_value.cpp',
      '<(src_loc)/ui/effects/animation_value.h',
      '<(src_loc)/ui/effects/animations.cpp',
      '<(src_loc)/ui/effects/animations.h',
      '<(src_loc)/ui/style/style_core.cpp',
      '<(src_loc)/ui/style/style_core.h',
      '<(src_loc)/ui/style/style_core_color.cpp',
      '<(src_loc)/ui/style/style_core_color.h',
      '<(src_loc)/ui/style/style_core_direction.cpp',
      '<(src_loc)/ui/style/style_core_direction.h',
      '<(src_loc)/ui/style/style_core_font.cpp',
      '<(src_loc)/ui/style/style_core_font.h',
      '<(src_loc)/ui/style/style_core_icon.cpp',
      '<(src_loc)/ui/style/style_core_icon.h',
      '<(src_loc)/ui/style/style_core_scale.cpp',
      '<(src_loc)/ui/style/style_core_scale.h',
      '<(src_loc)/ui/style/style_core_types.cpp',
      '<(src_loc)/ui/style/style_core_types.h',
      '<(src_loc)/ui/widgets/buttons.cpp',
      '<(src_loc)/ui/widgets/buttons.h',
      '<(src_loc)/ui/abstract_button.cpp',
      '<(src_loc)/ui/abstract_button.h',
      '<(src_loc)/ui/painter.h',
      '<(src_loc)/ui/ui_integration.h',
      '<(src_loc)/ui/ui_utility.h',
      '<(emoji_suggestions_loc)/emoji_suggestions.cpp',
      '<(emoji_suggestions_loc)/emoji_suggestions.h',
    ],
  }],
}
