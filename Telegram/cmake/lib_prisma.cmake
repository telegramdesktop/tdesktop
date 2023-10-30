# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(lib_prisma STATIC)
init_target(lib_prisma)

add_library(desktop-app::lib_prisma ALIAS lib_prisma)

set(prisma_loc ${third_party_loc}/libprisma/libprisma)

nice_target_sources(lib_prisma ${prisma_loc}
PRIVATE
    Highlight.cpp
    Highlight.h
    LanguageTree.cpp
    LanguageTree.h
    SyntaxHighlighter.cpp
    SyntaxHighlighter.h
    TokenList.cpp
    TokenList.h
)

target_include_directories(lib_prisma
PUBLIC
    ${prisma_loc}
)

target_link_libraries(lib_prisma
PRIVATE
    desktop-app::external_boost_regex
)
