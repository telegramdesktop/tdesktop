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
    'target_name': 'codegen_lang',
    'variables': {
      'src_loc': '../SourceFiles',
      'mac_target': '10.10',
    },
    'includes': [
      'common_executable.gypi',
      'qt.gypi',
    ],

    'include_dirs': [
      '<(src_loc)',
    ],
    'sources': [
      '<(src_loc)/codegen/common/basic_tokenized_file.cpp',
      '<(src_loc)/codegen/common/basic_tokenized_file.h',
      '<(src_loc)/codegen/common/checked_utf8_string.cpp',
      '<(src_loc)/codegen/common/checked_utf8_string.h',
      '<(src_loc)/codegen/common/clean_file.cpp',
      '<(src_loc)/codegen/common/clean_file.h',
      '<(src_loc)/codegen/common/clean_file_reader.h',
      '<(src_loc)/codegen/common/const_utf8_string.h',
      '<(src_loc)/codegen/common/cpp_file.cpp',
      '<(src_loc)/codegen/common/cpp_file.h',
      '<(src_loc)/codegen/common/logging.cpp',
      '<(src_loc)/codegen/common/logging.h',
      '<(src_loc)/codegen/lang/generator.cpp',
      '<(src_loc)/codegen/lang/generator.h',
      '<(src_loc)/codegen/lang/main.cpp',
      '<(src_loc)/codegen/lang/options.cpp',
      '<(src_loc)/codegen/lang/options.h',
      '<(src_loc)/codegen/lang/parsed_file.cpp',
      '<(src_loc)/codegen/lang/parsed_file.h',
      '<(src_loc)/codegen/lang/processor.cpp',
      '<(src_loc)/codegen/lang/processor.h',
    ],
  }, {
    'target_name': 'codegen_style',
    'variables': {
      'src_loc': '../SourceFiles',
      'mac_target': '10.10',
    },
    'includes': [
      'common_executable.gypi',
      'qt.gypi',
    ],

    'include_dirs': [
      '<(src_loc)',
    ],
    'sources': [
      '<(src_loc)/codegen/common/basic_tokenized_file.cpp',
      '<(src_loc)/codegen/common/basic_tokenized_file.h',
      '<(src_loc)/codegen/common/checked_utf8_string.cpp',
      '<(src_loc)/codegen/common/checked_utf8_string.h',
      '<(src_loc)/codegen/common/clean_file.cpp',
      '<(src_loc)/codegen/common/clean_file.h',
      '<(src_loc)/codegen/common/clean_file_reader.h',
      '<(src_loc)/codegen/common/const_utf8_string.h',
      '<(src_loc)/codegen/common/cpp_file.cpp',
      '<(src_loc)/codegen/common/cpp_file.h',
      '<(src_loc)/codegen/common/logging.cpp',
      '<(src_loc)/codegen/common/logging.h',
      '<(src_loc)/codegen/style/generator.cpp',
      '<(src_loc)/codegen/style/generator.h',
      '<(src_loc)/codegen/style/main.cpp',
      '<(src_loc)/codegen/style/module.cpp',
      '<(src_loc)/codegen/style/module.h',
      '<(src_loc)/codegen/style/options.cpp',
      '<(src_loc)/codegen/style/options.h',
      '<(src_loc)/codegen/style/parsed_file.cpp',
      '<(src_loc)/codegen/style/parsed_file.h',
      '<(src_loc)/codegen/style/processor.cpp',
      '<(src_loc)/codegen/style/processor.h',
      '<(src_loc)/codegen/style/structure_types.cpp',
      '<(src_loc)/codegen/style/structure_types.h',
    ],
  }, {
    'target_name': 'codegen_numbers',
    'variables': {
      'src_loc': '../SourceFiles',
      'mac_target': '10.10',
    },
    'includes': [
      'common_executable.gypi',
      'qt.gypi',
    ],

    'include_dirs': [
      '<(src_loc)',
    ],
    'sources': [
      '<(src_loc)/codegen/common/basic_tokenized_file.cpp',
      '<(src_loc)/codegen/common/basic_tokenized_file.h',
      '<(src_loc)/codegen/common/checked_utf8_string.cpp',
      '<(src_loc)/codegen/common/checked_utf8_string.h',
      '<(src_loc)/codegen/common/clean_file.cpp',
      '<(src_loc)/codegen/common/clean_file.h',
      '<(src_loc)/codegen/common/clean_file_reader.h',
      '<(src_loc)/codegen/common/const_utf8_string.h',
      '<(src_loc)/codegen/common/cpp_file.cpp',
      '<(src_loc)/codegen/common/cpp_file.h',
      '<(src_loc)/codegen/common/logging.cpp',
      '<(src_loc)/codegen/common/logging.h',
      '<(src_loc)/codegen/numbers/generator.cpp',
      '<(src_loc)/codegen/numbers/generator.h',
      '<(src_loc)/codegen/numbers/main.cpp',
      '<(src_loc)/codegen/numbers/options.cpp',
      '<(src_loc)/codegen/numbers/options.h',
      '<(src_loc)/codegen/numbers/parsed_file.cpp',
      '<(src_loc)/codegen/numbers/parsed_file.h',
      '<(src_loc)/codegen/numbers/processor.cpp',
      '<(src_loc)/codegen/numbers/processor.h',
    ],
  }, {
    'target_name': 'codegen_emoji',
    'variables': {
      'src_loc': '../SourceFiles',
      'mac_target': '10.10',
    },
    'includes': [
      'common_executable.gypi',
      'qt.gypi',
    ],

    'include_dirs': [
      '<(src_loc)',
    ],
    'sources': [
      '<(src_loc)/codegen/common/cpp_file.cpp',
      '<(src_loc)/codegen/common/cpp_file.h',
      '<(src_loc)/codegen/common/logging.cpp',
      '<(src_loc)/codegen/common/logging.h',
      '<(src_loc)/codegen/emoji/data.cpp',
      '<(src_loc)/codegen/emoji/data.h',
      '<(src_loc)/codegen/emoji/generator.cpp',
      '<(src_loc)/codegen/emoji/generator.h',
      '<(src_loc)/codegen/emoji/main.cpp',
      '<(src_loc)/codegen/emoji/options.cpp',
      '<(src_loc)/codegen/emoji/options.h',
      '<(src_loc)/codegen/emoji/replaces.cpp',
      '<(src_loc)/codegen/emoji/replaces.h',
    ],
  }],
}
