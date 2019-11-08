add_subdirectory(lib_rpl)
add_subdirectory(lib_crl)
add_subdirectory(lib_base)
add_subdirectory(codegen)

# get_filename_component(src_loc "./SourceFiles" REALPATH)

# add_executable(Telegram WIN32 MACOSX_BUNDLE
#     ${src_loc}/main.cpp
# )
# init_target(Telegram)

# force_include(Telegram stdafx.h)

# target_link_libraries(Telegram
# PRIVATE
#     lib_base
# )
