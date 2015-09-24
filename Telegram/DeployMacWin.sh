while IFS='' read -r line || [[ -n "$line" ]]; do
    set $line
    eval $1="$2"
done < Version

AppVersionStrFull="$AppVersionStr"
DevParam=''
if [ "$DevChannel" != "0" ]; then
  AppVersionStrFull="$AppVersionStr.dev"
  DevParam='-dev'
fi

if [ ! -f "./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/tmacupd$AppVersion" ]; then
  echo "tmacupd$AppVersion not found!"
  exit 1
fi

if [ ! -f "./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/tsetup.$AppVersionStrFull.dmg" ]; then
  echo "tsetup.$AppVersionStrFull.dmg not found!"
  exit 1
fi

if [ ! -f "./../../tother/tmac32/$AppVersionStrMajor/$AppVersionStrFull/tmac32upd$AppVersion" ]; then
  echo "tmac32upd$AppVersion not found!"
  exit 1
fi

if [ ! -f "./../../tother/tmac32/$AppVersionStrMajor/$AppVersionStrFull/tsetup32.$AppVersionStrFull.dmg" ]; then
  echo "tsetup32.$AppVersionStrFull.dmg not found!"
  exit 1
fi

if [ ! -f "./../../tother/tsetup/$AppVersionStrMajor/$AppVersionStrFull/tupdate$AppVersion" ]; then
  echo "tupdate$AppVersion not found!"
  exit 1
fi

if [ ! -f "./../../tother/tsetup/$AppVersionStrMajor/$AppVersionStrFull/tportable.$AppVersionStrFull.zip" ]; then
  echo "tportable.$AppVersionStrFull.zip not found!"
  exit 1
fi

if [ ! -f "./../../tother/tsetup/$AppVersionStrMajor/$AppVersionStrFull/tsetup.$AppVersionStrFull.exe" ]; then
  echo "tsetup.$AppVersionStrFull.exe not found!"
  exit 1
fi

if [ ! -d "./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor" ]; then
  mkdir "./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor"
fi

scp ./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/tmacupd$AppVersion tmaster:tdesktop/www/tmac/
scp ./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/tsetup.$AppVersionStrFull.dmg tmaster:tdesktop/www/tmac/
scp ./../../tother/tmac32/$AppVersionStrMajor/$AppVersionStrFull/tmac32upd$AppVersion tmaster:tdesktop/www/tmac32/
scp ./../../tother/tmac32/$AppVersionStrMajor/$AppVersionStrFull/tsetup32.$AppVersionStrFull.dmg tmaster:tdesktop/www/tmac32/
scp ./../../tother/tsetup/$AppVersionStrMajor/$AppVersionStrFull/tupdate$AppVersion tmaster:tdesktop/www/tsetup/
scp ./../../tother/tsetup/$AppVersionStrMajor/$AppVersionStrFull/tportable.$AppVersionStrFull.zip tmaster:tdesktop/www/tsetup/
scp ./../../tother/tsetup/$AppVersionStrMajor/$AppVersionStrFull/tsetup.$AppVersionStrFull.exe tmaster:tdesktop/www/tsetup/

mv -v ./../../tother/tsetup/$AppVersionStrMajor/$AppVersionStrFull ./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor/

cp -v ./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/tmacupd$AppVersion ./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor/$AppVersionStrFull/
cp -v ./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/tsetup.$AppVersionStrFull.dmg ./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor/$AppVersionStrFull/
cp -rv ./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/Telegram.app.dSYM ./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor/$AppVersionStrFull/
cp -v ./../../tother/tmac32/$AppVersionStrMajor/$AppVersionStrFull/tmac32upd$AppVersion ./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor/$AppVersionStrFull/
cp -v ./../../tother/tmac32/$AppVersionStrMajor/$AppVersionStrFull/tsetup32.$AppVersionStrFull.dmg ./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor/$AppVersionStrFull/
cp -rv ./../../tother/tmac32/$AppVersionStrMajor/$AppVersionStrFull/Telegram.app.dSYM ./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor/$AppVersionStrFull/Telegram32.app.dSYM

