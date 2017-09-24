@echo off

SET BUILD_DIR=C:\TBuild
SET LIB_DIR=%BUILD_DIR%\Libraries
SET SRC_DIR=%BUILD_DIR%\tdesktop
SET QT_VERSION=5_6_2

call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional\VC\Auxiliary\Build\vcvarsall.bat" x86

call:configureBuild
call:getDependencies
call:setupGYP
cd %SRC_DIR%

echo Finished!

GOTO:EOF

:: FUNCTIONS
:logInfo
    echo [INFO] %~1
GOTO:EOF

:logError
    echo [ERROR] %~1
GOTO:EOF

:getDependencies
    call:logInfo "Clone dependencies repository"
    git clone -q --depth 1 --branch=master https://github.com/telegramdesktop/dependencies_windows.git %LIB_DIR%
    cd %LIB_DIR%

    if exist prepare.bat (
        call prepare.bat
    ) else (
        call:logError "Error cloning dependencies, trying again"
        rmdir %LIB_DIR% /S /Q
        call:getDependencies
    )
GOTO:EOF

:setupGYP
    call:logInfo "Setup GYP/Ninja and generate VS solution"
    cd %LIB_DIR%
    git clone https://chromium.googlesource.com/external/gyp
    cd gyp
    git checkout a478c1ab51
    SET PATH=%PATH%;C:\TBuild\Libraries\gyp;C:\TBuild\Libraries\ninja;
    cd %SRC_DIR%
    git submodule init
    git submodule update
    cd %SRC_DIR%\Telegram
    call gyp\refresh.bat
GOTO:EOF

:configureBuild
    call:logInfo "Configuring build"
    call:logInfo "Build version: %BUILD_VERSION%"
    set TDESKTOP_BUILD_DEFINES=

    echo %BUILD_VERSION% | findstr /C:"disable_autoupdate">nul && (
        set TDESKTOP_BUILD_DEFINES=%TDESKTOP_BUILD_DEFINES%,TDESKTOP_DISABLE_AUTOUPDATE
    )

    echo %BUILD_VERSION% | findstr /C:"disable_register_custom_scheme">nul && (
        set TDESKTOP_BUILD_DEFINES=%TDESKTOP_BUILD_DEFINES%,TDESKTOP_DISABLE_REGISTER_CUSTOM_SCHEME
    )

    echo %BUILD_VERSION% | findstr /C:"disable_crash_reports">nul && (
        set TDESKTOP_BUILD_DEFINES=%TDESKTOP_BUILD_DEFINES%,TDESKTOP_DISABLE_CRASH_REPORTS
    )

    echo %BUILD_VERSION% | findstr /C:"disable_network_proxy">nul && (
        set TDESKTOP_BUILD_DEFINES=%TDESKTOP_BUILD_DEFINES%,TDESKTOP_DISABLE_NETWORK_PROXY
    )

    echo %BUILD_VERSION% | findstr /C:"disable_desktop_file_generation">nul && (
        set TDESKTOP_BUILD_DEFINES=%TDESKTOP_BUILD_DEFINES%,TDESKTOP_DISABLE_DESKTOP_FILE_GENERATION
    )

    echo %BUILD_VERSION% | findstr /C:"disable_unity_integration">nul && (
        set TDESKTOP_BUILD_DEFINES=%TDESKTOP_BUILD_DEFINES%,TDESKTOP_DISABLE_UNITY_INTEGRATION
    )

    if not "%TDESKTOP_BUILD_DEFINES%" == "" (
        set "TDESKTOP_BUILD_DEFINES=%TDESKTOP_BUILD_DEFINES:~1%"
    )

    call:logInfo "Build Defines: %TDESKTOP_BUILD_DEFINES%"
GOTO:EOF
