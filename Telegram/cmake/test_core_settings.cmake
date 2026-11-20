# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

if (WIN32 AND NOT DESKTOP_APP_USE_PACKAGED)
    add_library(test_core_settings_code STATIC)
    init_non_host_target(test_core_settings_code "(tests)")

    target_link_libraries(test_core_settings_code
    PUBLIC
        tdesktop::td_mtproto
        tdesktop::td_ui
        desktop-app::lib_webrtc
        ${libs_loc}/dav1d/builddir-$<IF:$<CONFIG:Debug>,debug,release>/src/libdav1d.a
    )

    add_executable(test_core_settings)
    init_non_host_target(test_core_settings "(tests)")

    target_precompile_headers(test_core_settings
    PRIVATE
        ${src_loc}/ui/ui_pch.h
        ${src_loc}/tests/test_core_settings_pch.hpp
    )
    nice_target_sources(test_core_settings ${src_loc}
    PRIVATE
        chat_helpers/spellchecker_locale_ids.cpp
        chat_helpers/spellchecker_locale_ids.h
        core/application.h
        core/core_settings.cpp
        core/core_settings.h
        core/core_settings_proxy.cpp
        core/core_settings_proxy.h
        data/data_types.h
        data/data_wall_paper.h
        mtproto/type_utils.h
        platform/platform_notifications_manager.h
        storage/serialize_common.cpp
        storage/serialize_common.h
        tests/test_core_settings.cc
        tests/test_core_settings_app_symbols.cc
        tests/test_core_settings_pch.hpp
        tests/test_named_checks.hpp
        window/themes/window_theme.h
        window/themes/window_themes_embedded.cpp
        window/themes/window_themes_embedded.h
        settings.cpp
        settings.h
    )
    target_link_libraries(test_core_settings
    PRIVATE
        test_core_settings_code
        desktop-app::lib_spellcheck
    )
    target_link_options(test_core_settings PRIVATE /OPT:REF)

    set_target_properties(test_core_settings_code PROPERTIES
        EXCLUDE_FROM_ALL TRUE
        EXCLUDE_FROM_DEFAULT_BUILD TRUE
    )
    set_target_properties(test_core_settings PROPERTIES
        EXCLUDE_FROM_ALL TRUE
        EXCLUDE_FROM_DEFAULT_BUILD TRUE
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_core_settings
    )
endif()
