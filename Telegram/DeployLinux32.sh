AppVersion=`./Version.sh | awk -F " " '{print $1}'`
AppVersionStr=`./Version.sh | awk -F " " '{print $2}'`

if [ ! -f "./../Linux/Release/deploy/$AppVersionStr/tlinux32upd$AppVersion" ]; then
    echo "tlinux32upd$AppVersion not found!"
    exit 1
fi

if [ ! -f "./../Linux/Release/deploy/$AppVersionStr/tsetup32.$AppVersionStr.tar.xz" ]; then
    echo "tsetup32.$AppVersionStr.zip not found!"
    exit 1
fi

scp ./../Linux/Release/deploy/$AppVersionStr/tlinux32upd$AppVersion tmaster:tdesktop/www/tlinux32/
scp ./../Linux/Release/deploy/$AppVersionStr/tsetup32.$AppVersionStr.tar.xz tmaster:tdesktop/www/tlinux32/

