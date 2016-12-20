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
echo Building version %AppVersionStrFull% for Windows..
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
set "BinaryName=Telegram"
set "DropboxSymbolsPath=X:\Telegram\symbols"
set "FinalReleasePath=Y:\TBuild\tother\tsetup"

if not exist %DropboxSymbolsPath% (
  echo Dropbox path %DropboxSymbolsPath% not found!
  exit /b 1
)

if not exist %FinalReleasePath% (
  echo Release path %FinalReleasePath% not found!
  exit /b 1
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

echo .
echo Version %AppVersionStrFull% build successfull. Preparing..
echo .

echo Dumping debug symbols..
xcopy "%ReleasePath%\%BinaryName%.exe" "%ReleasePath%\%BinaryName%.exe.exe*"
call "%SolutionPath%\..\Libraries\breakpad\src\tools\windows\dump_syms\Release\dump_syms.exe" "%ReleasePath%\%BinaryName%.exe.pdb" > "%ReleasePath%\%BinaryName%.exe.sym"
del "%ReleasePath%\%BinaryName%.exe.exe"
echo Done!

set "PATH=%PATH%;C:\Program Files\7-Zip;C:\Program Files (x86)\Inno Setup 5"

cd "%ReleasePath%"
call "%SignPath%" "%BinaryName%.exe"
if %errorlevel% neq 0 goto error

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
  set "UpdateFile=%UpdateFile%_%BetaSignature%"
  set "PortableFile=tbeta%BetaVersion%_%BetaSignature%.zip"
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

echo .
echo Version %AppVersionStrFull% is ready for deploy!
echo .

set "FinalDeployPath=%FinalReleasePath%\%AppVersionStrMajor%\%AppVersionStrFull%"

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
if not exist "%FinalReleasePath%\%AppVersionStrMajor%" mkdir "%FinalReleasePath%\%AppVersionStrMajor%"
if not exist "%FinalDeployPath%" mkdir "%FinalDeployPath%"

xcopy "%DeployPath%\%UpdateFile%" "%FinalDeployPath%\"
xcopy "%DeployPath%\%PortableFile%" "%FinalDeployPath%\"
if %BetaVersion% equ 0 (
  xcopy "%DeployPath%\%SetupFile%" "%FinalDeployPath%\"
) else (
  xcopy "%DeployPath%\%BetaKeyFile%" "%FinalDeployPath%\" /Y
)
xcopy "%DeployPath%\%BinaryName%.pdb" "%FinalDeployPath%\"
xcopy "%DeployPath%\%BinaryName%.exe.pdb" "%FinalDeployPath%\"
xcopy "%DeployPath%\Updater.exe" "%FinalDeployPath%\"
xcopy "%DeployPath%\Updater.pdb" "%FinalDeployPath%\"
xcopy "%DeployPath%\Updater.exe.pdb" "%FinalDeployPath%\"

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
