cd ..\Win32\Deploy
call ..\..\..\TelegramPrivate\Sign.bat tsetup.0.6.7.exe
call Prepare.exe -path Telegram.exe -path Updater.exe
mkdir deploy\0.6.7\Telegram
move deploy\0.6.7\Telegram.exe deploy\0.6.7\Telegram\
cd ..\..\Telegram
