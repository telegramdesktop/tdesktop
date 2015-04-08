AppVersion=`./Version.sh | awk -F " " '{print $1}'`
AppVersionStr=`./Version.sh | awk -F " " '{print $2}'`
DevChannel=`./Version.sh | awk -F " " '{print $3}'`
DevPostfix=''
if [ "$DevChannel" != "0" ]; then
  DevPostfix='.dev'
fi

if [ ! -f "./../Mac/Release/deploy/$AppVersionStr$DevPostfix/tmacupd$AppVersion" ]; then
    echo "tmacupd$AppVersion not found!"
    exit 1
fi

if [ ! -f "./../Mac/Release/deploy/$AppVersionStr$DevPostfix/tsetup.$AppVersionStr$DevPostfix.dmg" ]; then
    echo "tsetup.$AppVersionStr$DevPostfix.dmg not found!"
    exit 1
fi

if [ ! -f "./../../tother/tsetup/tupdate$AppVersion" ]; then
    echo "tupdate$AppVersion not found!"
    exit 1
fi

if [ ! -f "./../../tother/tsetup/tportable.$AppVersionStr$DevPostfix.zip" ]; then
    echo "tportable.$AppVersionStr$DevPostfix.zip not found!"
    exit 1
fi

if [ ! -f "./../../tother/tsetup/tsetup.$AppVersionStr$DevPostfix.exe" ]; then
    echo "tsetup.$AppVersionStr$DevPostfix.exe not found!"
    exit 1
fi

scp ./../Mac/Release/deploy/$AppVersionStr$DevPostfix/tmacupd$AppVersion tmaster:tdesktop/www/tmac/
scp ./../Mac/Release/deploy/$AppVersionStr$DevPostfix/tsetup.$AppVersionStr$DevPostfix.dmg tmaster:tdesktop/www/tmac/
scp ./../../tother/tsetup/tupdate$AppVersion tmaster:tdesktop/www/tsetup/
scp ./../../tother/tsetup/tportable.$AppVersionStr$DevPostfix.zip tmaster:tdesktop/www/tsetup/
scp ./../../tother/tsetup/tsetup.$AppVersionStr$DevPostfix.exe tmaster:tdesktop/www/tsetup/
