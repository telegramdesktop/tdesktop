cmake_minimum_required(VERSION 3.12)
cmake_policy(SET CMP0076 NEW)
cmake_policy(SET CMP0091 NEW)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project(Telegram
    LANGUAGES C CXX
    VERSION 1.9.0
    DESCRIPTION "Official Telegram Desktop messenger"
    HOMEPAGE_URL "https://desktop.telegram.org"
)

include(cmake/paths.cmake)
include(cmake/force_include.cmake)
include(cmake/init_target.cmake)
include(cmake/generate_target.cmake)
include(cmake/nice_target_sources.cmake)

add_subdirectory(cmake)
add_subdirectory(Telegram)
