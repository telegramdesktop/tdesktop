# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

include(cmake/generate_widget_helper_identity.cmake)
include(cmake/lib_widget_helper_rust.cmake)

if (build_macstore)
    set(widget_helper_identifier ${bundle_identifier}.WidgetHelper)
else()
    set(widget_helper_identifier com.tdesktop.Telegram.WidgetHelper)
endif()

add_executable(TelegramWidgetHelper)
init_standalone_target(TelegramWidgetHelper)

nice_target_sources(TelegramWidgetHelper ${src_loc}
PRIVATE
    _other/widget_helper.cpp
    _other/widget_helper.h
    _other/widget_helper_linux.cpp
    _other/widget_helper_mac.cpp
    _other/widget_helper_rust.cpp
    _other/widget_helper_service_mac.cpp
    _other/widget_helper_thread_mac.cpp
    _other/widget_helper_thread_mac.h
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
    test/test_widget_helper_probe_lockdown.cpp
    test/test_widget_helper_probe_lockdown.h
    test/test_widget_helper_probe_lockdown_linux.cpp
    test/test_widget_helper_probe_lockdown_mac.cpp
    test/test_widget_helper_probe_lockdown_win.cpp
)

if (NOT build_macstore)
    remove_target_sources(TelegramWidgetHelper ${src_loc}
        _other/widget_helper_service_mac.cpp
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
    tdesktop::lib_widget_sandbox_platform
    tdesktop::lib_widget_helper_rust
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
        # Not KnownDLLs on every Windows, which the LPAC primary cannot open: WarmUp() binds them while impersonating.
        /DELAYLOAD:bcrypt.dll
        $<$<CONFIG:Debug>:/DELAYLOAD:ws2_32.dll>
    )
    if (NOT build_winarm)
        # advapi32 can forward SystemFunction036 to cryptbase.dll, no KnownDLL: a delay-load hook answers it instead.
        target_link_options(TelegramWidgetHelper PRIVATE /DELAYLOAD:advapi32.dll)
    else()
        target_link_options(TelegramWidgetHelper PRIVATE /DELAYLOAD:bcryptprimitives.dll)
    endif()
    if (DESKTOP_APP_WIN7_BASELINE)
        target_link_options(TelegramWidgetHelper PRIVATE /SUBSYSTEM:WINDOWS,6.01)
    endif()
    if (MSVC)
        foreach (target TelegramWidgetHelper lib_widget_sandbox_platform)
            target_compile_definitions(${target}
            PRIVATE
                _MSVC_STL_HARDENING=1
                _MSVC_STL_DESTRUCTOR_TOMBSTONES=1
            )
            target_compile_options(${target} PRIVATE /guard:cf)
        endforeach()
        target_link_options(TelegramWidgetHelper PRIVATE /GUARD:CF)
        # The ARM64 linker refuses /CETCOMPAT with LNK1246.
        if (CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "^(X86|x64)$")
            target_link_options(TelegramWidgetHelper PRIVATE /CETCOMPAT)
        endif()
    endif()
    add_dependencies(Telegram TelegramWidgetHelper)

    generate_widget_helper_identity(Telegram TelegramWidgetHelper authenticode)
elseif (APPLE)
    include(cmake/widget_helper_hardening.cmake)

    set_target_properties(TelegramWidgetHelper PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${output_folder}
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
    )
    if (build_macstore)
        set(widget_helper_entitlements ${CMAKE_CURRENT_SOURCE_DIR}/Telegram/TelegramWidgetHelper.entitlements)
        set_target_properties(TelegramWidgetHelper PROPERTIES
            MACOSX_BUNDLE TRUE
            BUNDLE_EXTENSION xpc
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/TelegramWidgetHelper.plist
            XCODE_PRODUCT_TYPE com.apple.product-type.xpc-service
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${widget_helper_identifier}
            LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/TelegramWidgetHelper.plist;${widget_helper_entitlements}"
        )
        set(widget_helper_signed $<TARGET_BUNDLE_DIR:TelegramWidgetHelper>)
    else()
        set(widget_helper_entitlements ${CMAKE_CURRENT_SOURCE_DIR}/Telegram/TelegramWidgetHelperDirect.entitlements)
        set_target_properties(TelegramWidgetHelper PROPERTIES
            LINK_DEPENDS ${widget_helper_entitlements}
        )
        set(widget_helper_signed $<TARGET_FILE:TelegramWidgetHelper>)
    endif()

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
            --entitlements ${widget_helper_entitlements}
            ${widget_helper_signed}
        COMMAND_EXPAND_LISTS
        VERBATIM
    )

    if (build_macstore)
        set(widget_helper_built "$<TARGET_BUNDLE_CONTENT_DIR:TelegramWidgetHelper>")
        set(widget_helper_service "$<TARGET_FILE_DIR:Telegram>/../XPCServices/TelegramWidgetHelper.xpc/Contents")
        add_custom_target(TelegramWidgetHelper_bundle
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${widget_helper_service}/MacOS"
                "${widget_helper_service}/_CodeSignature"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${widget_helper_built}/Info.plist" "${widget_helper_service}/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${widget_helper_built}/MacOS/TelegramWidgetHelper" "${widget_helper_service}/MacOS/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${widget_helper_built}/_CodeSignature/CodeResources" "${widget_helper_service}/_CodeSignature/"
            VERBATIM
        )
    else()
        add_custom_target(TelegramWidgetHelper_bundle
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Telegram>/../Helpers"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:TelegramWidgetHelper>" "$<TARGET_FILE_DIR:Telegram>/../Helpers/"
            VERBATIM
        )
    endif()
    generate_widget_helper_identity(Telegram TelegramWidgetHelper cdhash
        IDENTIFIER ${widget_helper_identifier}
        ENTITLEMENTS ${widget_helper_entitlements})
    add_dependencies(TelegramWidgetHelper_bundle TelegramWidgetHelper)
    add_dependencies(Telegram TelegramWidgetHelper_bundle)
else()
    set(widget_helper_folder TelegramHelpers)
    set(widget_helper_pinned ON)
    if (DESKTOP_APP_USE_PACKAGED)
        include(GNUInstallDirs)
        set(widget_helper_destination "${CMAKE_INSTALL_BINDIR}/TelegramHelpers")
        if (NOT DESKTOP_APP_SPECIAL_TARGET)
            file(RELATIVE_PATH widget_helper_folder
                "${CMAKE_INSTALL_FULL_BINDIR}"
                "${CMAKE_INSTALL_FULL_LIBEXECDIR}/TelegramDesktop")
            set(widget_helper_destination "${CMAKE_INSTALL_LIBEXECDIR}/TelegramDesktop")
            set(widget_helper_pinned OFF)
        endif()
        install(TARGETS TelegramWidgetHelper RUNTIME DESTINATION "${widget_helper_destination}")
    endif()

    set_target_properties(TelegramWidgetHelper PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:Telegram>/${widget_helper_folder}"
    )
    set_property(SOURCE ${src_loc}/platform/linux/widget_helper_linux.cpp APPEND PROPERTY COMPILE_DEFINITIONS
        "TDESKTOP_WIDGET_HELPER_PATH=\"${widget_helper_folder}/TelegramWidgetHelper\"")
    if (widget_helper_pinned)
        if (NOT CMAKE_STRIP)
            message(FATAL_ERROR
                "TelegramWidgetHelper is stripped outside Debug, but no strip tool "
                "was found (CMAKE_STRIP).")
        endif()

        # Strip here, nothing may change the helper after its identity is taken.
        add_custom_command(TARGET TelegramWidgetHelper POST_BUILD
            COMMAND "$<$<NOT:$<CONFIG:Debug>>:${CMAKE_STRIP};$<TARGET_FILE:TelegramWidgetHelper>>"
            COMMAND_EXPAND_LISTS
            VERBATIM
        )

        set_property(SOURCE ${src_loc}/platform/linux/widget_helper_linux.cpp APPEND PROPERTY COMPILE_DEFINITIONS
            TDESKTOP_WIDGET_HELPER_PINNED)
        generate_widget_helper_identity(Telegram TelegramWidgetHelper sha256)
    endif()
    foreach (target TelegramWidgetHelper lib_widget_sandbox_platform)
        target_compile_definitions(${target} PRIVATE _GLIBCXX_ASSERTIONS)
        target_compile_options_if_exists(${target}
        PRIVATE
            -fstack-protector-strong
            -fstack-clash-protection
            -fcf-protection
            -ftrivial-auto-var-init=zero
        )
        # Fortify needs optimization, glibc warns without it; through -Wp, our level wins over a distribution's.
        target_compile_options(${target}
        PRIVATE
            $<$<NOT:$<CONFIG:Debug>>:-Wp,-U_FORTIFY_SOURCE>
            $<$<NOT:$<CONFIG:Debug>>:-Wp,-D_FORTIFY_SOURCE=3>
        )
    endforeach()
    target_link_options(TelegramWidgetHelper
    PRIVATE
        LINKER:-z,relro
        LINKER:-z,now
    )
    add_dependencies(Telegram TelegramWidgetHelper)
endif()
