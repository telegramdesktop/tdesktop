# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(td_webauthn OBJECT)
init_non_host_target(td_webauthn)
add_library(tdesktop::td_webauthn ALIAS td_webauthn)

nice_target_sources(td_webauthn ${src_loc}
PRIVATE
    webauthn/cable_core.cpp
    webauthn/cable_core.h
    webauthn/cable_scanner.h
    webauthn/cable_tunnel.cpp
    webauthn/cable_tunnel.h
)

target_include_directories(td_webauthn
PUBLIC
    ${src_loc}
)

target_link_libraries(td_webauthn
PUBLIC
    desktop-app::external_qt
PRIVATE
    desktop-app::external_openssl
)

if (LINUX)
    nice_target_sources(td_webauthn ${src_loc}
    PRIVATE
        webauthn/cable_scanner_linux.cpp
    )
    target_link_libraries(td_webauthn PRIVATE desktop-app::external_glib)
elseif (WIN32)
    nice_target_sources(td_webauthn ${src_loc}
    PRIVATE
        webauthn/cable_scanner_win.cpp
    )
    target_link_libraries(td_webauthn
    PRIVATE
        desktop-app::lib_base
        desktop-app::lib_crl
        Bthprops.lib
    )
elseif (APPLE)
    nice_target_sources(td_webauthn ${src_loc}
    PRIVATE
        webauthn/cable_scanner_mac.mm
    )
    target_link_libraries(td_webauthn
    PRIVATE
        desktop-app::lib_base
        desktop-app::lib_crl
    )
else()
    nice_target_sources(td_webauthn ${src_loc}
    PRIVATE
        webauthn/cable_scanner_dummy.cpp
    )
endif()
