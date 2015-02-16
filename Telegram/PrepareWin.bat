@echo OFF

set "AppVersionStrSmall=0.7.15"
set "AppVersionStr=0.7.15"
set "AppVersionStrFull=0.7.15.0"
set "DevChannel=1"

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

call Prepare.exe -path Telegram.exe -path Updater.exe %DevParam%
if %errorlevel% neq 0 goto error1

cd deploy\%AppVersionStr%%DevPostfix%
mkdir Telegram
move Telegram.exe Telegram\
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

:eof
