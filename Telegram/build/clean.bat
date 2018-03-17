@echo OF
setlocal enabledelayedexpansion
set "FullScriptPath=%~dp0"
set "FullExecPath=%cd%"

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

set "HomePath=%FullScriptPath%.."
set "ReleasePath=%HomePath%\..\out\Release"
set "DeployPath=%ReleasePath%\deploy\%AppVersionStrMajor%\%AppVersionStrFull%"

rm -r %DeployPath%