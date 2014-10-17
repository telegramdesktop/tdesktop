AppVersionStr=0.6.3
AppVersion=6003

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

scp ./../Mac/Release/deploy/$AppVersionStr/tmacupd$AppVersion tupdates:tdesktop/static/tmac/
scp ./../Mac/Release/deploy/$AppVersionStr/tsetup.$AppVersionStr.dmg tupdates:tdesktop/static/tmac/
scp ./../../tother/tsetup/tupdate$AppVersion tupdates:tdesktop/static/tsetup/
scp ./../../tother/tsetup/tportable.$AppVersionStr.zip tupdates:tdesktop/static/tsetup/
scp ./../../tother/tsetup/tsetup.$AppVersionStr.exe tupdates:tdesktop/static/tsetup/

