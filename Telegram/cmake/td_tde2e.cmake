# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(td_tde2e OBJECT)
init_non_host_target(td_tde2e)
add_library(tdesktop::td_tde2e ALIAS td_tde2e)

nice_target_sources(td_tde2e ${src_loc}
PRIVATE
    tde2e/tde2e_api.cpp
    tde2e/tde2e_api.h
)

target_include_directories(td_tde2e
PUBLIC
    ${src_loc}
)

target_link_libraries(td_tde2e
PUBLIC
    desktop-app::lib_base
PRIVATE
    desktop-app::external_tde2e
)



