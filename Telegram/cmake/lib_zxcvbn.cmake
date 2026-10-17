# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# zxcvbn-c: the password strength estimator behind
# SourceFiles/ui/passcode_strength.cpp. The dictionary is compiled in
# (USE_DICT_FILE stays undefined), so the library performs no file I/O.
# Besides upstream's six lists it holds the Russian and Ukrainian frequency
# lists from Resources/zxcvbn, transliterated to Latin at build time by
# zxcvbn_translit through the same table the estimator applies to a candidate.
# There is no packaged branch: no distribution ships this library.

add_library(lib_zxcvbn STATIC)
init_target(lib_zxcvbn "(external)")

add_library(desktop-app::lib_zxcvbn ALIAS lib_zxcvbn)

set(zxcvbn_loc ${third_party_loc}/zxcvbn)
set(zxcvbn_gen ${CMAKE_CURRENT_BINARY_DIR}/zxcvbn_gen)

# Silence third-party warnings. On MSVC the flags must ride an INTERFACE lib
# linked after common_options to land last and beat its /W4 /WX.
if (MSVC)
    add_library(lib_zxcvbn_warnings_off INTERFACE)
    target_compile_options(lib_zxcvbn_warnings_off
    INTERFACE
        /W0
        /WX-)
endif()

# Upstream ships the word lists and the generator, not the generated
# dictionary, so `dict-src.h` is produced here from the same inputs and in the
# same order upstream's own makefile uses, followed by the two transliterated
# lists. The generator is deterministic and interleaves its inputs by rank, so
# the input order is part of the result.
set(zxcvbn_words
    words-eng_wiki.txt
    words-female.txt
    words-male.txt
    words-passwd.txt
    words-surname.txt
    words-tv_film.txt
)
list(TRANSFORM zxcvbn_words PREPEND ${zxcvbn_loc}/)

add_executable(zxcvbn_dictgen ${zxcvbn_loc}/dict-generate.cpp)
init_target(zxcvbn_dictgen "(codegen)")
target_compile_features(zxcvbn_dictgen PRIVATE cxx_std_11)
if (MSVC)
    target_link_libraries(zxcvbn_dictgen PRIVATE lib_zxcvbn_warnings_off)
else()
    target_compile_options(zxcvbn_dictgen PRIVATE -w)
endif()

add_executable(zxcvbn_translit
    ${src_loc}/_other/zxcvbn_translit.cpp
    ${src_loc}/ui/passcode_strength_translit.cpp
)
init_target(zxcvbn_translit "(codegen)")
target_include_directories(zxcvbn_translit PRIVATE ${src_loc})

# The generator drops every word with a byte above 0x7F, so the Cyrillic lists
# are transliterated first, by the table the estimator itself applies.
set(zxcvbn_translit_words ru_50k.txt uk_50k.txt)
set(zxcvbn_latin_words)
foreach (name ${zxcvbn_translit_words})
    string(REGEX REPLACE "\\.txt$" "-latin.txt" latin_name ${name})
    add_custom_command(
        OUTPUT ${zxcvbn_gen}/${latin_name}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${zxcvbn_gen}
        COMMAND zxcvbn_translit ${res_loc}/zxcvbn/${name} ${zxcvbn_gen}/${latin_name}
        DEPENDS zxcvbn_translit ${res_loc}/zxcvbn/${name}
        COMMENT "Transliterating zxcvbn ${name}"
        VERBATIM
    )
    list(APPEND zxcvbn_latin_words ${zxcvbn_gen}/${latin_name})
endforeach()

set(zxcvbn_dict_inputs ${zxcvbn_words} ${zxcvbn_latin_words})
list(LENGTH zxcvbn_dict_inputs zxcvbn_dict_input_count)
# dict-generate.cpp FileInfo InInfo[10]; NumFiles saturates at 9.
set(zxcvbn_dict_input_ceiling 9)
if (zxcvbn_dict_input_count GREATER ${zxcvbn_dict_input_ceiling})
    message(FATAL_ERROR "zxcvbn dictionary generator combines at most ${zxcvbn_dict_input_ceiling} input lists (pinned dict-generate.cpp FileInfo InInfo[10]); this build has ${zxcvbn_dict_input_count}.")
endif()

# OUTPUT with explicit DEPENDS: the generator runs only when the word lists or
# the generator itself change, so an ordinary incremental build neither
# regenerates the dictionary nor relinks Telegram because of it.
add_custom_command(
    OUTPUT ${zxcvbn_gen}/dict-src.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${zxcvbn_gen}
    COMMAND zxcvbn_dictgen -o ${zxcvbn_gen}/dict-src.h ${zxcvbn_words} ${zxcvbn_latin_words}
    DEPENDS zxcvbn_dictgen ${zxcvbn_words} ${zxcvbn_latin_words}
    COMMENT "Generating zxcvbn dict-src.h"
    VERBATIM
)

# Upstream's zxcvbn.c opens with #ifdef _WIN32 #include "stdafx.h" and upstream
# ships no such header, so Windows needs an empty one.
# file(CONFIGURE), not file(WRITE): the latter rewrites the header on every
# cmake run even when nothing changed, and a fresh mtime here recompiles the
# library and relinks Telegram.
file(CONFIGURE OUTPUT ${zxcvbn_gen}/stdafx.h
    CONTENT "/* Empty: upstream zxcvbn.c does #ifdef _WIN32 #include \"stdafx.h\" and ships no such header. */\n" @ONLY)

nice_target_sources(lib_zxcvbn ${zxcvbn_loc}
PRIVATE
    zxcvbn.c
    zxcvbn.h
)

target_sources(lib_zxcvbn PRIVATE ${zxcvbn_gen}/dict-src.h)

target_include_directories(lib_zxcvbn
PUBLIC
    ${zxcvbn_loc}
PRIVATE
    ${zxcvbn_gen}
)

# PUBLIC: the estimator bounds the candidate at the length the library details
# and must read this one number, never carry a copy of it.
target_compile_definitions(lib_zxcvbn PUBLIC ZXCVBN_DETAIL_LEN=100)

if (LINUX)
    target_link_libraries(lib_zxcvbn PUBLIC m)
endif()

if (MSVC)
    target_link_libraries(lib_zxcvbn PRIVATE lib_zxcvbn_warnings_off)
else()
    target_compile_options(lib_zxcvbn PRIVATE -w)
endif()
