# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(lib_scheme OBJECT)
init_target(lib_scheme)
add_library(tdesktop::lib_scheme ALIAS lib_scheme)

include(cmake/generate_scheme.cmake)

set(scheme_files
    ${res_loc}/tl/mtproto.tl
    ${res_loc}/tl/api.tl
)

generate_scheme(lib_scheme ${src_loc}/codegen/scheme/codegen_scheme.py "${scheme_files}")

nice_target_sources(lib_scheme ${res_loc}
PRIVATE
    tl/mtproto.tl
    tl/api.tl
)

target_include_directories(lib_scheme
PUBLIC
    ${src_loc}
)

target_link_libraries(lib_scheme
PUBLIC
    desktop-app::lib_base
    desktop-app::lib_tl
)
