cmake_minimum_required(VERSION 3.12)
project(tg_local)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenSSL REQUIRED)
find_package(Qt5 COMPONENTS REQUIRED Core Gui Widgets)
find_package(SQLite3 REQUIRED)
find_package(GTest REQUIRED)

add_library(tg_local_lib STATIC main/local.cpp coder/coder.cpp key_manager/key_manager.cpp)

target_include_directories(tg_local_lib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tg_local_lib PRIVATE OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(tg_local_lib PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)
target_link_libraries(tg_local_lib PRIVATE SQLite::SQLite3)

# add_executable(tg_local main.cpp)
# target_link_libraries(tg_local PRIVATE tg_local_lib)

# add_executable(tg_local_test_coder coder/test/test.cpp)
# target_link_libraries(tg_local_test_coder PRIVATE tg_local_lib GTest::GTest GTest::Main)

# add_executable(tg_local_test_km key_manager/test/test.cpp)
# target_link_libraries(tg_local_test_km PRIVATE tg_local_lib GTest::GTest GTest::Main)
