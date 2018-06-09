@echo off

IF "%BUILD_DIR%"=="" SET BUILD_DIR=C:\TBuild
SET LIB_DIR=%BUILD_DIR%\Libraries
SET SRC_DIR=%BUILD_DIR%\tdesktop
SET QT_VERSION=5_6_2

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
    git clone https://github.com/Microsoft/Range-V3-VS2015 range-v3
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
    SET PATH=%PATH%;%BUILD_DIR%\Libraries\gyp;%BUILD_DIR%\Libraries\ninja;
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

    if not "%TDESKTOP_BUILD_DEFINES%" == "" (
        set "TDESKTOP_BUILD_DEFINES=%TDESKTOP_BUILD_DEFINES:~1%"
    )

    call:logInfo "Build Defines: %TDESKTOP_BUILD_DEFINES%"
GOTO:EOF
