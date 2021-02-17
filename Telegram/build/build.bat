@echo OFF
setlocal enabledelayedexpansion
set "FullScriptPath=%~dp0"
set "FullExecPath=%cd%"

if not exist "%FullScriptPath%..\..\..\DesktopPrivate" (
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
) else if "%BuildTarget%" equ "uwp64" (
  set "BuildUWP=1"
) else (
  set "BuildUWP=0"
)

if "%BuildTarget%" equ "win64" (
  set "Build64=1"
) else if "%BuildTarget%" equ "uwp64" (
  set "Build64=1"
) else (
  set "Build64=0"
)

if %Build64% neq 0 (
  if "%Platform%" neq "x64" (
    echo Bad environment. Make sure to run from 'x64 Native Tools Command Prompt for VS 2019'.
    exit /b
  ) else if "%VSCMD_ARG_HOST_ARCH%" neq "x64" (
    echo Bad environment. Make sure to run from 'x64 Native Tools Command Prompt for VS 2019'.
    exit /b
  ) else if "%VSCMD_ARG_TGT_ARCH%" neq "x64" (
    echo Bad environment. Make sure to run from 'x64 Native Tools Command Prompt for VS 2019'.
    exit /b
  )
) else (
  if "%Platform%" neq "x86" (
    echo Bad environment. Make sure to run from 'x86 Native Tools Command Prompt for VS 2019'.
    exit /b
  ) else if "%VSCMD_ARG_HOST_ARCH%" neq "x86" (
    echo Bad environment. Make sure to run from 'x86 Native Tools Command Prompt for VS 2019'.
    exit /b
  ) else if "%VSCMD_ARG_TGT_ARCH%" neq "x86" (
    echo Bad environment. Make sure to run from 'x86 Native Tools Command Prompt for VS 2019'.
    exit /b
  )
)

FOR /F "tokens=1,2* delims= " %%i in (%FullScriptPath%version) do set "%%i=%%j"

set "VersionForPacker=%AppVersion%"
if %AlphaVersion% neq 0 (
  set "AppVersion=%AlphaVersion%"
  set "AppVersionStrFull=%AppVersionStr%_%AlphaVersion%"
  set "AlphaBetaParam=-alpha %AlphaVersion%"
  set "AlphaKeyFile=talpha_%AlphaVersion%_key"
) else (
  if %BetaChannel% neq 0 (
    set "AlphaBetaParam=-beta"
    set "AppVersionStrFull=%AppVersionStr%.beta"
  ) else (
    set "AlphaBetaParam="
    set "AppVersionStrFull=%AppVersionStr%"
  )
)

echo.
if %BuildUWP% neq 0 (
  if %Build64% neq 0 (
    echo Building version %AppVersionStrFull% for UWP 64 bit..
  ) else (
    echo Building version %AppVersionStrFull% for UWP..
  )
) else (
  if %Build64% neq 0 (
    echo Building version %AppVersionStrFull% for Windows 64 bit..
  ) else (
    echo Building version %AppVersionStrFull% for Windows..
  )
)
echo.

set "HomePath=%FullScriptPath%.."
set "ResourcesPath=%HomePath%\Resources"
set "SolutionPath=%HomePath%\..\out"
if %Build64% neq 0 (
  set "UpdateFile=tx64upd%AppVersion%"
  set "SetupFile=tsetup-x64.%AppVersionStrFull%.exe"
  set "PortableFile=tportable-x64.%AppVersionStrFull%.zip"
  set "DumpSymsPath=%SolutionPath%\..\..\Libraries\win64\breakpad\src\tools\windows\dump_syms\Release\dump_syms.exe"
) else (
  set "UpdateFile=tupdate%AppVersion%"
  set "SetupFile=tsetup.%AppVersionStrFull%.exe"
  set "PortableFile=tportable.%AppVersionStrFull%.zip"
  set "DumpSymsPath=%SolutionPath%\..\..\Libraries\breakpad\src\tools\windows\dump_syms\Release\dump_syms.exe"
)
set "ReleasePath=%SolutionPath%\Release"
set "DeployPath=%ReleasePath%\deploy\%AppVersionStrMajor%\%AppVersionStrFull%"
set "SignPath=%HomePath%\..\..\DesktopPrivate\Sign.bat"
set "BinaryName=Telegram"
set "DropboxSymbolsPath=Y:\Telegram\symbols"
set "FinalReleasePath=Z:\Projects\backup\tdesktop"

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
if %AlphaVersion% neq 0 (
  if exist %DeployPath%\ (
    echo Deploy folder for version %AppVersionStr% already exists!
    exit /b 1
  )
  if exist %ReleasePath%\%AlphaKeyFile% (
    echo Alpha version key file for version %AppVersion% already exists!
    exit /b 1
  )
) else (
  if exist %ReleasePath%\deploy\%AppVersionStrMajor%\%AppVersionStr%.alpha\ (
    echo Deploy folder for version %AppVersionStr%.alpha already exists!
    exit /b 1
  )
  if exist %ReleasePath%\deploy\%AppVersionStrMajor%\%AppVersionStr%.beta\ (
    echo Deploy folder for version %AppVersionStr%.beta already exists!
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

call configure.bat
if %errorlevel% neq 0 goto error

cd "%SolutionPath%"
call cmake --build . --config Release --target Telegram
if %errorlevel% neq 0 goto error

echo.
echo Version %AppVersionStrFull% build successfull. Preparing..
echo.

if not exist "%DumpSymsPath%" (
  echo Utility dump_syms not found!
  exit /b 1
)

echo Dumping debug symbols..
call "%DumpSymsPath%" "%ReleasePath%\%BinaryName%.pdb" > "%ReleasePath%\%BinaryName%.sym"
echo Done!

set "PATH=%PATH%;C:\Program Files\7-Zip;C:\Program Files (x86)\Inno Setup 5"

cd "%ReleasePath%"

:sign1
call "%SignPath%" "%BinaryName%.exe"
if %errorlevel% neq 0 (
  timeout /t 3
  goto sign1
)

if %BuildUWP% equ 0 (
:sign2
  call "%SignPath%" "Updater.exe"
  if %errorlevel% neq 0 (
    timeout /t 3
    goto sign2
  )

  if %AlphaVersion% equ 0 (
    iscc /dMyAppVersion=%AppVersionStrSmall% /dMyAppVersionZero=%AppVersionStr% /dMyAppVersionFull=%AppVersionStrFull% "/dReleasePath=%ReleasePath%" "/dMyBuildTarget=%BuildTarget%" "%FullScriptPath%setup.iss"
    if %errorlevel% neq 0 goto error
    if not exist "%SetupFile%" goto error
:sign3
    call "%SignPath%" "%SetupFile%"
    if %errorlevel% neq 0 (
      timeout /t 3
      goto sign3
    )
  )

  call Packer.exe -version %VersionForPacker% -path %BinaryName%.exe -path Updater.exe -target %BuildTarget% %AlphaBetaParam%
  if %errorlevel% neq 0 goto error

  if %AlphaVersion% neq 0 (
    if not exist "%ReleasePath%\%AlphaKeyFile%" (
      echo Alpha version key file not found!
      exit /b 1
    )

    FOR /F "tokens=1* delims= " %%i in (%ReleasePath%\%AlphaKeyFile%) do set "AlphaSignature=%%i"
  )
  if %errorlevel% neq 0 goto error

  if %AlphaVersion% neq 0 (
    set "UpdateFile=!UpdateFile!_!AlphaSignature!"
    set "PortableFile=talpha!AlphaVersion!_!AlphaSignature!.zip"
  )
)

for /f ^"usebackq^ eol^=^

^ delims^=^" %%a in (%ReleasePath%\%BinaryName%.sym) do (
  set "SymbolsHashLine=%%a"
  goto symbolslinedone
)
:symbolslinedone
FOR /F "tokens=1,2,3,4* delims= " %%i in ("%SymbolsHashLine%") do set "SymbolsHash=%%l"

echo Copying %BinaryName%.sym to %DropboxSymbolsPath%\%BinaryName%.pdb\%SymbolsHash%
if not exist %DropboxSymbolsPath%\%BinaryName%.pdb mkdir %DropboxSymbolsPath%\%BinaryName%.pdb
if not exist %DropboxSymbolsPath%\%BinaryName%.pdb\%SymbolsHash% mkdir %DropboxSymbolsPath%\%BinaryName%.pdb\%SymbolsHash%
move "%ReleasePath%\%BinaryName%.sym" %DropboxSymbolsPath%\%BinaryName%.pdb\%SymbolsHash%\
echo Done!

if %BuildUWP% neq 0 (
  cd "%HomePath%"

  mkdir "%ReleasePath%\AppX"
  xcopy "Resources\uwp\AppX\*" "%ReleasePath%\AppX\" /E
  set "ResourcePath=%ReleasePath%\AppX\AppxManifest.xml"
  if %Build64% equ 0 (
    call :repl "Argument= (ProcessorArchitecture=)&quot;ARCHITECTURE&quot;/ $1&quot;x86&quot;" "Filename=!ResourcePath!" || goto error
  ) else (
    call :repl "Argument= (ProcessorArchitecture=)&quot;ARCHITECTURE&quot;/ $1&quot;x64&quot;" "Filename=!ResourcePath!" || goto error
  )
  makepri new /pr Resources\uwp\AppX\ /cf Resources\uwp\priconfig.xml /mn %ReleasePath%\AppX\AppxManifest.xml /of %ReleasePath%\AppX\resources.pri
  if %errorlevel% neq 0 goto error

  xcopy "%ReleasePath%\%BinaryName%.exe" "%ReleasePath%\AppX\"

  if %Build64% equ 0 (
    MakeAppx.exe pack /d "%ReleasePath%\AppX" /l /p ..\out\Release\%BinaryName%.x86.appx
  ) else (
    MakeAppx.exe pack /d "%ReleasePath%\AppX" /l /p ..\out\Release\%BinaryName%.x64.appx
  )
  if %errorlevel% neq 0 goto error

  if not exist "%ReleasePath%\deploy" mkdir "%ReleasePath%\deploy"
  if not exist "%ReleasePath%\deploy\%AppVersionStrMajor%" mkdir "%ReleasePath%\deploy\%AppVersionStrMajor%"
  mkdir "%DeployPath%"

  move "%ReleasePath%\%BinaryName%.pdb" "%DeployPath%\"
  if %Build64% equ 0 (
    move "%ReleasePath%\%BinaryName%.x86.appx" "%DeployPath%\"
  ) else (
    move "%ReleasePath%\%BinaryName%.x64.appx" "%DeployPath%\"
  )
  move "%ReleasePath%\%BinaryName%.exe" "%DeployPath%\"

  if "%AlphaBetaParam%" equ "" (
    move "%ReleasePath%\AppX" "%DeployPath%\AppX"
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
  move "%ReleasePath%\%BinaryName%.pdb" "%DeployPath%\"
  move "%ReleasePath%\Updater.pdb" "%DeployPath%\"
  if %AlphaVersion% equ 0 (
    move "%ReleasePath%\%SetupFile%" "%DeployPath%\"
  ) else (
    move "%ReleasePath%\%AlphaKeyFile%" "%DeployPath%\"
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

if %Build64% equ 0 (
  set "FinalDeployPath=%FinalReleasePath%\%AppVersionStrMajor%\%AppVersionStrFull%\tsetup"
) else (
  set "FinalDeployPath=%FinalReleasePath%\%AppVersionStrMajor%\%AppVersionStrFull%\tx64"
)

if %BuildUWP% equ 0 (
  echo.
  echo Version %AppVersionStrFull% is ready for deploy!
  echo.

  if not exist "%DeployPath%\%UpdateFile%" goto error
  if not exist "%DeployPath%\%PortableFile%" goto error
  if %AlphaVersion% equ 0 (
    if not exist "%DeployPath%\%SetupFile%" goto error
  )
  if not exist "%DeployPath%\%BinaryName%.pdb" goto error
  if not exist "%DeployPath%\Updater.exe" goto error
  if not exist "%DeployPath%\Updater.pdb" goto error
  md "%FinalDeployPath%"

  xcopy "%DeployPath%\%UpdateFile%" "%FinalDeployPath%\" /Y
  xcopy "%DeployPath%\%PortableFile%" "%FinalDeployPath%\" /Y
  if %AlphaVersion% equ 0 (
    xcopy "%DeployPath%\%SetupFile%" "%FinalDeployPath%\" /Y
  ) else (
    xcopy "%DeployPath%\%AlphaKeyFile%" "%FinalDeployPath%\" /Y
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
