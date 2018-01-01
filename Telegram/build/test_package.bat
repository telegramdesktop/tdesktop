@echo OFF
setlocal enabledelayedexpansion
set "FullScriptPath=%~dp0"
set "FullExecPath=%cd%"

if not exist "%FullScriptPath%..\..\..\TelegramPrivate" (
  echo.
  echo This script is for building the production version of Telegram Desktop.
  echo.
  echo For building custom versions please visit the build instructions page at:
  echo https://github.com/telegramdesktop/tdesktop/#build-instructions
  exit /b
)

set "HomePath=%FullScriptPath%.."
set "SignAppxPath=%HomePath%\..\..\TelegramPrivate\AppxSign.bat"
set "ResourcesPath=%HomePath%\Resources"
set "SolutionPath=%HomePath%\.."
set "ReleasePath=%HomePath%\..\out\Debug"
set "BinaryName=Telegram"

if exist %ReleasePath%\AppX\ (
  echo Result folder out\Debug\AppX already exists!
  exit /b 1
)

cd "%HomePath%"

call gyp\refresh.bat
if %errorlevel% neq 0 goto error

cd "%SolutionPath%"
call ninja -C out/Debug Telegram
if %errorlevel% neq 0 goto error

cd "%HomePath%"

mkdir "%ReleasePath%\AppX"
xcopy "Resources\uwp\AppX\*" "%ReleasePath%\AppX\" /E

set "ResourcePath=%ReleasePath%\AppX\AppxManifest.xml"
call :repl "Argument= (Publisher=)&quot;CN=536BC709-8EE1-4478-AF22-F0F0F26FF64A&quot;/ $1&quot;CN=Telegram Messenger LLP, O=Telegram Messenger LLP, L=London, C=GB&quot;" "Filename=%ResourcePath%" || goto :error
call :repl "Argument= (ProcessorArchitecture=)&quot;ARCHITECTURE&quot;/ $1&quot;x64&quot;" "Filename=%ResourcePath%" || goto :error

makepri new /pr Resources\uwp\AppX\ /cf Resources\uwp\priconfig.xml /mn %ReleasePath%\AppX\AppxManifest.xml /of %ReleasePath%\AppX\resources.pri
if %errorlevel% neq 0 goto error

xcopy "%ReleasePath%\%BinaryName%.exe" "%ReleasePath%\AppX\"

MakeAppx.exe pack /d "%ReleasePath%\AppX" /l /p ..\out\Debug\%BinaryName%.appx
if %errorlevel% neq 0 goto error

call "%SignAppxPath%" "..\out\Debug\%BinaryName%.appx"

move "%ReleasePath%\%BinaryName%.appx" "%ReleasePath%\AppX\"

echo Done.

exit /b

:repl
(
  set %1
  set %2
  set "TempFilename=!Filename!__tmp__"
  cscript //Nologo "%FullScriptPath%replace.vbs" "Replace" "!Argument!" < "!Filename!" > "!TempFilename!" || goto :repl_finish
  xcopy /Y !TempFilename! !Filename! >NUL || goto :repl_finish
  goto :repl_finish
)

:repl_finish
(
  set ErrorCode=%errorlevel%
  if !ErrorCode! neq 0 (
    echo Replace error !ErrorCode!
    echo While replacing "%Replace%"
    echo In file "%Filename%"
  )
  del %TempFilename%
  exit /b !ErrorCode!
)
