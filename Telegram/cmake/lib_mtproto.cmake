add_library(lib_mtproto OBJECT)
init_target(lib_mtproto)

set(lib_mtproto_sources
PRIVATE
    mtproto/mtp_abstract_socket.cpp
    mtproto/mtp_abstract_socket.h
    mtproto/mtp_tcp_socket.cpp
    mtproto/mtp_tcp_socket.h
    mtproto/mtp_tls_socket.cpp
    mtproto/mtp_tls_socket.h
)
nice_target_sources(lib_mtproto ${src_loc} "${lib_mtproto_sources}")

target_precompile_headers(lib_mtproto PRIVATE ${src_loc}/mtproto/mtp_pch.h)

target_include_directories(lib_mtproto
PUBLIC
    ${src_loc}
)

target_link_libraries(lib_mtproto
PUBLIC
    lib_scheme
)
