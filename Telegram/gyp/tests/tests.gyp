# This file is part of Telegram Desktop,
# the official desktop version of Telegram messaging app, see https://telegram.org
#
# Telegram Desktop is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# It is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# In addition, as a special exception, the copyright holders give permission
# to link the code of portions of this program with the OpenSSL library.
#
# Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
# Copyright (c) 2014 John Preston, https://desktop.telegram.org

{
  'includes': [
    '../common.gypi',
  ],
  'variables': {
    'libs_loc': '../../../../Libraries',
    'src_loc': '../../SourceFiles',
    'submodules_loc': '../../ThirdParty',
    'mac_target': '10.10',
    'list_tests_command': 'python <(DEPTH)/tests/list_tests.py --input <(DEPTH)/tests/tests_list.txt',
  },
  'targets': [{
    'target_name': 'tests',
    'type': 'none',
    'includes': [
      '../common.gypi',
    ],
    'dependencies': [
      '<!@(<(list_tests_command))',
    ],
    'sources': [
      '<!@(<(list_tests_command) --sources)',
    ],
    'rules': [{
      'rule_name': 'run_tests',
      'extension': 'test',
      'inputs': [
        '<(PRODUCT_DIR)/<(RULE_INPUT_ROOT)<(exe_ext)',
      ],
      'outputs': [
        '<(SHARED_INTERMEDIATE_DIR)/<(RULE_INPUT_ROOT).timestamp',
      ],
      'action': [
        '<(PRODUCT_DIR)/<(RULE_INPUT_ROOT)<(exe_ext)',
        '--touch', '<(SHARED_INTERMEDIATE_DIR)/<(RULE_INPUT_ROOT).timestamp',
      ],
      'message': 'Running <(RULE_INPUT_ROOT)..',
    }]
  }, {
    'target_name': 'tests_flat_map',
    'includes': [
      'common_test.gypi',
    ],
    'sources': [
      '<(src_loc)/base/flat_map.h',
      '<(src_loc)/base/flat_map_tests.cpp',
    ],
  }, {
    'target_name': 'tests_flat_set',
    'includes': [
      'common_test.gypi',
    ],
    'sources': [
      '<(src_loc)/base/flat_set.h',
      '<(src_loc)/base/flat_set_tests.cpp',
    ],
  }, {
    'target_name': 'tests_flags',
    'includes': [
      'common_test.gypi',
    ],
    'sources': [
      '<(src_loc)/base/flags.h',
      '<(src_loc)/base/flags_tests.cpp',
    ],
  }],
}
