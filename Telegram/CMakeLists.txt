add_subdirectory(lib_rpl)
add_subdirectory(lib_crl)
add_subdirectory(lib_base)
add_subdirectory(lib_ui)
add_subdirectory(codegen)

# include(lib_ui/cmake/generate_styles.cmake)

# get_filename_component(src_loc "./SourceFiles" REALPATH)

# add_executable(Telegram WIN32 MACOSX_BUNDLE)
# init_target(Telegram)

# force_include(Telegram stdafx.h)

# set(telegram_sources
# PRIVATE
#     main.cpp
# )

# nice_target_sources(Telegram ${src_loc} "${telegram_sources}")

# target_compile_definitions(Telegram
# PRIVATE
#     TDESKTOP_API_ID=17349
#     TDESKTOP_API_HASH=344583e45741c457fe1862106095a5eb
# )

# target_link_libraries(Telegram
# PRIVATE
#     lib_base
#     lib_ui
# )
