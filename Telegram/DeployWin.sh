AppVersion=`./Version.sh | awk -F " " '{print $1}'`
AppVersionStr=`./Version.sh | awk -F " " '{print $2}'`
DevChannel=`./Version.sh | awk -F " " '{print $3}'`
DevPostfix=''
if [ "$DevChannel" != "0" ]; then
  DevPostfix='.dev'
fi

if [ ! -f "./../Win32/Deploy/deploy/$AppVersionStr$DevPostfix/tupdate$AppVersion" ]; then
    echo "tupdate$AppVersion not found!"
    exit 1
fi

if [ ! -f "./../Win32/Deploy/deploy/$AppVersionStr$DevPostfix/tportable.$AppVersionStr$DevPostfix.zip" ]; then
    echo "tportable.$AppVersionStr$DevPostfix.zip not found!"
    exit 1
fi

if [ ! -f "./../Win32/Deploy/deploy/$AppVersionStr$DevPostfix/tsetup.$AppVersionStr$DevPostfix.exe" ]; then
    echo "tsetup.$AppVersionStr$DevPostfix.exe not found!"
    exit 1
fi

cp -v ./../Win32/Deploy/deploy/$AppVersionStr$DevPostfix/tupdate$AppVersion /z/TBuild/tother/tsetup/
cp -v ./../Win32/Deploy/deploy/$AppVersionStr$DevPostfix/tportable.$AppVersionStr$DevPostfix.zip /z/TBuild/tother/tsetup/
cp -v ./../Win32/Deploy/deploy/$AppVersionStr$DevPostfix/tsetup.$AppVersionStr$DevPostfix.exe /z/TBuild/tother/tsetup/
