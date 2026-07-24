# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
#
# Vendored libfido2 (+ libcbor): USB-HID CTAP2 security key backend for
# webauthn/webauthn_common.cpp, linked against tdesktop's bundled OpenSSL.
# On Windows it serves only systems without webauthn.dll (Win7/8/8.1,
# Win10 < 1903); winhello.c is left out.

add_library(lib_fido2 STATIC)
init_target(lib_fido2 "(external)")
add_library(tdesktop::lib_fido2 ALIAS lib_fido2)

set(fido2_loc ${third_party_loc}/libfido2)
set(fido2_src ${fido2_loc}/src)
set(fido2_compat ${fido2_loc}/openbsd-compat)
set(cbor_loc ${third_party_loc}/libcbor)
set(fido2_gen ${CMAKE_CURRENT_BINARY_DIR}/fido2_gen)

# --- libcbor generated headers (normally produced by libcbor's own CMake). ---
set(CBOR_VERSION_MAJOR 0)
set(CBOR_VERSION_MINOR 11)
set(CBOR_VERSION_PATCH 0)
set(CBOR_BUFFER_GROWTH 2)
set(CBOR_MAX_STACK_SIZE 2048)
set(CBOR_PRETTY_PRINTER 1)
if (MSVC)
    set(CBOR_RESTRICT_SPECIFIER __restrict)
    set(CBOR_INLINE_SPECIFIER __inline)
    set(cbor_deprecated "__declspec(deprecated)")
else()
    set(CBOR_RESTRICT_SPECIFIER restrict)
    set(CBOR_INLINE_SPECIFIER inline)
    set(cbor_deprecated "__attribute__((__deprecated__))")
endif()
configure_file(
    ${cbor_loc}/src/cbor/configuration.h.in
    ${fido2_gen}/cbor/configuration.h)
file(WRITE ${fido2_gen}/cbor/cbor_export.h "#ifndef CBOR_EXPORT_H
#define CBOR_EXPORT_H
#define CBOR_EXPORT
#define CBOR_NO_EXPORT
#define CBOR_DEPRECATED ${cbor_deprecated}
#define CBOR_DEPRECATED_EXPORT
#define CBOR_DEPRECATED_NO_EXPORT
#endif
")

# --- Sources. ---
file(GLOB cbor_sources CONFIGURE_DEPENDS
    ${cbor_loc}/src/*.c
    ${cbor_loc}/src/cbor/*.c
    ${cbor_loc}/src/cbor/internal/*.c)

set(fido2_sources
    ${fido2_src}/aes256.c
    ${fido2_src}/assert.c
    ${fido2_src}/authkey.c
    ${fido2_src}/bio.c
    ${fido2_src}/blob.c
    ${fido2_src}/buf.c
    ${fido2_src}/cbor.c
    ${fido2_src}/compress.c
    ${fido2_src}/config.c
    ${fido2_src}/cred.c
    ${fido2_src}/credman.c
    ${fido2_src}/dev.c
    ${fido2_src}/ecdh.c
    ${fido2_src}/eddsa.c
    ${fido2_src}/err.c
    ${fido2_src}/es256.c
    ${fido2_src}/es384.c
    ${fido2_src}/hid.c
    ${fido2_src}/info.c
    ${fido2_src}/io.c
    ${fido2_src}/iso7816.c
    ${fido2_src}/largeblob.c
    ${fido2_src}/log.c
    ${fido2_src}/pin.c
    ${fido2_src}/random.c
    ${fido2_src}/reset.c
    ${fido2_src}/rs1.c
    ${fido2_src}/rs256.c
    ${fido2_src}/time.c
    ${fido2_src}/touch.c
    ${fido2_src}/tpm.c
    ${fido2_src}/types.c
    ${fido2_src}/u2f.c
    ${fido2_src}/util.c)

# Platform USB-HID backend.
if (LINUX)
    list(APPEND fido2_sources
        ${fido2_src}/hid_linux.c
        ${fido2_src}/hid_unix.c)
elseif (APPLE)
    list(APPEND fido2_sources
        ${fido2_src}/hid_osx.c)
elseif (WIN32)
    list(APPEND fido2_sources
        ${fido2_src}/hid_win.c)
endif()

# openbsd-compat/*.c are each internally guarded by #ifndef HAVE_<fn>, so the
# fixed upstream list is safe to compile unconditionally: files whose symbol the
# platform already provides collapse to nothing once we define the HAVE_* below.
set(fido2_compat_sources
    ${fido2_compat}/bsd-asprintf.c
    ${fido2_compat}/bsd-getpagesize.c
    ${fido2_compat}/clock_gettime.c
    ${fido2_compat}/endian_win32.c
    ${fido2_compat}/explicit_bzero.c
    ${fido2_compat}/explicit_bzero_win32.c
    ${fido2_compat}/freezero.c
    ${fido2_compat}/recallocarray.c
    ${fido2_compat}/strlcat.c
    ${fido2_compat}/timingsafe_bcmp.c)

target_sources(lib_fido2 PRIVATE
    ${cbor_sources}
    ${fido2_sources}
    ${fido2_compat_sources})

# --- Feature detection (mirrors libfido2's own configure checks). ---
include(CheckSymbolExists)
include(CheckIncludeFiles)
set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
check_include_files(endian.h HAVE_ENDIAN_H)
check_include_files(err.h HAVE_ERR_H)
check_include_files(signal.h HAVE_SIGNAL_H)
check_include_files(sys/random.h HAVE_SYS_RANDOM_H)
check_include_files(unistd.h HAVE_UNISTD_H)
check_symbol_exists(arc4random_buf stdlib.h HAVE_ARC4RANDOM_BUF)
check_symbol_exists(asprintf stdio.h HAVE_ASPRINTF)
check_symbol_exists(clock_gettime time.h HAVE_CLOCK_GETTIME)
check_symbol_exists(explicit_bzero string.h HAVE_EXPLICIT_BZERO)
check_symbol_exists(freezero stdlib.h HAVE_FREEZERO)
check_symbol_exists(getline stdio.h HAVE_GETLINE)
check_symbol_exists(getpagesize unistd.h HAVE_GETPAGESIZE)
check_symbol_exists(getrandom sys/random.h HAVE_GETRANDOM)
check_symbol_exists(memset_s string.h HAVE_MEMSET_S)
check_symbol_exists(recallocarray stdlib.h HAVE_RECALLOCARRAY)
check_symbol_exists(strlcat string.h HAVE_STRLCAT)
check_symbol_exists(strlcpy string.h HAVE_STRLCPY)
check_symbol_exists(strsep string.h HAVE_STRSEP)
check_symbol_exists(sysconf unistd.h HAVE_SYSCONF)
check_symbol_exists(timespecsub sys/time.h HAVE_TIMESPECSUB)
check_symbol_exists(timingsafe_bcmp string.h HAVE_TIMINGSAFE_BCMP)
unset(CMAKE_REQUIRED_DEFINITIONS)

set(fido2_definitions
    _FIDO_INTERNAL
    HAVE_CBOR_H
    HAVE_OPENSSLV_H
    OPENSSL_API_COMPAT=0x10100000L)
if (WIN32)
    list(APPEND fido2_definitions
        _CRT_SECURE_NO_WARNINGS
        "TLS=__declspec(thread)")
else()
    list(APPEND fido2_definitions
        _GNU_SOURCE
        HAVE_DEV_URANDOM
        TLS=__thread)
endif()
foreach(v
    HAVE_ENDIAN_H HAVE_ERR_H HAVE_SIGNAL_H HAVE_SYS_RANDOM_H HAVE_UNISTD_H
    HAVE_ARC4RANDOM_BUF HAVE_ASPRINTF HAVE_CLOCK_GETTIME HAVE_EXPLICIT_BZERO
    HAVE_FREEZERO HAVE_GETLINE HAVE_GETPAGESIZE HAVE_GETRANDOM HAVE_MEMSET_S
    HAVE_RECALLOCARRAY HAVE_STRLCAT HAVE_STRLCPY HAVE_STRSEP HAVE_SYSCONF
    HAVE_TIMESPECSUB HAVE_TIMINGSAFE_BCMP)
    if (${v})
        list(APPEND fido2_definitions ${v})
    endif()
endforeach()
target_compile_definitions(lib_fido2 PRIVATE ${fido2_definitions})

# --- Dependencies. ---
# NOTE: openbsd-compat is deliberately NOT on the include path — libfido2 pulls
# it in via relative quote-includes ("../openbsd-compat/..."), and its shim
# headers (e.g. time.h) #include the *system* header of the same name; putting
# that directory on -I makes <time.h> resolve back to the shim (infinite include
# recursion).
#
# src/ is private too: its webauthn.h would shadow the Windows SDK header of
# that name (webauthn_win.cpp needs the SDK one). A generated forwarding fido.h
# exposes the API without putting src/ on the include path.
file(WRITE ${fido2_gen}/fido.h "#include \"${fido2_src}/fido.h\"\n")

target_include_directories(lib_fido2
PRIVATE
    ${fido2_src}
PUBLIC
    ${cbor_loc}/src
    ${fido2_gen})

target_link_libraries(lib_fido2
PUBLIC
    desktop-app::external_openssl
PRIVATE
    desktop-app::external_zlib)

# Platform HID transport dependencies.
if (LINUX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
    target_link_libraries(lib_fido2 PRIVATE PkgConfig::UDEV)
elseif (APPLE)
    target_link_libraries(lib_fido2
    PRIVATE
        "-framework CoreFoundation"
        "-framework IOKit")
elseif (WIN32)
    target_link_libraries(lib_fido2
    PRIVATE
        bcrypt
        setupapi
        hid
        ws2_32)
endif()

# Silence third-party C warnings. On MSVC the flags must ride an INTERFACE lib
# linked after common_options to land last and beat its /W4 /WX.
if (MSVC)
    add_library(lib_fido2_warnings_off INTERFACE)
    target_compile_options(lib_fido2_warnings_off
    INTERFACE
        /W0
        /WX-)
    target_link_libraries(lib_fido2 PRIVATE lib_fido2_warnings_off)
else()
    target_compile_options(lib_fido2 PRIVATE -w)
endif()
