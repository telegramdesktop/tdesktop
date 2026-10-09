cmake_minimum_required(VERSION 3.21)
project(Breakpad LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_RELEASE "-Os -g -DNDEBUG")
set(CMAKE_OBJCXX_FLAGS_RELEASE "-Os -g -DNDEBUG")
set(CMAKE_OBJC_FLAGS_RELEASE "-Os -g -DNDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-Os -g -DNDEBUG")
set(src "${BREAKPAD_SOURCE_DIR}/src")
set(mac "${src}/client/mac")
set(output "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
get_filename_component(compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
find_program(breakpad_dsymutil dsymutil PATHS "${compiler_bin}" NO_DEFAULT_PATH REQUIRED)

add_library(breakpadUtilities SHARED
    ${src}/common/mac/launch_reporter.cc
    ${src}/common/mac/arch_utilities.cc
    ${mac}/handler/breakpad_nlist_64.cc
    ${src}/common/convert_UTF.cc
    ${mac}/handler/dynamic_images.cc
    ${src}/common/mac/file_id.cc
    ${src}/common/mac/macho_id.cc
    ${src}/common/mac/macho_utilities.cc
    ${src}/common/mac/macho_walker.cc
    ${src}/common/mac/MachIPC.mm
    ${src}/common/md5.cc
    ${src}/client/minidump_file_writer.cc
    ${mac}/handler/minidump_generator.cc
    ${src}/common/mac/string_utilities.cc
    ${src}/common/string_conversion.cc
    ${src}/common/mac/bootstrap_compat.cc
)
target_include_directories(breakpadUtilities PRIVATE ${src})
target_link_libraries(breakpadUtilities PRIVATE "-framework Foundation" "-framework Carbon")
set_target_properties(breakpadUtilities PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${output}/Breakpad.framework/Versions/A/Resources"
    INSTALL_NAME_DIR "@executable_path/../Frameworks/Breakpad.framework/Resources"
    BUILD_WITH_INSTALL_NAME_DIR TRUE
    MACHO_CURRENT_VERSION 1.0.0
    MACHO_COMPATIBILITY_VERSION 1.0.0
)

add_library(Breakpad SHARED
    ${src}/common/mac/HTTPMultipartUpload.m
    ${src}/common/mac/HTTPGetRequest.m
    ${mac}/handler/protected_memory_allocator.cc
    ${src}/common/mac/minidump_upload.m
    ${mac}/handler/exception_handler.cc
    ${src}/common/mac/HTTPPutRequest.m
    ${mac}/Framework/Breakpad.mm
    ${src}/common/mac/launch_reporter.cc
    ${mac}/Framework/OnDemandServer.mm
    ${mac}/crash_generation/crash_generation_client.cc
    ${mac}/crash_generation/crash_generation_server.cc
    ${mac}/crash_generation/ConfigFile.mm
    ${src}/common/mac/arch_utilities.cc
    ${src}/common/simple_string_dictionary.cc
    ${src}/common/mac/HTTPSimplePostRequest.m
    ${src}/common/mac/encoding_util.m
    ${src}/common/mac/HTTPRequest.m
    ${src}/common/mac/SymbolCollectorClient.m
    ${src}/processor/convert_old_arm64_context.cc
    ${mac}/Framework/Breakpad.h
    ${src}/client/apple/Framework/BreakpadDefines.h
)
target_include_directories(Breakpad PRIVATE ${src} ${src}/client/apple/Framework)
target_compile_options(Breakpad PRIVATE -include "${mac}/Framework/Breakpad_Prefix.pch")
target_link_libraries(Breakpad PRIVATE breakpadUtilities "-framework Cocoa")
set(EXECUTABLE_NAME Breakpad)
set(PRODUCT_NAME Breakpad)
configure_file(${mac}/Framework/Info.plist ${CMAKE_CURRENT_BINARY_DIR}/Info.plist)
set_target_properties(Breakpad PROPERTIES
    FRAMEWORK TRUE
    FRAMEWORK_VERSION A
    LIBRARY_OUTPUT_DIRECTORY "${output}"
    INSTALL_NAME_DIR "@executable_path/../Frameworks"
    BUILD_WITH_INSTALL_NAME_DIR TRUE
    MACHO_CURRENT_VERSION 1.0.0
    MACHO_COMPATIBILITY_VERSION 1.0.0
    MACOSX_FRAMEWORK_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
    PUBLIC_HEADER "${mac}/Framework/Breakpad.h;${src}/client/apple/Framework/BreakpadDefines.h"
)
add_custom_command(TARGET Breakpad POST_BUILD
    COMMAND "${breakpad_dsymutil}" "$<TARGET_FILE:Breakpad>"
        -o "$<TARGET_BUNDLE_DIR:Breakpad>.dSYM"
    VERBATIM
)

add_executable(dump_syms
    ${src}/common/mac/arch_utilities.cc
    ${src}/common/mac/macho_reader.cc
    ${src}/common/dwarf/elf_reader.cc
    ${src}/common/dwarf/dwarf2reader.cc
    ${src}/common/path_helper.cc
    ${src}/common/dwarf/bytereader.cc
    ${src}/common/mac/macho_utilities.cc
    ${src}/common/mac/file_id.cc
    ${src}/common/mac/macho_id.cc
    ${src}/common/mac/macho_walker.cc
    ${src}/common/mac/dump_syms.cc
    ${src}/tools/mac/dump_syms/dump_syms_tool.cc
    ${src}/common/dwarf/dwarf2diehandler.cc
    ${src}/common/dwarf_cu_to_module.cc
    ${src}/common/dwarf_line_to_module.cc
    ${src}/common/dwarf_range_list_handler.cc
    ${src}/common/language.cc
    ${src}/common/module.cc
    ${src}/common/dwarf_cfi_to_module.cc
    ${src}/common/stabs_reader.cc
    ${src}/common/stabs_to_module.cc
    ${src}/common/md5.cc
)
set_source_files_properties(
    ${src}/common/mac/dump_syms.cc
    ${src}/tools/mac/dump_syms/dump_syms_tool.cc
    PROPERTIES LANGUAGE OBJCXX
)
target_include_directories(dump_syms PRIVATE
    ${src}
    ${src}/common/mac/include
    ${src}/third_party/musl/include
)
target_compile_definitions(dump_syms PRIVATE HAVE_MACH_O_NLIST_H)
target_compile_options(dump_syms PRIVATE -Wno-deprecated-declarations)
target_link_libraries(dump_syms PRIVATE "-framework Foundation")
set_target_properties(dump_syms PROPERTIES
    CXX_STANDARD 11
    OBJCXX_STANDARD 11
    RUNTIME_OUTPUT_DIRECTORY "${output}"
)

install(DIRECTORY "${output}/Breakpad.framework" DESTINATION "src/client/mac/build/$<CONFIG>")
install(DIRECTORY "${output}/Breakpad.framework.dSYM" DESTINATION "src/client/mac/build/$<CONFIG>")
install(PROGRAMS "$<TARGET_FILE:dump_syms>" DESTINATION "src/tools/mac/dump_syms/build/$<CONFIG>")
