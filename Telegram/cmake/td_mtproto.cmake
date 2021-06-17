# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(td_mtproto OBJECT)
init_target(td_mtproto)
add_library(tdesktop::td_mtproto ALIAS td_mtproto)

target_precompile_headers(td_mtproto PRIVATE ${src_loc}/mtproto/mtproto_pch.h)
nice_target_sources(td_mtproto ${src_loc}
PRIVATE
    mtproto/details/mtproto_abstract_socket.cpp
    mtproto/details/mtproto_abstract_socket.h
    mtproto/details/mtproto_bound_key_creator.cpp
    mtproto/details/mtproto_bound_key_creator.h
    mtproto/details/mtproto_dc_key_binder.cpp
    mtproto/details/mtproto_dc_key_binder.h
    mtproto/details/mtproto_dc_key_creator.cpp
    mtproto/details/mtproto_dc_key_creator.h
    mtproto/details/mtproto_dcenter.cpp
    mtproto/details/mtproto_dcenter.h
    mtproto/details/mtproto_domain_resolver.cpp
    mtproto/details/mtproto_domain_resolver.h
    mtproto/details/mtproto_dump_to_text.cpp
    mtproto/details/mtproto_dump_to_text.h
    mtproto/details/mtproto_received_ids_manager.cpp
    mtproto/details/mtproto_received_ids_manager.h
    mtproto/details/mtproto_rsa_public_key.cpp
    mtproto/details/mtproto_rsa_public_key.h
    mtproto/details/mtproto_serialized_request.cpp
    mtproto/details/mtproto_serialized_request.h
    mtproto/details/mtproto_tcp_socket.cpp
    mtproto/details/mtproto_tcp_socket.h
    mtproto/details/mtproto_tls_socket.cpp
    mtproto/details/mtproto_tls_socket.h
    mtproto/mtproto_auth_key.cpp
    mtproto/mtproto_auth_key.h
    mtproto/mtproto_concurrent_sender.cpp
    mtproto/mtproto_concurrent_sender.h
    mtproto/mtproto_config.cpp
    mtproto/mtproto_config.h
    mtproto/mtproto_dc_options.cpp
    mtproto/mtproto_dc_options.h
    mtproto/mtproto_dh_utils.cpp
    mtproto/mtproto_dh_utils.h
    mtproto/mtproto_pch.h
    mtproto/mtproto_proxy_data.cpp
    mtproto/mtproto_proxy_data.h
    mtproto/mtproto_response.cpp
    mtproto/mtproto_response.h
)

target_include_directories(td_mtproto
PUBLIC
    ${src_loc}
)

target_link_libraries(td_mtproto
PUBLIC
    tdesktop::td_scheme
PRIVATE
    desktop-app::external_zlib
)
