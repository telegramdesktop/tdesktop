@echo OFF

FOR /F "tokens=1,2* delims= " %i in (Version) do set "%i=%j"

if %DevChannel% neq 0 goto preparedev

set "DevPostfix="
set "DevParam="
goto devprepared

:preparedev

set "DevPostfix=.dev"
set "DevParam=-dev"

:devprepared

echo.
echo Building version %AppVersionStr%%DevPostfix%..
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
echo Version %AppVersionStr%%DevPostfix% build successfull! Preparing..
echo .

set "PATH=%PATH%;C:\Program Files\7-Zip;C:\Program Files (x86)\Inno Setup 5"
cd Win32\Deploy\

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
if not exist deploy\%AppVersionStrMajor% mkdir deploy\%AppVersionStrMajor%
mkdir deploy\%AppVersionStrMajor%\%AppVersionStr%%DevPostfix%
mkdir deploy\%AppVersionStrMajor%\%AppVersionStr%%DevPostfix%\Telegram
if %errorlevel% neq 0 goto error1

move Telegram.exe deploy\%AppVersionStrMajor%\%AppVersionStr%%DevPostfix%\Telegram\
move Updater.exe deploy\%AppVersionStrMajor%\%AppVersionStr%%DevPostfix%\
move Telegram.pdb deploy\%AppVersionStrMajor%\%AppVersionStr%%DevPostfix%\
move Updater.pdb deploy\%AppVersionStrMajor%\%AppVersionStr%%DevPostfix%\
move tsetup.%AppVersionStr%%DevPostfix%.exe deploy\%AppVersionStrMajor%\%AppVersionStr%%DevPostfix%\
move tupdate%AppVersion% deploy\%AppVersionStrMajor%\%AppVersionStr%%DevPostfix%\
if %errorlevel% neq 0 goto error1

cd deploy\%AppVersionStrMajor%\%AppVersionStr%%DevPostfix%\
7z a -mx9 tportable.%AppVersionStr%%DevPostfix%.zip Telegram\
if %errorlevel% neq 0 goto error2

echo .
echo Version %AppVersionStr%%DevPostfix% is ready for deploy!
echo .

if not exist tupdate%AppVersion% goto error2
if not exist tportable.%AppVersionStr%%DevPostfix%.zip goto error2
if not exist tsetup.%AppVersionStr%%DevPostfix%.exe goto error2
if not exist Telegram.pdb goto error2
if not exist Updater.exe goto error2
if not exist Updater.pdb goto error2
if not exist Z:\TBuild\tother\tsetup\%AppVersionStrMajor% mkdir Z:\TBuild\tother\tsetup\%AppVersionStrMajor%
if not exist Z:\TBuild\tother\tsetup\%AppVersionStrMajor%\%AppVersionStr%%DevPostfix% mkdir Z:\TBuild\tother\tsetup\%AppVersionStrMajor%\%AppVersionStr%%DevPostfix%

xcopy tupdate%AppVersion% Z:\TBuild\tother\tsetup\%AppVersionStrMajor%\%AppVersionStr%%DevPostfix%\
xcopy tportable.%AppVersionStr%%DevPostfix%.zip Z:\TBuild\tother\tsetup\%AppVersionStrMajor%\%AppVersionStr%%DevPostfix%\
xcopy tsetup.%AppVersionStr%%DevPostfix%.exe Z:\TBuild\tother\tsetup\%AppVersionStrMajor%\%AppVersionStr%%DevPostfix%\
xcopy Telegram.pdb Z:\TBuild\tother\tsetup\%AppVersionStrMajor%\%AppVersionStr%%DevPostfix%\
xcopy Updater.exe Z:\TBuild\tother\tsetup\%AppVersionStrMajor%\%AppVersionStr%%DevPostfix%\
xcopy Updater.pdb Z:\TBuild\tother\tsetup\%AppVersionStrMajor%\%AppVersionStr%%DevPostfix%\

echo Version %AppVersionStr%%DevPostfix% deployed successfully!

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
