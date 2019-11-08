cmake_minimum_required(VERSION 3.12)
cmake_policy(SET CMP0076 NEW)
cmake_policy(SET CMP0091 NEW)

project(Telegram
    LANGUAGES C CXX
    VERSION 1.9.0
    DESCRIPTION "Official Telegram Desktop messenger"
    HOMEPAGE_URL "https://desktop.telegram.org"
)

include(cmake/constants.cmake)
include(cmake/functions.cmake)

add_subdirectory(cmake)
add_subdirectory(Telegram)
