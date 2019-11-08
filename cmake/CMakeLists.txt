add_subdirectory(external)

add_library(with_ranges INTERFACE)

if (WIN32)
    target_compile_options(with_ranges
    INTERFACE
        /experimental:preprocessor # need for range-v3 see https://github.com/ericniebler/range-v3#supported-compilers
        /wd5105 # needed for `/experimental:preprocessor`, suppressing C5105 "macro expansion producing 'defined' has undefined behavior"
    )
endif()

add_library(common_no_ranges INTERFACE)

target_compile_features(common_no_ranges
INTERFACE
    cxx_std_17
)

target_compile_definitions(common_no_ranges
INTERFACE
    UNICODE
)

if (WIN32)
    target_compile_definitions(common_no_ranges
    INTERFACE
        WIN32
        _WINDOWS
        _UNICODE
        UNICODE
        # HAVE_STDINT_H
        # ZLIB_WINAPI
        _SCL_SECURE_NO_WARNINGS
        _USING_V110_SDK71_
        NOMINMAX
    )
    target_compile_options(common_no_ranges
    INTERFACE
        /permissive-
        # /Qspectre
        /W1
        /WX
        /MP     # Enable multi process build.
        /EHsc   # Catch C++ exceptions only, extern C functions never throw a C++ exception.
        /w14834 # [[nodiscard]]
        /w15038 # wrong initialization order
        /w14265 # class has virtual functions, but destructor is not virtual
        /wd4068 # Disable "warning C4068: unknown pragma"
        /wd5105 # needed for `/experimental:preprocessor`, suppressing C5105 "macro expansion producing 'defined' has undefined behavior"
        /Zc:wchar_t- # don't tread wchar_t as builtin type
    )
    target_link_libraries(common_no_ranges
    INTERFACE
        winmm
        imm32
        ws2_32
        kernel32
        user32
        gdi32
        winspool
        comdlg32
        advapi32
        shell32
        ole32
        oleaut32
        uuid
        odbc32
        odbccp32
        Shlwapi
        Iphlpapi
        Gdiplus
        Strmiids
        Netapi32
        Userenv
        Version
        Dwmapi
        Wtsapi32
        UxTheme
        DbgHelp
        Rstrtmgr
      )
else()
endif()

add_library(common INTERFACE)

target_link_libraries(common
INTERFACE
    common_no_ranges
    with_ranges
)