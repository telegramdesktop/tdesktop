@echo OFF
setlocal enabledelayedexpansion
set "FullScriptPath=%~dp0"
set "FullExecPath=%cd%"

cd "%FullScriptPath%"
call gyp --depth=. --generator-output=../build/msvs -Goutput_dir=../../../out Telegram.gyp --format=ninja
call gyp --depth=. --generator-output=../build/msvs -Goutput_dir=../../../out Telegram.gyp --format=msvs-ninja
cd "%FullExecPath%"

exit /b
