# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

include(cmake/generate_widget_helper_identity.cmake)

set(widget_helper_identifier com.tdesktop.Telegram.WidgetHelper)

add_executable(TelegramWidgetHelper)
init_standalone_target(TelegramWidgetHelper)

nice_target_sources(TelegramWidgetHelper ${src_loc}
PRIVATE
    _other/widget_helper.cpp
    _other/widget_helper.h
    _other/widget_helper_linux.cpp
    _other/widget_helper_mac.cpp
    _other/widget_helper_posix.cpp
    _other/widget_helper_win.cpp
    platform/linux/widget_descriptors_linux.cpp
    platform/linux/widget_descriptors_linux.h
    platform/linux/widget_lockdown_linux.cpp
    platform/linux/widget_lockdown_linux.h
    platform/posix/widget_channel_posix.cpp
    platform/posix/widget_channel_posix.h
    platform/win/widget_channel_win.cpp
    platform/win/widget_channel_win.h
    platform/win/widget_sandbox_win.cpp
    platform/win/widget_sandbox_win.h
    test/test_widget_helper_probe_lockdown.h
    test/test_widget_helper_probe_lockdown_linux.cpp
    test/test_widget_helper_probe_lockdown_mac.cpp
    test/test_widget_helper_probe_lockdown_win.cpp
)

if (NOT APPLE)
    remove_target_sources(TelegramWidgetHelper ${src_loc}
        _other/widget_helper_posix.cpp
    )
endif()

target_include_directories(TelegramWidgetHelper PRIVATE ${src_loc})

target_compile_definitions(TelegramWidgetHelper
PRIVATE
    TDESKTOP_APP_VERSION=${desktop_app_version_int}
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/../build/version")

target_link_libraries(TelegramWidgetHelper
PRIVATE
    tdesktop::lib_widget_sandbox
    desktop-app::external_gsl
)

if (WIN32)
    if (build_winarm)
        set(widget_helper_arch arm64)
    elseif (build_win64)
        set(widget_helper_arch x64)
    else()
        set(widget_helper_arch x86)
    endif()

    set_target_properties(TelegramWidgetHelper PROPERTIES
        WIN32_EXECUTABLE TRUE
        RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:Telegram>/modules/${widget_helper_arch}"
    )
    target_link_libraries(TelegramWidgetHelper
    PRIVATE
        advapi32
        bcrypt
        $<$<CONFIG:Debug>:ws2_32>
        delayimp
    )
    target_link_options(TelegramWidgetHelper
    PRIVATE
        # GUI image with main(): a console one would start a conhost.exe the job forbids.
        /ENTRY:mainCRTStartup
        # Not a KnownDLL, which the LPAC primary cannot open: WarmUp() binds it while impersonating.
        /DELAYLOAD:bcrypt.dll
        $<$<CONFIG:Debug>:/DELAYLOAD:ws2_32.dll>
    )
    add_dependencies(Telegram TelegramWidgetHelper)
elseif (APPLE)
    set_target_properties(TelegramWidgetHelper PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${output_folder}
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
    )

    if (NOT CMAKE_STRIP)
        message(FATAL_ERROR
            "TelegramWidgetHelper is stripped outside Debug, but no strip tool "
            "was found (CMAKE_STRIP).")
    endif()

    # Strip and sign here, nothing may change the helper after its identity is taken.
    add_custom_command(TARGET TelegramWidgetHelper POST_BUILD
        COMMAND "$<$<NOT:$<CONFIG:Debug>>:${CMAKE_STRIP};$<TARGET_FILE:TelegramWidgetHelper>>"
        COMMAND codesign --force --sign - --options runtime
            --identifier ${widget_helper_identifier}
            $<TARGET_FILE:TelegramWidgetHelper>
        COMMAND_EXPAND_LISTS
        VERBATIM
    )

    add_custom_target(TelegramWidgetHelper_bundle
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Telegram>/../Helpers"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:TelegramWidgetHelper>" "$<TARGET_FILE_DIR:Telegram>/../Helpers/"
        VERBATIM
    )
    add_dependencies(TelegramWidgetHelper_bundle TelegramWidgetHelper)
    add_dependencies(Telegram TelegramWidgetHelper_bundle)

    generate_widget_helper_identity(Telegram TelegramWidgetHelper cdhash IDENTIFIER ${widget_helper_identifier})
else()
    set(widget_helper_folder TelegramHelpers)
    if (DESKTOP_APP_USE_PACKAGED)
        include(GNUInstallDirs)
        set(widget_helper_destination "${CMAKE_INSTALL_BINDIR}/TelegramHelpers")
        if (NOT DESKTOP_APP_SPECIAL_TARGET)
            file(RELATIVE_PATH widget_helper_folder
                "${CMAKE_INSTALL_FULL_BINDIR}"
                "${CMAKE_INSTALL_FULL_LIBEXECDIR}/telegram-desktop")
            set(widget_helper_destination "${CMAKE_INSTALL_LIBEXECDIR}/telegram-desktop")
        endif()
        install(TARGETS TelegramWidgetHelper RUNTIME DESTINATION "${widget_helper_destination}")
    endif()

    set_target_properties(TelegramWidgetHelper PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:Telegram>/${widget_helper_folder}"
    )
    set_property(SOURCE ${src_loc}/platform/linux/widget_helper_linux.cpp APPEND PROPERTY COMPILE_DEFINITIONS
        "TDESKTOP_WIDGET_HELPER_PATH=\"${widget_helper_folder}/TelegramWidgetHelper\"")
    add_dependencies(Telegram TelegramWidgetHelper)
endif()
