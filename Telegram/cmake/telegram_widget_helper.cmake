# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_executable(TelegramWidgetHelper)
init_standalone_target(TelegramWidgetHelper)

nice_target_sources(TelegramWidgetHelper ${src_loc}
PRIVATE
    _other/widget_helper.cpp
    _other/widget_helper.h
    _other/widget_helper_mac.cpp
    _other/widget_helper_posix.cpp
    platform/posix/widget_channel_posix.cpp
    platform/posix/widget_channel_posix.h
    test/test_widget_helper_probe_lockdown.h
    test/test_widget_helper_probe_lockdown_mac.cpp
)

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

set_target_properties(TelegramWidgetHelper PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${output_folder}
)

add_custom_target(TelegramWidgetHelper_bundle
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Telegram>/../Helpers"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:TelegramWidgetHelper>" "$<TARGET_FILE_DIR:Telegram>/../Helpers/"
    VERBATIM
)
add_dependencies(TelegramWidgetHelper_bundle TelegramWidgetHelper)
add_dependencies(Telegram TelegramWidgetHelper_bundle)
