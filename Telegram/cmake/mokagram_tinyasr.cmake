# Mokagram: локальное распознавание речи. Статическая библиотека tinyasr
# (CLI C API, без зависимостей) строится из вендоренного исходника в дереве
# сборки. Крейт собирается с --no-default-features: потоки и std из Rust не
# нужны, распознавание идёт в собственном фоновом потоке клиента. Так в
# бинарнике остаётся один Rust-рантайм: второй (wallet_engine) уже собран в
# общий архив tdesktop_rust, а два staticlib с std дали бы дубли символов.

find_program(MOKAGRAM_CARGO cargo)
if (NOT MOKAGRAM_CARGO)
    message(FATAL_ERROR "cargo is required to build the bundled tinyasr library")
endif()

set(tinyasr_source ${CMAKE_CURRENT_LIST_DIR}/../ThirdParty/tinyasr)
set(tinyasr_build ${CMAKE_CURRENT_BINARY_DIR}/tinyasr)
set(tinyasr_archive ${tinyasr_build}/release/libtinyasr.a)

add_custom_command(
OUTPUT ${tinyasr_archive}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${tinyasr_build}
    COMMAND ${MOKAGRAM_CARGO} build --release --no-default-features --features c-api
        --manifest-path ${tinyasr_source}/Cargo.toml
        --target-dir ${tinyasr_build}
    DEPENDS
        ${tinyasr_source}/Cargo.toml
        ${tinyasr_source}/src/compat.rs
        ${tinyasr_source}/src/fbank.rs
        ${tinyasr_source}/src/ffi.rs
        ${tinyasr_source}/src/file.rs
        ${tinyasr_source}/src/kernels.rs
        ${tinyasr_source}/src/lib.rs
        ${tinyasr_source}/src/model.rs
        ${tinyasr_source}/src/no_std_runtime.rs
        ${tinyasr_source}/src/onnx.rs
        ${tinyasr_source}/src/par.rs
        ${tinyasr_source}/src/session.rs
        ${tinyasr_source}/src/text.rs
        ${tinyasr_source}/src/vad.rs
    VERBATIM
    COMMENT "Building bundled tinyasr"
)

add_custom_target(mokagram_tinyasr DEPENDS ${tinyasr_archive})

add_library(mokagram_tinyasr_lib STATIC IMPORTED GLOBAL)
set_target_properties(mokagram_tinyasr_lib PROPERTIES
    IMPORTED_LOCATION ${tinyasr_archive}
)
add_dependencies(mokagram_tinyasr_lib mokagram_tinyasr)

add_library(mokagram::tinyasr INTERFACE IMPORTED GLOBAL)
target_include_directories(mokagram::tinyasr INTERFACE ${tinyasr_source}/include)
target_link_libraries(mokagram::tinyasr INTERFACE mokagram_tinyasr_lib)
if (UNIX AND NOT APPLE)
    target_link_libraries(mokagram::tinyasr INTERFACE dl pthread m)
endif()
