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
    'target_name': 'lib_mtproto',
    'includes': [
      'common/library.gypi',
      'modules/qt.gypi',
      'modules/pch.gypi',
      'modules/openssl.gypi',
    ],
    'variables': {
      'src_loc': '../SourceFiles',
      'res_loc': '../Resources',
      'official_build_target%': '',
      'submodules_loc': '../ThirdParty',
      'pch_source': '<(src_loc)/mtproto/mtp_pch.cpp',
      'pch_header': '<(src_loc)/mtproto/mtp_pch.h',
    },
    'defines': [
    ],
    'dependencies': [
      'lib_scheme.gyp:lib_scheme',
      'crl.gyp:crl',
    ],
    'export_dependent_settings': [
      'lib_scheme.gyp:lib_scheme',
    ],
    'conditions': [[ 'build_macold', {
      'xcode_settings': {
        'OTHER_CPLUSPLUSFLAGS': [ '-nostdinc++' ],
      },
      'include_dirs': [
        '/usr/local/macold/include/c++/v1',
      ],
    }]],
    'include_dirs': [
      '<(src_loc)',
      '<(SHARED_INTERMEDIATE_DIR)',
      '<(libs_loc)/range-v3/include',
      '<(submodules_loc)/GSL/include',
      '<(submodules_loc)/variant/include',
      '<(submodules_loc)/crl/src',
    ],
    'sources': [
      '<(src_loc)/mtproto/mtp_abstract_socket.cpp',
      '<(src_loc)/mtproto/mtp_abstract_socket.h',
      '<(src_loc)/mtproto/mtp_tcp_socket.cpp',
      '<(src_loc)/mtproto/mtp_tcp_socket.h',
      '<(src_loc)/mtproto/mtp_tls_socket.cpp',
      '<(src_loc)/mtproto/mtp_tls_socket.h',
    ],
  }],
}
