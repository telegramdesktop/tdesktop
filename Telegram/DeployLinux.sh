AppVersionStr=0.6.7
AppVersion=6007

if [ ! -f "./../Linux/Release/deploy/$AppVersionStr/tlinuxupd$AppVersion" ]; then
    echo "tlinuxupd$AppVersion not found!";
    exit 1
fi

if [ ! -f "./../Linux/Release/deploy/$AppVersionStr/tsetup.$AppVersionStr.tar.xz" ]; then
    echo "tsetup.$AppVersionStr.tar.xz not found!"
    exit 1
fi

scp ./../Linux/Release/deploy/$AppVersionStr/tlinuxupd$AppVersion tupdates:tdesktop/static/tlinux/
scp ./../Linux/Release/deploy/$AppVersionStr/tsetup.$AppVersionStr.tar.xz tupdates:tdesktop/static/tlinux/

