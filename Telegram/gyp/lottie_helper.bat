@echo OFF

setlocal enabledelayedexpansion
set "FullScriptPath=%~dp0"

python %FullScriptPath%lottie_helper.py
if %errorlevel% neq 0 goto error

exit /b

:error
echo FAILED
exit /b 1
