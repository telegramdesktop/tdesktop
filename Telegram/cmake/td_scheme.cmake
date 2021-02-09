# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(td_scheme OBJECT)
init_target(td_scheme)
add_library(tdesktop::td_scheme ALIAS td_scheme)

include(cmake/generate_scheme.cmake)

set(scheme_files
    ${res_loc}/tl/mtproto.tl
    ${res_loc}/tl/api.tl
)

generate_scheme(td_scheme ${src_loc}/codegen/scheme/codegen_scheme.py "${scheme_files}")

nice_target_sources(td_scheme ${res_loc}
PRIVATE
    tl/mtproto.tl
    tl/api.tl
)

target_include_directories(td_scheme
PUBLIC
    ${src_loc}
)

target_link_libraries(td_scheme
PUBLIC
    desktop-app::lib_base
    desktop-app::lib_tl
)

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "mips64")
    # Sometimes final linking may fail with error "relocation truncated to fit"
    # due to large scheme size.
    target_compile_options(td_scheme
    PRIVATE
        -mxgot
    )
endif()
