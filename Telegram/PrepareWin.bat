@echo OFF

set "AppVersion=8011"
set "AppVersionStrSmall=0.8.11"
set "AppVersionStr=0.8.11"
set "AppVersionStrFull=0.8.11.0"
set "DevChannel=0"

if %DevChannel% neq 0 goto preparedev

set "DevPostfix="
set "DevParam="
goto devprepared

:preparedev

set "DevPostfix=.dev"
set "DevParam=-dev"

:devprepared

echo.
echo Preparing version %AppVersionStr%%DevPostfix%..
echo.

if exist ..\Win32\Deploy\deploy\%AppVersionStr%\ goto error_exist1
if exist ..\Win32\Deploy\deploy\%AppVersionStr%.dev\ goto error_exist2
if exist ..\Win32\Deploy\tupdate%AppVersion% goto error_exist3

set "PATH=%PATH%;C:\Program Files\7-Zip;C:\Program Files (x86)\Inno Setup 5"
cd ..\Win32\Deploy

call ..\..\..\TelegramPrivate\Sign.bat Telegram.exe
if %errorlevel% neq 0 goto error1

call ..\..\..\TelegramPrivate\Sign.bat Updater.exe
if %errorlevel% neq 0 goto error1

iscc /dMyAppVersion=%AppVersionStrSmall% /dMyAppVersionZero=%AppVersionStr% /dMyAppFullVersion=%AppVersionStrFull% /dMyAppVersionForExe=%AppVersionStr%%DevPostfix% ..\..\Telegram\Setup.iss
if %errorlevel% neq 0 goto error1

call ..\..\..\TelegramPrivate\Sign.bat tsetup.%AppVersionStr%%DevPostfix%.exe
if %errorlevel% neq 0 goto error1

call Packer.exe -version %AppVersion% -path Telegram.exe -path Updater.exe %DevParam%
if %errorlevel% neq 0 goto error1

if not exist deploy mkdir deploy
mkdir deploy\%AppVersionStr%%DevPostfix%
mkdir deploy\%AppVersionStr%%DevPostfix%\Telegram

move Telegram.exe deploy\%AppVersionStr%%DevPostfix%\Telegram\
move Updater.exe deploy\%AppVersionStr%%DevPostfix%\
move Telegram.pdb deploy\%AppVersionStr%%DevPostfix%\
move Updater.pdb deploy\%AppVersionStr%%DevPostfix%\
move tsetup.%AppVersionStr%%DevPostfix%.exe deploy\%AppVersionStr%%DevPostfix%\
move tupdate%AppVersion% deploy\%AppVersionStr%%DevPostfix%\

cd deploy\%AppVersionStr%%DevPostfix%
7z a -mx9 tportable.%AppVersionStr%%DevPostfix%.zip Telegram\
if %errorlevel% neq 0 goto error2

echo .
echo Version %AppVersionStr%%DevPostfix% is ready for deploy!
echo .

cd ..\..\..\..\Telegram
goto eof

:error2
cd ..\..
:error1
cd ..\..\Telegram
echo ERROR occured!
exit /b %errorlevel%

:error_exist1
echo Deploy folder for version %AppVersionStr% already exists!
exit /b 1

:error_exist2
echo Deploy folder for version %AppVersionStr%.dev already exists!
exit /b 1

:error_exist3
echo Update file for version %AppVersion% already exists!
exit /b 1

:eof
