# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'includes': [
    'helpers/common/common.gypi',
  ],
  'targets': [{
    'target_name': 'lib_mtproto',
    'includes': [
      'helpers/common/library.gypi',
      'helpers/modules/qt.gypi',
      'helpers/modules/pch.gypi',
      'helpers/modules/openssl.gypi',
    ],
    'variables': {
      'src_loc': '../SourceFiles',
      'res_loc': '../Resources',
      'pch_source': '<(src_loc)/mtproto/mtproto_pch.cpp',
      'pch_header': '<(src_loc)/mtproto/mtproto_pch.h',
    },
    'defines': [
    ],
    'dependencies': [
      'lib_scheme.gyp:lib_scheme',
    ],
    'export_dependent_settings': [
      'lib_scheme.gyp:lib_scheme',
    ],
    'include_dirs': [
      '<(src_loc)',
    ],
    'sources': [
      '<(src_loc)/mtproto/details/mtproto_abstract_socket.cpp',
      '<(src_loc)/mtproto/details/mtproto_abstract_socket.h',
      '<(src_loc)/mtproto/details/mtproto_bound_key_creator.cpp',
      '<(src_loc)/mtproto/details/mtproto_bound_key_creator.h',
      '<(src_loc)/mtproto/details/mtproto_dc_key_binder.cpp',
      '<(src_loc)/mtproto/details/mtproto_dc_key_binder.h',
      '<(src_loc)/mtproto/details/mtproto_dc_key_creator.cpp',
      '<(src_loc)/mtproto/details/mtproto_dc_key_creator.h',
      '<(src_loc)/mtproto/details/mtproto_dcenter.cpp',
      '<(src_loc)/mtproto/details/mtproto_dcenter.h',
      '<(src_loc)/mtproto/details/mtproto_domain_resolver.cpp',
      '<(src_loc)/mtproto/details/mtproto_domain_resolver.h',
      '<(src_loc)/mtproto/details/mtproto_dump_to_text.cpp',
      '<(src_loc)/mtproto/details/mtproto_dump_to_text.h',
      '<(src_loc)/mtproto/details/mtproto_received_ids_manager.cpp',
      '<(src_loc)/mtproto/details/mtproto_received_ids_manager.h',
      '<(src_loc)/mtproto/details/mtproto_rsa_public_key.cpp',
      '<(src_loc)/mtproto/details/mtproto_rsa_public_key.h',
      '<(src_loc)/mtproto/details/mtproto_serialized_request.cpp',
      '<(src_loc)/mtproto/details/mtproto_serialized_request.h',
      '<(src_loc)/mtproto/details/mtproto_tcp_socket.cpp',
      '<(src_loc)/mtproto/details/mtproto_tcp_socket.h',
      '<(src_loc)/mtproto/details/mtproto_tls_socket.cpp',
      '<(src_loc)/mtproto/details/mtproto_tls_socket.h',
      '<(src_loc)/mtproto/mtproto_auth_key.cpp',
      '<(src_loc)/mtproto/mtproto_auth_key.h',
      '<(src_loc)/mtproto/mtproto_concurrent_sender.cpp',
      '<(src_loc)/mtproto/mtproto_concurrent_sender.h',
      '<(src_loc)/mtproto/mtproto_dh_utils.cpp',
      '<(src_loc)/mtproto/mtproto_dh_utils.h',
      '<(src_loc)/mtproto/mtproto_proxy_data.cpp',
      '<(src_loc)/mtproto/mtproto_proxy_data.h',
      '<(src_loc)/mtproto/mtproto_rpc_sender.cpp',
      '<(src_loc)/mtproto/mtproto_rpc_sender.h',
    ],
  }],
}
