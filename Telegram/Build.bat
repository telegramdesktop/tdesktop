@echo OFF

FOR /F "tokens=1,2* delims= " %%i in (Version) do set "%%i=%%j"

set "VersionForPacker=%AppVersion%"
if %BetaVersion% neq 0 (
  set "AppVersion=%BetaVersion%"
  set "AppVersionStrFull=%AppVersionStr%_%BetaVersion%"
  set "DevParam=-beta %BetaVersion%"
  set "BetaKeyFile=tbeta_%BetaVersion%_key"
) else (
  if %DevChannel% neq 0 (
    set "DevParam=-dev"
    set "AppVersionStrFull=%AppVersionStr%.dev"
  ) else (
    set "DevParam="
    set "AppVersionStrFull=%AppVersionStr%"
  )
)

echo.
echo Building version %AppVersionStrFull% for Windows..
echo.

set "UpdateFile=tupdate%AppVersion%"
set "SetupFile=tsetup.%AppVersionStrFull%.exe"
set "PortableFile=tportable.%AppVersionStrFull%.zip"
set "HomePath=..\..\Telegram"
set "ReleasePath=..\Win32\Deploy"
set "DeployPath=%ReleasePath%\deploy\%AppVersionStrMajor%\%AppVersionStrFull%"
set "SignPath=..\..\TelegramPrivate\Sign.bat"

if %BetaVersion% neq 0 (
  if exist %ReleasePath%\%BetaKeyFile% (
    echo Beta version key file for version %AppVersion% already exists!
    exit /b 1
  )
) else (
  if exist %ReleasePath%\deploy\%AppVersionStrMajor%\%AppVersionStr%.dev\ (
    echo Deploy folder for version %AppVersionStr%.dev already exists!
    exit /b 1
  )
  if exist %ReleasePath%\tupdate%AppVersion% (
    echo Update file for version %AppVersion% already exists!
    exit /b 1
  )
)

if exist %ReleasePath%\deploy\%AppVersionStrMajor%\%AppVersionStr%\ (
  echo Deploy folder for version %AppVersionStr% already exists!
  exit /b 1
)

cd SourceFiles\
rem copy telegram.qrc /B+,,/Y
cd ..\
if %errorlevel% neq 0 goto error

cd ..\
MSBuild Telegram.sln /property:Configuration=Deploy
cd Telegram\
if %errorlevel% neq 0 goto error

echo .
echo Version %AppVersionStrFull% build successfull. Preparing..
echo .

set "PATH=%PATH%;C:\Program Files\7-Zip;C:\Program Files (x86)\Inno Setup 5"

call %SignPath% %ReleasePath%\Telegram.exe
if %errorlevel% neq 0 goto error

call %SignPath% %ReleasePath%\Updater.exe
if %errorlevel% neq 0 goto error

if %BetaVersion% equ 0 (
  cd %ReleasePath%
  iscc /dMyAppVersion=%AppVersionStrSmall% /dMyAppVersionZero=%AppVersionStr% /dMyAppVersionFull=%AppVersionStrFull% %HomePath%\Setup.iss
  cd %HomePath%
  if %errorlevel% neq 0 goto error

  call %SignPath% %ReleasePath%\tsetup.%AppVersionStrFull%.exe
  if %errorlevel% neq 0 goto error
)

cd %ReleasePath%
call Packer.exe -version %VersionForPacker% -path Telegram.exe -path Updater.exe %DevParam%
cd %HomePath%
if %errorlevel% neq 0 goto error

if %BetaVersion% neq 0 (
  if not exist %ReleasePath%\%BetaKeyFile% (
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

if not exist %ReleasePath%\deploy mkdir %ReleasePath%\deploy
if not exist %ReleasePath%\deploy\%AppVersionStrMajor% mkdir %ReleasePath%\deploy\%AppVersionStrMajor%
mkdir %DeployPath%
mkdir %DeployPath%\Telegram
if %errorlevel% neq 0 goto error

move %ReleasePath%\Telegram.exe %DeployPath%\Telegram\
move %ReleasePath%\Updater.exe %DeployPath%\
move %ReleasePath%\Telegram.pdb %DeployPath%\
move %ReleasePath%\Updater.pdb %DeployPath%\
if %BetaVersion% equ 0 (
  move %ReleasePath%\%SetupFile% %DeployPath%\
) else (
  move %ReleasePath%\%BetaKeyFile% %DeployPath%\
)
move %ReleasePath%\%UpdateFile% %DeployPath%\
if %errorlevel% neq 0 goto error

cd %DeployPath%\
7z a -mx9 %PortableFile% Telegram\
cd ..\..\..\%HomePath%\
if %errorlevel% neq 0 goto error

echo .
echo Version %AppVersionStrFull% is ready for deploy!
echo .

set "FinalReleasePath=Z:\TBuild\tother\tsetup"
set "FinalDeployPath=%FinalReleasePath%\%AppVersionStrMajor%\%AppVersionStrFull%"

if not exist %DeployPath%\%UpdateFile% goto error
if not exist %DeployPath%\%PortableFile% goto error
if %BetaVersion% equ 0 (
  if not exist %DeployPath%\%SetupFile% goto error
)
if not exist %DeployPath%\Telegram.pdb goto error
if not exist %DeployPath%\Updater.exe goto error
if not exist %DeployPath%\Updater.pdb goto error
if not exist %FinalReleasePath%\%AppVersionStrMajor% mkdir %FinalReleasePath%\%AppVersionStrMajor%
if not exist %FinalDeployPath% mkdir %FinalDeployPath%

xcopy %DeployPath%\%UpdateFile% %FinalDeployPath%\
xcopy %DeployPath%\%PortableFile% %FinalDeployPath%\
if %BetaVersion% equ 0 (
  xcopy %DeployPath%\%SetupFile% %FinalDeployPath%\
) else (
  xcopy %DeployPath%\%BetaKeyFile% %FinalDeployPath%\
)
xcopy %DeployPath%\Telegram.pdb %FinalDeployPath%\
xcopy %DeployPath%\Updater.exe %FinalDeployPath%\
xcopy %DeployPath%\Updater.pdb %FinalDeployPath%\

echo Version %AppVersionStrFull% is ready!

goto eof

:error
echo ERROR occured!
if %errorlevel% neq 0 exit /b %errorlevel%
exit /b 1

:eof
