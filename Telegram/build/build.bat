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

set "Build64=0"
set "BuildARM=0"
set "BuildUWP=0"
if "%BuildTarget%" equ "win64" (
  set "Build64=1"
) else if "%BuildTarget%" equ "winarm" (
  set "BuildARM=1"
) else if "%BuildTarget%" equ "uwp" (
  set "BuildUWP=1"
) else if "%BuildTarget%" equ "uwp64" (
  set "Build64=1"
  set "BuildUWP=1"
) else if "%BuildTarget%" equ "uwparm" (
  set "BuildARM=1"
  set "BuildUWP=1"
)

set "HomePath=%FullScriptPath%.."
set "ResourcesPath=%HomePath%\Resources"
rem Update signing settings. Edit these directly when rotating keys, they are
rem the single source of truth (no environment overrides).
set "ReleaseCloudVault=tdesktop-release-kv"
set "ReleaseCloudKeyId=rc-2026a"
set "ReleaseCloudKeyName=release-2026a"
set "ReleaseLocalKey=%HomePath%\..\..\DesktopPrivate\modern\release-local-2026b.pem"
set "ReleaseLocalKeyId=rl-2026b"
if %BuildUWP% equ 0 (
  if not exist "%ReleaseLocalKey%" (
    echo Release local key not found: %ReleaseLocalKey%
    exit /b 1
  )
  python "%FullScriptPath%sign_update.py" --check --az-vault "%ReleaseCloudVault%" --az-key "%ReleaseCloudKeyName%" --keys-loc "%ResourcesPath%\update" --key-id %ReleaseCloudKeyId% || goto error
)

if %Build64% neq 0 (
  if "%Platform%" neq "x64" (
    echo Bad environment. Make sure to run from 'x64 Native Tools Command Prompt for VS 2022'.
    exit /b
  ) else if "%VSCMD_ARG_HOST_ARCH%" neq "x64" (
    echo Bad environment. Make sure to run from 'x64 Native Tools Command Prompt for VS 2022'.
    exit /b
  ) else if "%VSCMD_ARG_TGT_ARCH%" neq "x64" (
    echo Bad environment. Make sure to run from 'x64 Native Tools Command Prompt for VS 2022'.
    exit /b
  )
) else if %BuildARM% neq 0 (
  if "%Platform%" neq "arm64" (
    echo Bad environment. Make sure to run from 'ARM64 Native Tools Command Prompt for VS 2022'.
    exit /b
  ) else if "%VSCMD_ARG_HOST_ARCH%" neq "arm64" (
    echo Bad environment. Make sure to run from 'ARM64 Native Tools Command Prompt for VS 2022'.
    exit /b
  ) else if "%VSCMD_ARG_TGT_ARCH%" neq "arm64" (
    echo Bad environment. Make sure to run from 'ARM64 Native Tools Command Prompt for VS 2022'.
    exit /b
  )
) else (
  if "%Platform%" neq "x86" (
    echo Bad environment. Make sure to run from 'x86 Native Tools Command Prompt for VS 2022'.
    exit /b
  ) else if "%VSCMD_ARG_HOST_ARCH%" neq "x86" (
    echo Bad environment. Make sure to run from 'x86 Native Tools Command Prompt for VS 2022'.
    exit /b
  ) else if "%VSCMD_ARG_TGT_ARCH%" neq "x86" (
    echo Bad environment. Make sure to run from 'x86 Native Tools Command Prompt for VS 2022'.
    exit /b
  )
)

FOR /F "tokens=1,2* delims= " %%i in (%FullScriptPath%version) do set "%%i=%%j"

if %AppVersion% lss 7002000 (
  echo The v2 update format requires version 7.2 or newer.
  exit /b 1
)

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
  ) else if %BuildARM% neq 0 (
    echo Building version %AppVersionStrFull% for UWP ARM..
  ) else (
    echo Building version %AppVersionStrFull% for UWP..
  )
) else (
  if %Build64% neq 0 (
    echo Building version %AppVersionStrFull% for Windows 64 bit..
  ) else if %BuildARM% neq 0 (
    echo Building version %AppVersionStrFull% for Windows on ARM..
  ) else (
    echo Building version %AppVersionStrFull% for Windows..
  )
)
echo.

set "SolutionPath=%HomePath%\..\out"
set "UpdateChannel=stable"
set "ArtifactSuffix="
if %BetaChannel% neq 0 (
  set "UpdateChannel=beta"
  set "ArtifactSuffix=-beta"
)
set "IsccNameParam="
if %AlphaVersion% neq 0 (
  echo The v2 update format has no alpha channel!
  exit /b 1
)
if %Build64% neq 0 (
  set "UpdateFile=td-update-win-x64-%AppVersion%%ArtifactSuffix%"
  set "SetupFile=td-setup-win-x64-%AppVersionStr%%ArtifactSuffix%.exe"
  set "PortableFile=td-portable-win-x64-%AppVersionStr%%ArtifactSuffix%.zip"
  set "DeployFolder=win-x64"
  set "IsccNameParam=/dMyOutputBaseFilename=td-setup-win-x64-%AppVersionStr%%ArtifactSuffix%"
  set "DumpSymsPath=%SolutionPath%\..\..\Libraries\win64\breakpad\src\tools\windows\dump_syms\Release\dump_syms.exe"
) else if %BuildARM% neq 0 (
  set "UpdateFile=td-update-win-arm-%AppVersion%%ArtifactSuffix%"
  set "SetupFile=td-setup-win-arm-%AppVersionStr%%ArtifactSuffix%.exe"
  set "PortableFile=td-portable-win-arm-%AppVersionStr%%ArtifactSuffix%.zip"
  set "DeployFolder=win-arm"
  set "IsccNameParam=/dMyOutputBaseFilename=td-setup-win-arm-%AppVersionStr%%ArtifactSuffix%"
  set "DumpSymsPath=%SolutionPath%\..\..\Libraries\breakpad\src\tools\windows\dump_syms\Release\dump_syms.exe"
) else (
  set "UpdateFile=td-update-win-x86-%AppVersion%%ArtifactSuffix%"
  set "SetupFile=td-setup-win-x86-%AppVersionStr%%ArtifactSuffix%.exe"
  set "PortableFile=td-portable-win-x86-%AppVersionStr%%ArtifactSuffix%.zip"
  set "DeployFolder=win-x86"
  set "IsccNameParam=/dMyOutputBaseFilename=td-setup-win-x86-%AppVersionStr%%ArtifactSuffix%"
  set "DumpSymsPath=%SolutionPath%\..\..\Libraries\breakpad\src\tools\windows\dump_syms\Release\dump_syms.exe"
)
set "ReleasePath=%SolutionPath%\Release"
set "DeployPath=%ReleasePath%\deploy\%AppVersionStrMajor%\%AppVersionStrFull%"
set "SignPath=%HomePath%\..\..\DesktopPrivate\Sign.bat"
set "BinaryName=Telegram"
set "DropboxSymbolsPath=Y:\Telegram\symbols"
set "DropboxSymbolsPathFallback=%HomePath%\..\..\Dropbox\Telegram\symbols"
set "FinalReleasePath=Z:\Projects\backup\tdesktop"
set "FinalReleasePathFallback=%HomePath%\..\..\Projects\backup\tdesktop"

if not exist %DropboxSymbolsPath% (
  if exist %DropboxSymbolsPathFallback% (
    set "DropboxSymbolsPath=%DropboxSymbolsPathFallback%"
  ) else (
    echo Dropbox path %DropboxSymbolsPath% not found!
    exit /b 1
  )
)

if not exist %FinalReleasePath% (
  if exist %FinalReleasePathFallback% (
    set "FinalReleasePath=%FinalReleasePathFallback%"
  ) else (
    echo Release path %FinalReleasePath% not found!
    exit /b 1
  )
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
  if exist %ReleasePath%\%UpdateFile% (
    echo Update file for version %AppVersion% already exists!
    exit /b 1
  )
  if exist %ReleasePath%\%PortableFile% (
    echo Portable file %PortableFile% already exists!
    exit /b 1
  )
)

set "LockDir=%SolutionPath%\.build.lock"
if exist "%LockDir%\" (
  echo Another build.bat seems to be running ^(found %LockDir%, remove it if stale^)!
  exit /b 1
)
mkdir "%LockDir%" || exit /b 1
call :build
set "ErrorCode=%errorlevel%"
rmdir "%LockDir%"
cd "%FullExecPath%"
exit /b %ErrorCode%

:build
cd "%HomePath%"

call configure.bat -DDESKTOP_APP_ENABLE_LTO=ON || goto error

cd "%SolutionPath%"
call cmake --build . --config Release --target Telegram || goto error

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

call :sign "%BinaryName%.exe"

if %BuildUWP% equ 0 (
  call :sign "Updater.exe"

  if %AlphaVersion% equ 0 (
    iscc /dMyAppVersion=%AppVersionStrSmall% /dMyAppVersionZero=%AppVersionStr% /dMyAppVersionFull=%AppVersionStrFull% "/dReleasePath=%ReleasePath%" "/dMyBuildTarget=%BuildTarget%" %IsccNameParam% "%FullScriptPath%setup.iss" || goto error
    if not exist "%SetupFile%" goto error
  )

  if %BuildARM% neq 0 (
    call Packer.exe -version %VersionForPacker% -path %BinaryName%.exe -path Updater.exe -target %BuildTarget% -channel %UpdateChannel% -keys-loc "%ResourcesPath%\update" -emit-signing-input signing-input.bin || goto error
  ) else (
    call Packer.exe -version %VersionForPacker% -path %BinaryName%.exe -path Updater.exe -path "modules\%Platform%\d3d\d3dcompiler_47.dll" -target %BuildTarget% -channel %UpdateChannel% -keys-loc "%ResourcesPath%\update" -emit-signing-input signing-input.bin || goto error
  )
  call :signupdate
  call Packer.exe -channel %UpdateChannel% -keys-loc "%ResourcesPath%\update" -unsigned "%UpdateFile%.unsigned" -embed-signatures %ReleaseCloudKeyId%:release-cloud.sig -local-key "%ReleaseLocalKey%" -local-key-id %ReleaseLocalKeyId% || goto error
  del signing-input.bin release-cloud.sig "%UpdateFile%.unsigned"

  if %AlphaVersion% neq 0 (
    if not exist "%ReleasePath%\%AlphaKeyFile%" (
      echo Alpha version key file not found!
      exit /b 1
    )

    FOR /F "tokens=1* delims= " %%i in (%ReleasePath%\%AlphaKeyFile%) do set "AlphaSignature=%%i"
  )

  if %AlphaVersion% neq 0 (
    set "UpdateFile=!UpdateFile!_!AlphaSignature!"
    set "PortableFile=talpha!AlphaVersion!_!AlphaSignature!.zip"
  )
) else (
  call :sign "StartupTask.exe"
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

  if %BuildARM% equ 0 (
    mkdir "%ReleasePath%\AppX\modules\%Platform%\d3d"
  )
  xcopy "Resources\uwp\AppX\*" "%ReleasePath%\AppX\" /E
  set "ResourcePath=%ReleasePath%\AppX\AppxManifest.xml"
  call :repl "Argument= (ProcessorArchitecture=)&quot;ARCHITECTURE&quot;/ $1&quot;%Platform%&quot;" "Filename=!ResourcePath!" || goto error
  makepri new /pr Resources\uwp\AppX\ /cf Resources\uwp\priconfig.xml /mn %ReleasePath%\AppX\AppxManifest.xml /of %ReleasePath%\AppX\resources.pri || goto error

  xcopy "%ReleasePath%\%BinaryName%.exe" "%ReleasePath%\AppX\"
  xcopy "%ReleasePath%\StartupTask.exe" "%ReleasePath%\AppX\"
  if %BuildARM% equ 0 (
    xcopy "%ReleasePath%\modules\%Platform%\d3d\d3dcompiler_47.dll" "%ReleasePath%\AppX\modules\%Platform%\d3d\"
  )

  MakeAppx.exe pack /d "%ReleasePath%\AppX" /l /p ..\out\Release\%BinaryName%.%Platform%.appx || goto error

  if not exist "%ReleasePath%\deploy" mkdir "%ReleasePath%\deploy"
  if not exist "%ReleasePath%\deploy\%AppVersionStrMajor%" mkdir "%ReleasePath%\deploy\%AppVersionStrMajor%"
  mkdir "%DeployPath%"

  move "%ReleasePath%\%BinaryName%.pdb" "%DeployPath%\"
  move "%ReleasePath%\%BinaryName%.%Platform%.appx" "%DeployPath%\"
  move "%ReleasePath%\%BinaryName%.exe" "%DeployPath%\"

  if "%AlphaBetaParam%" equ "" (
    move "%ReleasePath%\AppX" "%DeployPath%\AppX"
  ) else (
    echo Leaving result in out\Release\AppX_arch for now..
  )
) else (
  if not exist "%ReleasePath%\deploy" mkdir "%ReleasePath%\deploy"
  if not exist "%ReleasePath%\deploy\%AppVersionStrMajor%" mkdir "%ReleasePath%\deploy\%AppVersionStrMajor%"
  if %BuildARM% neq 0 (
    mkdir "%DeployPath%\%BinaryName%" || goto error
  ) else (
    mkdir "%DeployPath%\%BinaryName%\modules\%Platform%\d3d" || goto error
  )

  move "%ReleasePath%\%BinaryName%.exe" "%DeployPath%\%BinaryName%\" || goto error
  if %BuildARM% equ 0 (
    xcopy "%ReleasePath%\modules\%Platform%\d3d\d3dcompiler_47.dll" "%DeployPath%\%BinaryName%\modules\%Platform%\d3d\" || goto error
  )
  move "%ReleasePath%\Updater.exe" "%DeployPath%\" || goto error
  move "%ReleasePath%\%BinaryName%.pdb" "%DeployPath%\" || goto error
  move "%ReleasePath%\Updater.pdb" "%DeployPath%\" || goto error
  if %AlphaVersion% equ 0 (
    move "%ReleasePath%\%SetupFile%" "%DeployPath%\" || goto error
  ) else (
    move "%ReleasePath%\%AlphaKeyFile%" "%DeployPath%\" || goto error
  )
  move "%ReleasePath%\%UpdateFile%" "%DeployPath%\" || goto error

  cd "%DeployPath%"
  call :packportable

  move "%DeployPath%\%BinaryName%\%BinaryName%.exe" "%DeployPath%\" || goto error
  rmdir "%DeployPath%\%BinaryName%"
)

set "FinalDeployPath=%FinalReleasePath%\%AppVersionStrMajor%\%AppVersionStrFull%\!DeployFolder!"

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

:sign
call "%SignPath%" %1 && exit /b 0
echo Signing %1 failed, retrying in 3 seconds..
timeout /t 3
goto sign

:signupdate
python "%FullScriptPath%sign_update.py" --input signing-input.bin --output release-cloud.sig --az-vault "%ReleaseCloudVault%" --az-key "%ReleaseCloudKeyName%" && exit /b 0
echo Cloud signing of %UpdateFile% failed, retrying in 10 seconds (fix az login / network in another terminal)..
timeout /t 10
goto signupdate

:packportable
7z a -mx9 %PortableFile% %BinaryName%\ || goto packportableretry
7z t %PortableFile% || goto packportableretry
exit /b 0

:packportableretry
echo Packing %PortableFile% failed, retrying in 3 seconds..
if exist "%PortableFile%" del "%PortableFile%"
timeout /t 3
goto packportable
