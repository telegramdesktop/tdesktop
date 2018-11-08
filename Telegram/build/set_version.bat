@echo OFF

set "FullScriptPath=%~dp0"

python %FullScriptPath%set_version.py %1
if %errorlevel% neq 0 goto error

exit /b

:error
echo FAILED
exit /b 1
