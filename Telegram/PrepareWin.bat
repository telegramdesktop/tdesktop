@echo OFF

set "AppVersionStrSmall=0.6.19"
set "AppVersionStr=0.6.19"
set "AppVersionStrFull=0.6.19.0"

echo.
echo Preparing version %AppVersionStr%..
echo.

set "PATH=%PATH%;C:\Program Files\7-Zip;C:\Program Files (x86)\Inno Setup 5"
cd ..\Win32\Deploy

call ..\..\..\TelegramPrivate\Sign.bat Telegram.exe
if %errorlevel% neq 0 goto error1

call ..\..\..\TelegramPrivate\Sign.bat Updater.exe
if %errorlevel% neq 0 goto error1

iscc /dMyAppVersion=%AppVersionStrSmall% /dMyAppVersionZero=%AppVersionStr% /dMyAppFullVersion=%AppVersionStrFull% ..\..\Telegram\Setup.iss
if %errorlevel% neq 0 goto error1

call ..\..\..\TelegramPrivate\Sign.bat tsetup.%AppVersionStr%.exe
if %errorlevel% neq 0 goto error1

call Prepare.exe -path Telegram.exe -path Updater.exe
if %errorlevel% neq 0 goto error1

cd deploy\%AppVersionStr%
mkdir Telegram
move Telegram.exe Telegram\
7z a -mx9 tportable.%AppVersionStr%.zip Telegram\
if %errorlevel% neq 0 goto error2

echo .
echo Version %AppVersionStr% is ready for deploy!
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
