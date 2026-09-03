# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# zxcvbn-c: the password strength estimator behind
# SourceFiles/ui/passcode_strength.cpp. The dictionary is compiled in
# (USE_DICT_FILE stays undefined), so the library performs no file I/O.
# There is no packaged branch: no distribution ships this library.

add_library(lib_zxcvbn STATIC)
init_target(lib_zxcvbn "(external)")

add_library(desktop-app::lib_zxcvbn ALIAS lib_zxcvbn)

set(zxcvbn_loc ${third_party_loc}/zxcvbn)
set(zxcvbn_gen ${CMAKE_CURRENT_BINARY_DIR}/zxcvbn_gen)

# Upstream ships the word lists and the generator, not the generated
# dictionary, so `dict-src.h` is produced here from the same inputs and in the
# same order upstream's own makefile uses. The generator is deterministic.
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
    target_compile_options(zxcvbn_dictgen PRIVATE /W0 /WX-)
else()
    target_compile_options(zxcvbn_dictgen PRIVATE -w)
endif()

# OUTPUT with explicit DEPENDS: the generator runs only when the word lists or
# the generator itself change, so an ordinary incremental build neither
# regenerates the dictionary nor relinks Telegram because of it.
add_custom_command(
    OUTPUT ${zxcvbn_gen}/dict-src.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${zxcvbn_gen}
    COMMAND zxcvbn_dictgen -o ${zxcvbn_gen}/dict-src.h ${zxcvbn_words}
    DEPENDS zxcvbn_dictgen ${zxcvbn_words}
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

if (LINUX)
    target_link_libraries(lib_zxcvbn PUBLIC m)
endif()

# Silence third-party C warnings. On MSVC the flags must ride an INTERFACE lib
# linked after common_options to land last and beat its /W4 /WX.
if (MSVC)
    add_library(lib_zxcvbn_warnings_off INTERFACE)
    target_compile_options(lib_zxcvbn_warnings_off
    INTERFACE
        /W0
        /WX-)
    target_link_libraries(lib_zxcvbn PRIVATE lib_zxcvbn_warnings_off)
else()
    target_compile_options(lib_zxcvbn PRIVATE -w)
endif()
