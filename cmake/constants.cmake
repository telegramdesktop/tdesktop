get_filename_component(libs_loc "../Libraries" REALPATH)
get_filename_component(third_party_loc "Telegram/ThirdParty" REALPATH)
get_filename_component(submodules_loc "Telegram" REALPATH)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(build_debug 1)
else()
    set(build_debug 0)
endif()
