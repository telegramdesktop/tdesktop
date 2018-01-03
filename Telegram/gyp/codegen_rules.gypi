# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'actions': [{
    'action_name': 'update_dependent_styles',
    'inputs': [
      '<(DEPTH)/update_dependent.py',
      '<@(style_files)',
    ],
    'outputs': [
      '<(SHARED_INTERMEDIATE_DIR)/update_dependent_styles.timestamp',
    ],
    'action': [
      'python', '<(DEPTH)/update_dependent.py', '--styles',
      '-I', '<(res_loc)', '-I', '<(src_loc)',
      '-o', '<(SHARED_INTERMEDIATE_DIR)/update_dependent_styles.timestamp',
      '<@(style_files)',
    ],
    'message': 'Updating dependent style files..',
  }, {
    'action_name': 'update_dependent_qrc',
    'inputs': [
      '<(DEPTH)/update_dependent.py',
      '<@(qrc_files)',
      '<!@(python <(DEPTH)/update_dependent.py --qrc_list <@(qrc_files))',
    ],
    'outputs': [
      '<(SHARED_INTERMEDIATE_DIR)/update_dependent_qrc.timestamp',
    ],
    'action': [
      'python', '<(DEPTH)/update_dependent.py', '--qrc',
      '-o', '<(SHARED_INTERMEDIATE_DIR)/update_dependent_qrc.timestamp',
      '<@(qrc_files)',
    ],
    'message': 'Updating dependent qrc files..',
  }, {
    'action_name': 'codegen_palette',
    'inputs': [
      '<(PRODUCT_DIR)/codegen_style<(exe_ext)',
      '<(SHARED_INTERMEDIATE_DIR)/update_dependent_styles.timestamp',
      '<(res_loc)/colors.palette',
    ],
    'outputs': [
      '<(SHARED_INTERMEDIATE_DIR)/styles/palette.h',
      '<(SHARED_INTERMEDIATE_DIR)/styles/palette.cpp',
    ],
    'action': [
      '<(PRODUCT_DIR)/codegen_style<(exe_ext)',
      '-I', '<(res_loc)', '-I', '<(src_loc)',
      '-o', '<(SHARED_INTERMEDIATE_DIR)/styles',
      '-w', '<(PRODUCT_DIR)/..',

      # GYP/Ninja bug workaround: if we specify just <(RULE_INPUT_PATH)
      # the <(RULE_INPUT_ROOT) variables won't be available in Ninja,
      # and the 'message' will be just 'codegen_style-ing .style..'
      # Looks like the using the <(RULE_INPUT_ROOT) here "exports" it
      # for using in the 'message' field.

      '<(res_loc)/colors.palette',
    ],
    'message': 'codegen_palette-ing colors..',
    'process_outputs_as_sources': 1,
  }, {
    'action_name': 'codegen_lang',
    'inputs': [
      '<(PRODUCT_DIR)/codegen_lang<(exe_ext)',
      '<(res_loc)/langs/lang.strings',
    ],
    'outputs': [
      '<(SHARED_INTERMEDIATE_DIR)/lang_auto.cpp',
      '<(SHARED_INTERMEDIATE_DIR)/lang_auto.h',
    ],
    'action': [
      '<(PRODUCT_DIR)/codegen_lang<(exe_ext)',
      '-o', '<(SHARED_INTERMEDIATE_DIR)', '<(res_loc)/langs/lang.strings',
      '-w', '<(PRODUCT_DIR)/..',
    ],
    'message': 'codegen_lang-ing lang.strings..',
    'process_outputs_as_sources': 1,
  }, {
    'action_name': 'codegen_numbers',
    'inputs': [
      '<(PRODUCT_DIR)/codegen_numbers<(exe_ext)',
      '<(res_loc)/numbers.txt',
    ],
    'outputs': [
      '<(SHARED_INTERMEDIATE_DIR)/numbers.cpp',
      '<(SHARED_INTERMEDIATE_DIR)/numbers.h',
    ],
    'action': [
      '<(PRODUCT_DIR)/codegen_numbers<(exe_ext)',
      '-o', '<(SHARED_INTERMEDIATE_DIR)', '<(res_loc)/numbers.txt',
      '-w', '<(PRODUCT_DIR)/..',
    ],
    'message': 'codegen_numbers-ing numbers.txt..',
    'process_outputs_as_sources': 1,
  }, {
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
  }, {
    'action_name': 'codegen_emoji',
    'inputs': [
      '<(PRODUCT_DIR)/codegen_emoji<(exe_ext)',
      '<(res_loc)/emoji_autocomplete.json',
    ],
    'outputs': [
      '<(SHARED_INTERMEDIATE_DIR)/emoji.cpp',
      '<(SHARED_INTERMEDIATE_DIR)/emoji.h',
      '<(SHARED_INTERMEDIATE_DIR)/emoji_suggestions_data.cpp',
      '<(SHARED_INTERMEDIATE_DIR)/emoji_suggestions_data.h',
    ],
    'action': [
      '<(PRODUCT_DIR)/codegen_emoji<(exe_ext)',
      '<(res_loc)/emoji_autocomplete.json',
      '-o', '<(SHARED_INTERMEDIATE_DIR)',
    ],
    'message': 'codegen_emoji-ing..',
    'process_outputs_as_sources': 1,
  }],
  'rules': [{
    'rule_name': 'codegen_style',
    'extension': 'style',
    'inputs': [
      '<(PRODUCT_DIR)/codegen_style<(exe_ext)',
      '<(SHARED_INTERMEDIATE_DIR)/update_dependent_styles.timestamp',
    ],
    'outputs': [
      '<(SHARED_INTERMEDIATE_DIR)/styles/style_<(RULE_INPUT_ROOT).h',
      '<(SHARED_INTERMEDIATE_DIR)/styles/style_<(RULE_INPUT_ROOT).cpp',
    ],
    'action': [
      '<(PRODUCT_DIR)/codegen_style<(exe_ext)',
      '-I', '<(res_loc)', '-I', '<(src_loc)',
      '-o', '<(SHARED_INTERMEDIATE_DIR)/styles',
      '-w', '<(PRODUCT_DIR)/..',

      # GYP/Ninja bug workaround: if we specify just <(RULE_INPUT_PATH)
      # the <(RULE_INPUT_ROOT) variables won't be available in Ninja,
      # and the 'message' will be just 'codegen_style-ing .style..'
      # Looks like the using the <(RULE_INPUT_ROOT) here "exports" it
      # for using in the 'message' field.

      '<(RULE_INPUT_DIRNAME)/<(RULE_INPUT_ROOT)<(RULE_INPUT_EXT)',
    ],
    'message': 'codegen_style-ing <(RULE_INPUT_ROOT).style..',
    'process_outputs_as_sources': 1,
  }],
}
