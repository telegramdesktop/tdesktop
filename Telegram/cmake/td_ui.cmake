# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(td_ui OBJECT)
init_target(td_ui)
add_library(tdesktop::td_ui ALIAS td_ui)

include(lib_ui/cmake/generate_styles.cmake)

set(style_files
    ui/td_common.style
)

set(dependent_style_files
    ${submodules_loc}/lib_ui/ui/colors.palette
    ${submodules_loc}/lib_ui/ui/basic.style
    ${submodules_loc}/lib_ui/ui/layers/layers.style
    ${submodules_loc}/lib_ui/ui/widgets/widgets.style
)

generate_styles(td_ui ${src_loc} "${style_files}" "${dependent_style_files}")

target_precompile_headers(td_ui PRIVATE ${src_loc}/ui/ui_pch.h)
nice_target_sources(td_ui ${src_loc}
PRIVATE
    ${style_files}
    
    ui/ui_pch.h
    ui/text/format_values.cpp
    ui/text/format_values.h
    ui/toasts/common_toasts.cpp
    ui/toasts/common_toasts.h
)

target_include_directories(td_ui
PUBLIC
    ${src_loc}
)

target_link_libraries(td_ui
PUBLIC
    tdesktop::td_lang
    desktop-app::lib_ui
)
