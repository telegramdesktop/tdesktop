@echo OFF

setlocal enabledelayedexpansion
set "FullScriptPath=%~dp0"

call %FullScriptPath%..\..\ThirdParty\python\Scripts\activate.bat
python %FullScriptPath%configure.py %*
if %errorlevel% neq 0 goto error

exit /b

:error
echo FAILED
exit /b 1
