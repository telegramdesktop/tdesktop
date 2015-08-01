AppVersionStrMajor=`./Version.sh | awk -F " " '{print $1}'`
AppVersion=`./Version.sh | awk -F " " '{print $2}'`
AppVersionStr=`./Version.sh | awk -F " " '{print $3}'`
DevChannel=`./Version.sh | awk -F " " '{print $4}'`
DevPostfix=''
if [ "$DevChannel" != "0" ]; then
  DevPostfix='.dev'
fi

if [ ! -f "./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tmacupd$AppVersion" ]; then
  echo "tmacupd$AppVersion not found!"
  exit 1
fi

if [ ! -f "./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tsetup.$AppVersionStr$DevPostfix.dmg" ]; then
  echo "tsetup.$AppVersionStr$DevPostfix.dmg not found!"
  exit 1
fi

if [ ! -f "./../../tother/tmac32/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tmac32upd$AppVersion" ]; then
  echo "tmac32upd$AppVersion not found!"
  exit 1
fi

if [ ! -f "./../../tother/tmac32/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tsetup32.$AppVersionStr$DevPostfix.dmg" ]; then
  echo "tsetup32.$AppVersionStr$DevPostfix.dmg not found!"
  exit 1
fi

if [ ! -f "./../../tother/tsetup/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tupdate$AppVersion" ]; then
  echo "tupdate$AppVersion not found!"
  exit 1
fi

if [ ! -f "./../../tother/tsetup/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tportable.$AppVersionStr$DevPostfix.zip" ]; then
  echo "tportable.$AppVersionStr$DevPostfix.zip not found!"
  exit 1
fi

if [ ! -f "./../../tother/tsetup/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tsetup.$AppVersionStr$DevPostfix.exe" ]; then
  echo "tsetup.$AppVersionStr$DevPostfix.exe not found!"
  exit 1
fi

if [ ! -d "./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor" ]; then
  mkdir "./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor"
fi

scp ./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tmacupd$AppVersion tmaster:tdesktop/www/tmac/
scp ./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tsetup.$AppVersionStr$DevPostfix.dmg tmaster:tdesktop/www/tmac/
scp ./../../tother/tmac32/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tmac32upd$AppVersion tmaster:tdesktop/www/tmac32/
scp ./../../tother/tmac32/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tsetup32.$AppVersionStr$DevPostfix.dmg tmaster:tdesktop/www/tmac32/
scp ./../../tother/tsetup/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tupdate$AppVersion tmaster:tdesktop/www/tsetup/
scp ./../../tother/tsetup/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tportable.$AppVersionStr$DevPostfix.zip tmaster:tdesktop/www/tsetup/
scp ./../../tother/tsetup/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tsetup.$AppVersionStr$DevPostfix.exe tmaster:tdesktop/www/tsetup/

mv -v ./../../tother/tsetup/$AppVersionStrMajor/$AppVersionStr$DevPostfix ./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor/

cp -v ./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tmacupd$AppVersion ./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/
cp -v ./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tsetup.$AppVersionStr$DevPostfix.dmg ./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/
cp -rv ./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/Telegram.app.dSYM ./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/
cp -v ./../../tother/tmac32/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tmac32upd$AppVersion ./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/
cp -v ./../../tother/tmac32/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tsetup32.$AppVersionStr$DevPostfix.dmg ./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/
cp -rv ./../../tother/tmac32/$AppVersionStrMajor/$AppVersionStr$DevPostfix/Telegram.app.dSYM ./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/Telegram32.app.dSYM

