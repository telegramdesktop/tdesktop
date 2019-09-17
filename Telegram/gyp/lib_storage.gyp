# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'includes': [
    '../ThirdParty/gyp_helpers/common/common.gypi',
  ],
  'targets': [{
    'target_name': 'lib_storage',
    'includes': [
      '../ThirdParty/gyp_helpers/common/library.gypi',
      '../ThirdParty/gyp_helpers/modules/openssl.gypi',
      '../ThirdParty/gyp_helpers/modules/qt.gypi',
      '../ThirdParty/gyp_helpers/modules/pch.gypi',
    ],
    'variables': {
      'src_loc': '../SourceFiles',
      'res_loc': '../Resources',
      'pch_source': '<(src_loc)/storage/storage_pch.cpp',
      'pch_header': '<(src_loc)/storage/storage_pch.h',
    },
    'defines': [
      'XXH_INLINE_ALL',
    ],
    'dependencies': [
      '../ThirdParty/lib_base/lib_base.gyp:lib_base',
    ],
    'export_dependent_settings': [
      '../ThirdParty/lib_base/lib_base.gyp:lib_base',
    ],
    'include_dirs': [
      '<(src_loc)',
      '<(submodules_loc)/xxHash',
    ],
    'sources': [
      '<(src_loc)/storage/storage_clear_legacy.cpp',
      '<(src_loc)/storage/storage_clear_legacy_posix.cpp',
      '<(src_loc)/storage/storage_clear_legacy_win.cpp',
      '<(src_loc)/storage/storage_clear_legacy.h',
      '<(src_loc)/storage/storage_databases.cpp',
      '<(src_loc)/storage/storage_databases.h',
      '<(src_loc)/storage/storage_encryption.cpp',
      '<(src_loc)/storage/storage_encryption.h',
      '<(src_loc)/storage/storage_encrypted_file.cpp',
      '<(src_loc)/storage/storage_encrypted_file.h',
      '<(src_loc)/storage/storage_file_lock_posix.cpp',
      '<(src_loc)/storage/storage_file_lock_win.cpp',
      '<(src_loc)/storage/storage_file_lock.h',
      '<(src_loc)/storage/cache/storage_cache_binlog_reader.cpp',
      '<(src_loc)/storage/cache/storage_cache_binlog_reader.h',
      '<(src_loc)/storage/cache/storage_cache_cleaner.cpp',
      '<(src_loc)/storage/cache/storage_cache_cleaner.h',
      '<(src_loc)/storage/cache/storage_cache_compactor.cpp',
      '<(src_loc)/storage/cache/storage_cache_compactor.h',
      '<(src_loc)/storage/cache/storage_cache_database.cpp',
      '<(src_loc)/storage/cache/storage_cache_database.h',
      '<(src_loc)/storage/cache/storage_cache_database_object.cpp',
      '<(src_loc)/storage/cache/storage_cache_database_object.h',
      '<(src_loc)/storage/cache/storage_cache_types.cpp',
      '<(src_loc)/storage/cache/storage_cache_types.h',
    ],
    'conditions': [[ 'build_macold', {
      'xcode_settings': {
        'OTHER_CPLUSPLUSFLAGS': [ '-nostdinc++' ],
      },
      'include_dirs': [
        '/usr/local/macold/include/c++/v1',
      ],
    }], [ 'build_win', {
      'sources!': [
        '<(src_loc)/storage/storage_clear_legacy_posix.cpp',
        '<(src_loc)/storage/storage_file_lock_posix.cpp',
      ],
    }, {
      'sources!': [
        '<(src_loc)/storage/storage_clear_legacy_win.cpp',
        '<(src_loc)/storage/storage_file_lock_win.cpp',
      ],
    }]],
  }],
}
