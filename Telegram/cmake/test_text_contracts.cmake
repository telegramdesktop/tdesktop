# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

if (WIN32 AND NOT DESKTOP_APP_USE_PACKAGED
    AND NOT DESKTOP_APP_USE_PACKAGED_FONTS
    AND NOT DESKTOP_APP_USE_PANGO
    AND TARGET Qt${QT_VERSION_MAJOR}::QOffscreenIntegrationPlugin
    AND TARGET Qt${QT_VERSION_MAJOR}::QOffscreenIntegrationPlugin_init)
    add_executable(test_text_contracts)
    init_non_host_target(test_text_contracts "(tests)")

    nice_target_sources(test_text_contracts ${src_loc}
    PRIVATE
        tests/test_named_checks.hpp
        tests/test_text_contracts.cc
    )

    target_include_directories(test_text_contracts
    PRIVATE
        ${src_loc}
    )

    target_link_libraries(test_text_contracts
    PRIVATE
        desktop-app::lib_base
        desktop-app::lib_crl
        desktop-app::lib_ui
        desktop-app::external_qt
        desktop-app::external_qt_static_plugins
        $<TARGET_OBJECTS:Qt${QT_VERSION_MAJOR}::QOffscreenIntegrationPlugin_init>
        $<TARGET_FILE:Qt${QT_VERSION_MAJOR}::QOffscreenIntegrationPlugin>
    )

    set_target_properties(test_text_contracts PROPERTIES
        EXCLUDE_FROM_ALL TRUE
        EXCLUDE_FROM_DEFAULT_BUILD TRUE
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_text_contracts
    )
endif()
