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

FOR /F "tokens=1* delims= " %%i in (%FullScriptPath%target) do set "BuildTarget=%%i"

if "%BuildTarget%" equ "uwp" (
  set "BuildUWP=1"
) else (
  set "BuildUWP=0"
)

FOR /F "tokens=1,2* delims= " %%i in (%FullScriptPath%version) do set "%%i=%%j"

set "VersionForPacker=%AppVersion%"
if %BetaVersion% neq 0 (
  set "AppVersion=%BetaVersion%"
  set "AppVersionStrFull=%AppVersionStr%_%BetaVersion%"
  set "AlphaBetaParam=-beta %BetaVersion%"
  set "BetaKeyFile=tbeta_%BetaVersion%_key"
) else (
  if %AlphaChannel% neq 0 (
    set "AlphaBetaParam=-alpha"
    set "AppVersionStrFull=%AppVersionStr%.alpha"
  ) else (
    set "AlphaBetaParam="
    set "AppVersionStrFull=%AppVersionStr%"
  )
)

echo.
if %BuildUWP% neq 0 (
  echo Building version %AppVersionStrFull% for UWP..
) else (
  echo Building version %AppVersionStrFull% for Windows..
)
echo.

set "HomePath=%FullScriptPath%.."
set "ResourcesPath=%HomePath%\Resources"
set "SolutionPath=%HomePath%\.."
set "UpdateFile=tupdate%AppVersion%"
set "SetupFile=tsetup.%AppVersionStrFull%.exe"
set "PortableFile=tportable.%AppVersionStrFull%.zip"
set "ReleasePath=%HomePath%\..\out\Release"
set "DeployPath=%ReleasePath%\deploy\%AppVersionStrMajor%\%AppVersionStrFull%"
set "SignPath=%HomePath%\..\..\TelegramPrivate\Sign.bat"
set "SignAppxPath=%HomePath%\..\..\TelegramPrivate\AppxSign.bat"
set "BinaryName=Telegram"
set "DropboxSymbolsPath=Y:\Telegram\symbols"
set "FinalReleasePath=Z:\Telegram\backup"

if not exist %DropboxSymbolsPath% (
  echo Dropbox path %DropboxSymbolsPath% not found!
  exit /b 1
)

if not exist %FinalReleasePath% (
  echo Release path %FinalReleasePath% not found!
  exit /b 1
)

if %BuildUWP% neq 0 (
  if exist %ReleasePath%\AppX\ (
    echo Result folder out\Release\AppX already exists!
    exit /b 1
  )
)
if %BetaVersion% neq 0 (
  if exist %DeployPath%\ (
    echo Deploy folder for version %AppVersionStr% already exists!
    exit /b 1
  )
  if exist %ReleasePath%\%BetaKeyFile% (
    echo Beta version key file for version %AppVersion% already exists!
    exit /b 1
  )
) else (
  if exist %ReleasePath%\deploy\%AppVersionStrMajor%\%AppVersionStr%.alpha\ (
    echo Deploy folder for version %AppVersionStr%.alpha already exists!
    exit /b 1
  )
  if exist %ReleasePath%\deploy\%AppVersionStrMajor%\%AppVersionStr%.dev\ (
    echo Deploy folder for version %AppVersionStr%.dev already exists!
    exit /b 1
  )
  if exist %ReleasePath%\deploy\%AppVersionStrMajor%\%AppVersionStr%\ (
    echo Deploy folder for version %AppVersionStr% already exists!
    exit /b 1
  )
  if exist %ReleasePath%\tupdate%AppVersion% (
    echo Update file for version %AppVersion% already exists!
    exit /b 1
  )
)

cd "%HomePath%"

call gyp\refresh.bat
if %errorlevel% neq 0 goto error

cd "%SolutionPath%"
call ninja -C out/Release Telegram
if %errorlevel% neq 0 goto error

echo.
echo Version %AppVersionStrFull% build successfull. Preparing..
echo.

if not exist "%SolutionPath%\..\Libraries\breakpad\src\tools\windows\dump_syms\Release\dump_syms.exe" (
  echo Utility dump_syms not found!
  exit /b 1
)

echo Dumping debug symbols..
xcopy "%ReleasePath%\%BinaryName%.exe" "%ReleasePath%\%BinaryName%.exe.exe*"
call "%SolutionPath%\..\Libraries\breakpad\src\tools\windows\dump_syms\Release\dump_syms.exe" "%ReleasePath%\%BinaryName%.exe.pdb" > "%ReleasePath%\%BinaryName%.exe.sym"
del "%ReleasePath%\%BinaryName%.exe.exe"
echo Done!

set "PATH=%PATH%;C:\Program Files\7-Zip;C:\Program Files (x86)\Inno Setup 5"

cd "%ReleasePath%"
call "%SignPath%" "%BinaryName%.exe"
if %errorlevel% neq 0 goto error

if %BuildUWP% equ 0 (
  call "%SignPath%" "Updater.exe"
  if %errorlevel% neq 0 goto error

  if %BetaVersion% equ 0 (
    iscc /dMyAppVersion=%AppVersionStrSmall% /dMyAppVersionZero=%AppVersionStr% /dMyAppVersionFull=%AppVersionStrFull% "/dReleasePath=%ReleasePath%" "%FullScriptPath%setup.iss"
    if %errorlevel% neq 0 goto error
    if not exist "tsetup.%AppVersionStrFull%.exe" goto error

    call "%SignPath%" "tsetup.%AppVersionStrFull%.exe"
    if %errorlevel% neq 0 goto error
  )

  call Packer.exe -version %VersionForPacker% -path %BinaryName%.exe -path Updater.exe %AlphaBetaParam%
  if %errorlevel% neq 0 goto error

  if %BetaVersion% neq 0 (
    if not exist "%ReleasePath%\%BetaKeyFile%" (
      echo Beta version key file not found!
      exit /b 1
    )

    FOR /F "tokens=1* delims= " %%i in (%ReleasePath%\%BetaKeyFile%) do set "BetaSignature=%%i"
  )
  if %errorlevel% neq 0 goto error

  if %BetaVersion% neq 0 (
    set "UpdateFile=!UpdateFile!_!BetaSignature!"
    set "PortableFile=tbeta!BetaVersion!_!BetaSignature!.zip"
  )
)

for /f ^"usebackq^ eol^=^

^ delims^=^" %%a in (%ReleasePath%\%BinaryName%.exe.sym) do (
  set "SymbolsHashLine=%%a"
  goto symbolslinedone
)
:symbolslinedone
FOR /F "tokens=1,2,3,4* delims= " %%i in ("%SymbolsHashLine%") do set "SymbolsHash=%%l"

echo Copying %BinaryName%.exe.sym to %DropboxSymbolsPath%\%BinaryName%.exe.pdb\%SymbolsHash%
if not exist %DropboxSymbolsPath%\%BinaryName%.exe.pdb mkdir %DropboxSymbolsPath%\%BinaryName%.exe.pdb
if not exist %DropboxSymbolsPath%\%BinaryName%.exe.pdb\%SymbolsHash% mkdir %DropboxSymbolsPath%\%BinaryName%.exe.pdb\%SymbolsHash%
move "%ReleasePath%\%BinaryName%.exe.sym" %DropboxSymbolsPath%\%BinaryName%.exe.pdb\%SymbolsHash%\
echo Done!

if %BuildUWP% neq 0 (
  cd "%HomePath%"

  mkdir "%ReleasePath%\AppX_x86"
  xcopy "Resources\uwp\AppX\*" "%ReleasePath%\AppX_x86\" /E
  set "ResourcePath=%ReleasePath%\AppX_x86\AppxManifest.xml"
  call :repl "Argument= (ProcessorArchitecture=)&quot;ARCHITECTURE&quot;/ $1&quot;x86&quot;" "Filename=!ResourcePath!" || goto error

  makepri new /pr Resources\uwp\AppX\ /cf Resources\uwp\priconfig.xml /mn %ReleasePath%\AppX_x86\AppxManifest.xml /of %ReleasePath%\AppX_x86\resources.pri
  if %errorlevel% neq 0 goto error

  xcopy "%ReleasePath%\%BinaryName%.exe" "%ReleasePath%\AppX_x86\"

  MakeAppx.exe pack /d "%ReleasePath%\AppX_x86" /l /p ..\out\Release\%BinaryName%.x86.appx
  if %errorlevel% neq 0 goto error

  mkdir "%ReleasePath%\AppX_x64"
  xcopy "Resources\uwp\AppX\*" "%ReleasePath%\AppX_x64\" /E
  set "ResourcePath=%ReleasePath%\AppX_x64\AppxManifest.xml"
  call :repl "Argument= (ProcessorArchitecture=)&quot;ARCHITECTURE&quot;/ $1&quot;x64&quot;" "Filename=!ResourcePath!" || goto error

  makepri new /pr Resources\uwp\AppX\ /cf Resources\uwp\priconfig.xml /mn %ReleasePath%\AppX_x64\AppxManifest.xml /of %ReleasePath%\AppX_x64\resources.pri
  if %errorlevel% neq 0 goto error

  xcopy "%ReleasePath%\%BinaryName%.exe" "%ReleasePath%\AppX_x64\"

  MakeAppx.exe pack /d "%ReleasePath%\AppX_x64" /l /p ..\out\Release\%BinaryName%.x64.appx
  if %errorlevel% neq 0 goto error

  if not exist "%ReleasePath%\deploy" mkdir "%ReleasePath%\deploy"
  if not exist "%ReleasePath%\deploy\%AppVersionStrMajor%" mkdir "%ReleasePath%\deploy\%AppVersionStrMajor%"
  mkdir "%DeployPath%"

  xcopy "%ReleasePath%\%BinaryName%.pdb" "%DeployPath%\"
  move "%ReleasePath%\%BinaryName%.exe.pdb" "%DeployPath%\"
  move "%ReleasePath%\%BinaryName%.x86.appx" "%DeployPath%\"
  move "%ReleasePath%\%BinaryName%.x64.appx" "%DeployPath%\"
  move "%ReleasePath%\%BinaryName%.exe" "%DeployPath%\"

  if "%AlphaBetaParam%" equ "" (
    move "%ReleasePath%\AppX_x86" "%DeployPath%\AppX_x86"
    move "%ReleasePath%\AppX_x64" "%DeployPath%\AppX_x64"
  ) else (
    echo Leaving result in out\Release\AppX_arch for now..
  )
) else (
  if not exist "%ReleasePath%\deploy" mkdir "%ReleasePath%\deploy"
  if not exist "%ReleasePath%\deploy\%AppVersionStrMajor%" mkdir "%ReleasePath%\deploy\%AppVersionStrMajor%"
  mkdir "%DeployPath%"
  mkdir "%DeployPath%\%BinaryName%"
  if %errorlevel% neq 0 goto error

  move "%ReleasePath%\%BinaryName%.exe" "%DeployPath%\%BinaryName%\"
  move "%ReleasePath%\Updater.exe" "%DeployPath%\"
  xcopy "%ReleasePath%\%BinaryName%.pdb" "%DeployPath%\"
  xcopy "%ReleasePath%\Updater.pdb" "%DeployPath%\"
  move "%ReleasePath%\%BinaryName%.exe.pdb" "%DeployPath%\"
  move "%ReleasePath%\Updater.exe.pdb" "%DeployPath%\"
  if %BetaVersion% equ 0 (
    move "%ReleasePath%\%SetupFile%" "%DeployPath%\"
  ) else (
    move "%ReleasePath%\%BetaKeyFile%" "%DeployPath%\"
  )
  move "%ReleasePath%\%UpdateFile%" "%DeployPath%\"
  if %errorlevel% neq 0 goto error

  cd "%DeployPath%"
  7z a -mx9 %PortableFile% %BinaryName%\
  if %errorlevel% neq 0 goto error

  move "%DeployPath%\%BinaryName%\%BinaryName%.exe" "%DeployPath%\"
  rmdir "%DeployPath%\%BinaryName%"
  if %errorlevel% neq 0 goto error
)

set "FinalDeployPath=%FinalReleasePath%\%AppVersionStrMajor%\%AppVersionStrFull%\tsetup"

if %BuildUWP% equ 0 (
  echo.
  echo Version %AppVersionStrFull% is ready for deploy!
  echo.

  if not exist "%DeployPath%\%UpdateFile%" goto error
  if not exist "%DeployPath%\%PortableFile%" goto error
  if %BetaVersion% equ 0 (
    if not exist "%DeployPath%\%SetupFile%" goto error
  )
  if not exist "%DeployPath%\%BinaryName%.pdb" goto error
  if not exist "%DeployPath%\%BinaryName%.exe.pdb" goto error
  if not exist "%DeployPath%\Updater.exe" goto error
  if not exist "%DeployPath%\Updater.pdb" goto error
  if not exist "%DeployPath%\Updater.exe.pdb" goto error
  md "%FinalDeployPath%"

  xcopy "%DeployPath%\%UpdateFile%" "%FinalDeployPath%\" /Y
  xcopy "%DeployPath%\%PortableFile%" "%FinalDeployPath%\" /Y
  if %BetaVersion% equ 0 (
    xcopy "%DeployPath%\%SetupFile%" "%FinalDeployPath%\" /Y
  ) else (
    xcopy "%DeployPath%\%BetaKeyFile%" "%FinalDeployPath%\" /Y
  )
)

echo Version %AppVersionStrFull% is ready!

cd "%FullExecPath%"
exit /b

:error
(
  set ErrorCode=%errorlevel%
  if !ErrorCode! neq 0 (
    echo Error !ErrorCode!
  ) else (
    echo Error 666
    set ErrorCode=666
  )
  cd "%FullExecPath%"
  exit /b !ErrorCode!
)

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
