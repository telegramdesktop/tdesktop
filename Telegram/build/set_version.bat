@echo OFF
setlocal enabledelayedexpansion
set "FullScriptPath=%~dp0"

set "InputVersion=%1"

for /F "tokens=1,2,3,4 delims=. " %%a in ("%InputVersion%") do (
  set "VersionMajor=%%a"
  set "VersionMinor=%%b"
  set "VersionPatch=%%c"
  if "%%d" == "" (
    set "VersionBeta=0"
    set "VersionAlpha=0"
  ) else if "%%d" == "alpha" (
    set "VersionBeta=0"
    set "VersionAlpha=1"
  ) else (
    set "VersionBeta=%%d"
    set "VersionAlpha=0"
  )
)

set /a "VersionMajorCleared=%VersionMajor% %% 1000"
if "%VersionMajorCleared%" neq "%VersionMajor%" (
  echo Bad major version!
  exit /b 1
)
set /a "VersionMinorCleared=%VersionMinor% %% 1000"
if "%VersionMinorCleared%" neq "%VersionMinor%" (
  echo Bad minor version!
  exit /b 1
)
set /a "VersionPatchCleared=%VersionPatch% %% 1000"
if "%VersionPatchCleared%" neq "%VersionPatch%" (
  echo Bad patch version!
  exit /b 1
)
if "%VersionAlpha%" neq "0" (
  if "%VersionAlpha%" neq "1" (
    echo Bad alpha version!
    exit /b 1
  )
  set "VersionAlphaBool=true"
) else (
  set "VersionAlphaBool=false"
)

set /a "VersionFull=%VersionMajor% * 1000000 + %VersionMinor% * 1000 + %VersionPatch%"
if "%VersionBeta%" neq "0" (
  set /a "VersionBetaCleared=%VersionBeta% %% 1000"
  if "!VersionBetaCleared!" neq "%VersionBeta%" (
    echo Bad beta version!
    exit /b 1
  )
  set /a "VersionBetaMul=1000 + %VersionBeta%"
  set "VersionFullBeta=%VersionFull%!VersionBetaMul:~1!"
) else (
  set "VersionFullBeta=0"
)

set "VersionStr=%VersionMajor%.%VersionMinor%.%VersionPatch%"
if "%VersionPatch%" neq "0" (
  set "VersionStrSmall=%VersionStr%"
) else (
  set "VersionStrSmall=%VersionMajor%.%VersionMinor%"
)

if "%VersionAlpha%" neq "0" (
  echo Setting version: %VersionStr% alpha
) else if "%VersionBeta%" neq "0" (
  echo Setting version: %VersionStr%.%VersionBeta% closed beta
) else (
  echo Setting version: %VersionStr% stable
)

echo Checking changelog...
set "ChangelogFile=%FullScriptPath%..\..\changelog.txt"
call :count "Argument=^%VersionStr% " "Filename=%ChangelogFile%"
if "%FoundCount%" equ "0" (
  call :count "Argument=^%VersionStrSmall% " "Filename=%ChangelogFile%"
  if "!FoundCount!" equ "0" (
    echo Changelog entry not found!
    exit /b 1
  ) else if "!FoundCount!" neq "1" (
    echo Wrong changelog entries count found: %FoundCount%
    exit /b 1
  )
) else if "%FoundCount%" neq "1" (
  echo Wrong changelog entries count found: %FoundCount%
  exit /b 1
)

echo Patching build/version...
set "VersionFilePath=%FullScriptPath%version"
call :repl "Argument=(AppVersion) (\s*)\d+/$1$2 %VersionFull%" "Filename=%VersionFilePath%" || goto :error
call :repl "Argument=(AppVersionStrMajor) (\s*)[\d\.]+/$1$2 %VersionMajor%.%VersionMinor%" "Filename=%VersionFilePath%" || goto :error
call :repl "Argument=(AppVersionStrSmall) (\s*)[\d\.]+/$1$2 %VersionStrSmall%" "Filename=%VersionFilePath%" || goto :error
call :repl "Argument=(AppVersionStr) (\s*)[\d\.]+/$1$2 %VersionStr%" "Filename=%VersionFilePath%" || goto :error
call :repl "Argument=(AlphaChannel) (\s*)[\d\.]+/$1$2 %VersionAlpha%" "Filename=%VersionFilePath%" || goto :error
call :repl "Argument=(BetaVersion) (\s*)\d+/$1$2 %VersionFullBeta%" "Filename=%VersionFilePath%" || goto :error

echo Patching core/version.h...
set "VersionHeaderPath=%FullScriptPath%..\SourceFiles\core\version.h"
call :repl "Argument=(BETA_VERSION_MACRO\s+)\(\d+ULL\)/$1(%VersionFullBeta%ULL)" "Filename=%VersionHeaderPath%" || goto :error
call :repl "Argument=(AppVersion\s+=) (\s*)\d+/$1$2 %VersionFull%" "Filename=%VersionHeaderPath%" || goto :error
call :repl "Argument=(AppVersionStr\s+=) (\s*)[&hat;;]+/$1$2 &quot;%VersionStrSmall%&quot;" "Filename=%VersionHeaderPath%" || goto :error
call :repl "Argument=(AppAlphaVersion\s+=) (\s*)[a-z]+/$1$2 %VersionAlphaBool%" "Filename=%VersionHeaderPath%" || goto :error

echo Patching Telegram.rc...
set "ResourcePath=%FullScriptPath%..\Resources\winrc\Telegram.rc"
call :repl "Argument=(FILEVERSION) (\s*)\d+,\d+,\d+,\d+/$1$2 %VersionMajor%,%VersionMinor%,%VersionPatch%,%VersionBeta%" "Filename=%ResourcePath%" || goto :error
call :repl "Argument=(PRODUCTVERSION) (\s*)\d+,\d+,\d+,\d+/$1$2 %VersionMajor%,%VersionMinor%,%VersionPatch%,%VersionBeta%" "Filename=%ResourcePath%" || goto :error
call :repl "Argument=(&quot;FileVersion&quot;,) (\s*)&quot;\d+.\d+.\d+.\d+&quot;/$1$2 &quot;%VersionMajor%.%VersionMinor%.%VersionPatch%.%VersionBeta%&quot;" "Filename=%ResourcePath%" || goto :error
call :repl "Argument=(&quot;ProductVersion&quot;,) (\s*)&quot;\d+.\d+.\d+.\d+&quot;/$1$2 &quot;%VersionMajor%.%VersionMinor%.%VersionPatch%.%VersionBeta%&quot;" "Filename=%ResourcePath%" || goto :error

echo Patching Updater.rc...
set "ResourcePath=%FullScriptPath%..\Resources\winrc\Updater.rc"
call :repl "Argument=(FILEVERSION) (\s*)\d+,\d+,\d+,\d+/$1$2 %VersionMajor%,%VersionMinor%,%VersionPatch%,%VersionBeta%" "Filename=%ResourcePath%" || goto :error
call :repl "Argument=(PRODUCTVERSION) (\s*)\d+,\d+,\d+,\d+/$1$2 %VersionMajor%,%VersionMinor%,%VersionPatch%,%VersionBeta%" "Filename=%ResourcePath%" || goto :error
call :repl "Argument=(&quot;FileVersion&quot;,) (\s*)&quot;\d+.\d+.\d+.\d+&quot;/$1$2 &quot;%VersionMajor%.%VersionMinor%.%VersionPatch%.%VersionBeta%&quot;" "Filename=%ResourcePath%" || goto :error
call :repl "Argument=(&quot;ProductVersion&quot;,) (\s*)&quot;\d+.\d+.\d+.\d+&quot;/$1$2 &quot;%VersionMajor%.%VersionMinor%.%VersionPatch%.%VersionBeta%&quot;" "Filename=%ResourcePath%" || goto :error

echo Patching appxmanifest.xml...
set "ResourcePath=%FullScriptPath%..\Resources\uwp\AppX\AppxManifest.xml"
call :repl "Argument= (Version=)&quot;\d+.\d+.\d+.\d+&quot;/ $1&quot;%VersionMajor%.%VersionMinor%.%VersionPatch%.%VersionBeta%&quot;" "Filename=%ResourcePath%" || goto :error

exit /b

:error
(
  set ErrorCode=%errorlevel%
  echo Error !ErrorCode!
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

:count
(
  set %1
  set %2
  set "TempFilename=!Filename!__tmp__"
  cscript //Nologo "%FullScriptPath%replace.vbs" "Count" "!Argument!" < "!Filename!" > "!TempFilename!" || goto :repl_finish
  FOR /F "tokens=1,2* delims= " %%i in (!TempFilename!) do set "FoundCount=%%i"
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
