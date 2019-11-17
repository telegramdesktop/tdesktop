target_compile_definitions(common_options
INTERFACE
    WIN32
    _WINDOWS
    _UNICODE
    UNICODE
    _SCL_SECURE_NO_WARNINGS
    _USING_V110_SDK71_
    NOMINMAX
)
target_compile_options(common_options
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
    /Zc:wchar_t- # don't tread wchar_t as builtin type
)

target_link_options(common_options
INTERFACE
    /NODEFAULTLIB:LIBCMT
)

target_link_libraries(common_options
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
    Crypt32
)
