AppVersion=`./Version.sh | awk -F " " '{print $1}'`
AppVersionStr=`./Version.sh | awk -F " " '{print $2}'`

if [ ! -f "./../Mac/Release/deploy/$AppVersionStr/tmacupd$AppVersion" ]; then
    echo "tmacupd$AppVersion not found!"
    exit 1
fi

if [ ! -f "./../Mac/Release/deploy/$AppVersionStr/tsetup.$AppVersionStr.dmg" ]; then
    echo "tsetup.$AppVersionStr.dmg not found!"
    exit 1
fi

if [ ! -f "./../../tother/tsetup/tupdate$AppVersion" ]; then
    echo "tupdate$AppVersion not found!"
    exit 1
fi

if [ ! -f "./../../tother/tsetup/tportable.$AppVersionStr.zip" ]; then
    echo "tportable.$AppVersionStr.zip not found!"
    exit 1
fi

if [ ! -f "./../../tother/tsetup/tsetup.$AppVersionStr.exe" ]; then
    echo "tsetup.$AppVersionStr.exe not found!"
    exit 1
fi

scp ./../Mac/Release/deploy/$AppVersionStr/tmacupd$AppVersion tmaster:tdesktop/www/tmac/
scp ./../Mac/Release/deploy/$AppVersionStr/tsetup.$AppVersionStr.dmg tmaster:tdesktop/www/tmac/
scp ./../../tother/tsetup/tupdate$AppVersion tmaster:tdesktop/www/tsetup/
scp ./../../tother/tsetup/tportable.$AppVersionStr.zip tmaster:tdesktop/www/tsetup/
scp ./../../tother/tsetup/tsetup.$AppVersionStr.exe tmaster:tdesktop/www/tsetup/
