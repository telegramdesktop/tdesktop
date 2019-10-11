# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'includes': [
    '../helpers/common/common.gypi',
  ],
  'variables': {
    'src_loc': '../../SourceFiles',
    'base_loc': '<(submodules_loc)/lib_base',
    'rpl_loc': '<(submodules_loc)/lib_rpl',
    'mac_target': '10.12',
    'list_tests_command': 'python <(DEPTH)/tests/list_tests.py --input <(DEPTH)/tests/tests_list.txt',
  },
  'targets': [{
    'target_name': 'tests',
    'type': 'none',
    'includes': [
      '../helpers/common/common.gypi',
    ],
    'dependencies': [
      '<!@(<(list_tests_command))',
      'tests_storage',
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
    'target_name': 'tests_algorithm',
    'includes': [
      'common_test.gypi',
    ],
    'sources': [
      '<(base_loc)/base/algorithm.h',
      '<(base_loc)/base/algorithm_tests.cpp',
    ],
  }, {
    'target_name': 'tests_flags',
    'includes': [
      'common_test.gypi',
    ],
    'sources': [
      '<(base_loc)/base/flags.h',
      '<(base_loc)/base/flags_tests.cpp',
    ],
  }, {
    'target_name': 'tests_flat_map',
    'includes': [
      'common_test.gypi',
    ],
    'sources': [
      '<(base_loc)/base/flat_map.h',
      '<(base_loc)/base/flat_map_tests.cpp',
    ],
  }, {
    'target_name': 'tests_flat_set',
    'includes': [
      'common_test.gypi',
    ],
    'sources': [
      '<(base_loc)/base/flat_set.h',
      '<(base_loc)/base/flat_set_tests.cpp',
    ],
  }, {
    'target_name': 'tests_rpl',
    'includes': [
      'common_test.gypi',
    ],
    'sources': [
      '<(rpl_loc)/rpl/details/callable.h',
      '<(rpl_loc)/rpl/details/superset_type.h',
      '<(rpl_loc)/rpl/details/type_list.h',
      '<(rpl_loc)/rpl/after_next.h',
      '<(rpl_loc)/rpl/before_next.h',
      '<(rpl_loc)/rpl/combine.h',
      '<(rpl_loc)/rpl/combine_previous.h',
      '<(rpl_loc)/rpl/complete.h',
      '<(rpl_loc)/rpl/conditional.h',
      '<(rpl_loc)/rpl/consumer.h',
      '<(rpl_loc)/rpl/deferred.h',
      '<(rpl_loc)/rpl/distinct_until_changed.h',
      '<(rpl_loc)/rpl/event_stream.h',
      '<(rpl_loc)/rpl/fail.h',
      '<(rpl_loc)/rpl/filter.h',
      '<(rpl_loc)/rpl/flatten_latest.h',
      '<(rpl_loc)/rpl/lifetime.h',
      '<(rpl_loc)/rpl/map.h',
      '<(rpl_loc)/rpl/mappers.h',
      '<(rpl_loc)/rpl/merge.h',
      '<(rpl_loc)/rpl/never.h',
      '<(rpl_loc)/rpl/operators_tests.cpp',
      '<(rpl_loc)/rpl/producer.h',
      '<(rpl_loc)/rpl/producer_tests.cpp',
      '<(rpl_loc)/rpl/range.h',
      '<(rpl_loc)/rpl/rpl.h',
      '<(rpl_loc)/rpl/skip.h',
      '<(rpl_loc)/rpl/take.h',
      '<(rpl_loc)/rpl/then.h',
      '<(rpl_loc)/rpl/type_erased.h',
      '<(rpl_loc)/rpl/variable.h',
      '<(rpl_loc)/rpl/variable_tests.cpp',
    ],
  }, {
    'target_name': 'tests_storage',
    'includes': [
      'common_test.gypi',
      '../helpers/modules/openssl.gypi',
    ],
    'dependencies': [
      '../lib_storage.gyp:lib_storage',
    ],
    'sources': [
      '<(src_loc)/storage/storage_encrypted_file_tests.cpp',
      '<(src_loc)/storage/cache/storage_cache_database_tests.cpp',
      '<(src_loc)/platform/win/windows_dlls.cpp',
      '<(src_loc)/platform/win/windows_dlls.h',
    ],
    'conditions': [[ 'not build_win', {
      'sources!': [
        '<(src_loc)/platform/win/windows_dlls.cpp',
        '<(src_loc)/platform/win/windows_dlls.h',
      ],
    }], [ 'build_linux', {
      'libraries': [
        '<(linux_lib_ssl)',
        '<(linux_lib_crypto)',
      ],
    }]],
  }],
}
