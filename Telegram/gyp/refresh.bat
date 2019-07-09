@echo OFF

setlocal enabledelayedexpansion
set "FullScriptPath=%~dp0"

python %FullScriptPath%generate.py %1 %2 %3 %4 %5 %6
if %errorlevel% neq 0 goto error

exit /b

:error
echo FAILED
exit /b 1
