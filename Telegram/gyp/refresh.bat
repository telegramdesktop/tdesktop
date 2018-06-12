@echo OFF
setlocal EnableDelayedExpansion
set "FullScriptPath=%~dp0"
set "FullExecPath=%cd%"

set "Silence=>nul"
if "%1" == "-v" set "Silence="

if exist "%FullScriptPath%..\build\target" (
  FOR /F "tokens=1* delims= " %%i in (%FullScriptPath%..\build\target) do set "BuildTarget=%%i"
) else (
  set "BuildTarget="
)

rem strangely linking of Release Telegram build complains about the absence of lib.pdb
if exist "%FullScriptPath%..\..\..\Libraries\openssl\tmp32\lib.pdb" (
  if not exist "%FullScriptPath%..\..\..\Libraries\openssl\Release\lib\lib.pdb" (
    xcopy "%FullScriptPath%..\..\..\Libraries\openssl\tmp32\lib.pdb" "%FullScriptPath%..\..\..\Libraries\openssl\Release\lib\" %Silence%
  )
)

set BUILD_DEFINES=
if not "%TDESKTOP_BUILD_DEFINES%" == "" (
  set "BUILD_DEFINES=-Dbuild_defines=%TDESKTOP_BUILD_DEFINES%"
  echo [INFO] Set build defines to !BUILD_DEFINES!
)

set GYP_MSVS_VERSION=2017

cd "%FullScriptPath%"
call gyp --depth=. --generator-output=.. -Goutput_dir=../out !BUILD_DEFINES! -Dofficial_build_target=%BuildTarget% Telegram.gyp --format=ninja
if %errorlevel% neq 0 goto error
call gyp --depth=. --generator-output=.. -Goutput_dir=../out !BUILD_DEFINES! -Dofficial_build_target=%BuildTarget% Telegram.gyp --format=msvs-ninja
if %errorlevel% neq 0 goto error
cd ../..

rem looks like ninja build works without sdk 7.1 which was used by generating custom environment.arch files

rem cd "%FullScriptPath%"
rem call gyp --depth=. --generator-output=../.. -Goutput_dir=out -Gninja_use_custom_environment_files=1 Telegram.gyp --format=ninja
rem if %errorlevel% neq 0 goto error
rem call gyp --depth=. --generator-output=../.. -Goutput_dir=out -Gninja_use_custom_environment_files=1 Telegram.gyp --format=msvs-ninja
rem if %errorlevel% neq 0 goto error
rem cd ../..

rem call msbuild /target:SetBuildDefaultEnvironmentVariables Telegram.vcxproj /fileLogger %Silence%
rem if %errorlevel% neq 0 goto error

rem call python "%FullScriptPath%create_env.py"
rem if %errorlevel% neq 0 goto error

rem call move environment.x86 out\Debug\ %Silence%
rem if %errorlevel% neq 0 goto error

cd "%FullExecPath%"
exit /b

:error
echo FAILED
if exist "%FullScriptPath%..\..\msbuild.log" del "%FullScriptPath%..\..\msbuild.log"
if exist "%FullScriptPath%..\..\environment.x86" del "%FullScriptPath%..\..\environment.x86"
cd "%FullExecPath%"
exit /b 1
