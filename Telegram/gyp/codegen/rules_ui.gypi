# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'actions': [{
    'action_name': 'codegen_palette',
    'inputs': [
      '<(PRODUCT_DIR)/codegen_style<(exe_ext)',
      '<(style_timestamp)',
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
}
