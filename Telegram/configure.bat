@echo OFF

setlocal enabledelayedexpansion
set "FullScriptPath=%~dp0"

python %FullScriptPath%configure.py %*
if %errorlevel% neq 0 goto error

exit /b

:error
echo FAILED
exit /b 1
