# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(lib_mtproto OBJECT)
init_target(lib_mtproto)
add_library(tdesktop::lib_mtproto ALIAS lib_mtproto)

target_precompile_headers(lib_mtproto PRIVATE ${src_loc}/mtproto/mtp_pch.h)
nice_target_sources(lib_mtproto ${src_loc}
PRIVATE
    mtproto/mtp_abstract_socket.cpp
    mtproto/mtp_abstract_socket.h
    mtproto/mtp_tcp_socket.cpp
    mtproto/mtp_tcp_socket.h
    mtproto/mtp_tls_socket.cpp
    mtproto/mtp_tls_socket.h
)

target_include_directories(lib_mtproto
PUBLIC
    ${src_loc}
)

target_link_libraries(lib_mtproto
PUBLIC
    tdesktop::lib_scheme
)
