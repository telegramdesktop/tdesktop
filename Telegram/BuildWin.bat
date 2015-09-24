@echo OFF

FOR /F "tokens=1,2* delims= " %i in (Version) do set "%i=%j"

if %DevChannel% neq 0 goto preparedev

set "DevParam="
set "AppVersionStrFull=%AppVersionStr%"
goto devprepared

:preparedev

set "DevParam=-dev"
set "AppVersionStrFull=%AppVersionStr%.dev"

:devprepared

echo.
echo Building version %AppVersionStrFull%..
echo.

if exist ..\Win32\Deploy\deploy\%AppVersionStrMajor%\%AppVersionStr%\ goto error_exist1
if exist ..\Win32\Deploy\deploy\%AppVersionStrMajor%\%AppVersionStr%.dev\ goto error_exist2
if exist ..\Win32\Deploy\tupdate%AppVersion% goto error_exist3

cd SourceFiles\
copy telegram.qrc /B+,,/Y
cd ..\
if %errorlevel% neq 0 goto error

cd ..\
MSBuild Telegram.sln /property:Configuration=Deploy
if %errorlevel% neq 0 goto error0

echo .
echo Version %AppVersionStrFull% build successfull! Preparing..
echo .

set "PATH=%PATH%;C:\Program Files\7-Zip;C:\Program Files (x86)\Inno Setup 5"
cd Win32\Deploy\

call ..\..\..\TelegramPrivate\Sign.bat Telegram.exe
if %errorlevel% neq 0 goto error1

call ..\..\..\TelegramPrivate\Sign.bat Updater.exe
if %errorlevel% neq 0 goto error1

iscc /dMyAppVersion=%AppVersionStrSmall% /dMyAppVersionZero=%AppVersionStr% /dMyAppVersionFull=%AppVersionStrFull% ..\..\Telegram\Setup.iss
if %errorlevel% neq 0 goto error1

call ..\..\..\TelegramPrivate\Sign.bat tsetup.%AppVersionStrFull%.exe
if %errorlevel% neq 0 goto error1

call Packer.exe -version %AppVersion% -path Telegram.exe -path Updater.exe %DevParam%
if %errorlevel% neq 0 goto error1

if not exist deploy mkdir deploy
if not exist deploy\%AppVersionStrMajor% mkdir deploy\%AppVersionStrMajor%
mkdir deploy\%AppVersionStrMajor%\%AppVersionStrFull%
mkdir deploy\%AppVersionStrMajor%\%AppVersionStrFull%\Telegram
if %errorlevel% neq 0 goto error1

move Telegram.exe deploy\%AppVersionStrMajor%\%AppVersionStrFull%\Telegram\
move Updater.exe deploy\%AppVersionStrMajor%\%AppVersionStrFull%\
move Telegram.pdb deploy\%AppVersionStrMajor%\%AppVersionStrFull%\
move Updater.pdb deploy\%AppVersionStrMajor%\%AppVersionStrFull%\
move tsetup.%AppVersionStrFull%.exe deploy\%AppVersionStrMajor%\%AppVersionStrFull%\
move tupdate%AppVersion% deploy\%AppVersionStrMajor%\%AppVersionStrFull%\
if %errorlevel% neq 0 goto error1

cd deploy\%AppVersionStrMajor%\%AppVersionStrFull%\
7z a -mx9 tportable.%AppVersionStrFull%.zip Telegram\
if %errorlevel% neq 0 goto error2

echo .
echo Version %AppVersionStrFull% is ready for deploy!
echo .

if not exist tupdate%AppVersion% goto error2
if not exist tportable.%AppVersionStrFull%.zip goto error2
if not exist tsetup.%AppVersionStrFull%.exe goto error2
if not exist Telegram.pdb goto error2
if not exist Updater.exe goto error2
if not exist Updater.pdb goto error2
if not exist Z:\TBuild\tother\tsetup\%AppVersionStrMajor% mkdir Z:\TBuild\tother\tsetup\%AppVersionStrMajor%
if not exist Z:\TBuild\tother\tsetup\%AppVersionStrMajor%\%AppVersionStrFull% mkdir Z:\TBuild\tother\tsetup\%AppVersionStrMajor%\%AppVersionStrFull%

xcopy tupdate%AppVersion% Z:\TBuild\tother\tsetup\%AppVersionStrMajor%\%AppVersionStrFull%\
xcopy tportable.%AppVersionStrFull%.zip Z:\TBuild\tother\tsetup\%AppVersionStrMajor%\%AppVersionStrFull%\
xcopy tsetup.%AppVersionStrFull%.exe Z:\TBuild\tother\tsetup\%AppVersionStrMajor%\%AppVersionStrFull%\
xcopy Telegram.pdb Z:\TBuild\tother\tsetup\%AppVersionStrMajor%\%AppVersionStrFull%\
xcopy Updater.exe Z:\TBuild\tother\tsetup\%AppVersionStrMajor%\%AppVersionStrFull%\
xcopy Updater.pdb Z:\TBuild\tother\tsetup\%AppVersionStrMajor%\%AppVersionStrFull%\

echo Version %AppVersionStrFull% is ready!

cd ..\..\..\..\..\Telegram\
goto eof

:error2
cd ..\..\..\
:error1
cd ..\..\
:error0
cd Telegram\
goto error

:error_exist1
echo Deploy folder for version %AppVersionStr% already exists!
exit /b 1

:error_exist2
echo Deploy folder for version %AppVersionStr%.dev already exists!
exit /b 1

:error_exist3
echo Update file for version %AppVersion% already exists!
exit /b 1

:error
echo ERROR occured!
exit /b %errorlevel%

:eof
