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
      '<@(dependent_style_files)',
    ],
    'outputs': [
      '<(style_timestamp)',
    ],
    'action': [
      'python', '<(DEPTH)/update_dependent.py', '--styles',
      '-I', '<(res_loc)', '-I', '<(src_loc)',
      '-o', '<(style_timestamp)',
      '<@(style_files)',
    ],
    'message': 'Updating dependent style files..',
  }],
  'rules': [{
    'rule_name': 'codegen_style',
    'extension': 'style',
    'inputs': [
      '<(PRODUCT_DIR)/codegen_style<(exe_ext)',
      '<(style_timestamp)',
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
