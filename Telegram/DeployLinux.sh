AppVersion=`./Version.sh | awk -F " " '{print $1}'`
AppVersionStr=`./Version.sh | awk -F " " '{print $2}'`
DevChannel=`./Version.sh | awk -F " " '{print $3}'`
DevPostfix=''
if [ "$DevChannel" != "0" ]; then
  DevPostfix='.dev'
fi

if [ ! -f "./../Linux/Release/deploy/$AppVersionStr$DevPostfix/tlinuxupd$AppVersion" ]; then
    echo "tlinuxupd$AppVersion not found!";
    exit 1
fi

if [ ! -f "./../Linux/Release/deploy/$AppVersionStr$DevPostfix/tsetup.$AppVersionStr$DevPostfix.tar.xz" ]; then
    echo "tsetup.$AppVersionStr$DevPostfix.tar.xz not found!"
    exit 1
fi

scp ./../Linux/Release/deploy/$AppVersionStr$DevPostfix/tlinuxupd$AppVersion tmaster:tdesktop/www/tlinux/
scp ./../Linux/Release/deploy/$AppVersionStr$DevPostfix/tsetup.$AppVersionStr$DevPostfix.tar.xz tmaster:tdesktop/www/tlinux/
