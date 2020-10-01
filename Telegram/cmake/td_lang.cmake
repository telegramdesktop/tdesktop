# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(td_lang OBJECT)
init_target(td_lang)
add_library(tdesktop::td_lang ALIAS td_lang)

include(cmake/generate_lang.cmake)

generate_lang(td_lang ${res_loc}/langs/lang.strings)

target_precompile_headers(td_lang PRIVATE ${src_loc}/lang/lang_pch.h)
nice_target_sources(td_lang ${src_loc}
PRIVATE
    lang/lang_file_parser.cpp
    lang/lang_file_parser.h
    lang/lang_hardcoded.h
    lang/lang_keys.cpp
    lang/lang_keys.h
    lang/lang_pch.h
    lang/lang_tag.cpp
    lang/lang_tag.h
    lang/lang_text_entity.cpp
    lang/lang_text_entity.h
    lang/lang_values.h
)

target_include_directories(td_lang
PUBLIC
    ${src_loc}
)

target_link_libraries(td_lang
PUBLIC
    desktop-app::lib_ui
)
