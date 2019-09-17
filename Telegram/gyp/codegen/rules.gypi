# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'actions': [{
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
  }],
}
