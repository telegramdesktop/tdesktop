AppVersionStrMajor=`./Version.sh | awk -F " " '{print $1}'`
AppVersion=`./Version.sh | awk -F " " '{print $2}'`
AppVersionStr=`./Version.sh | awk -F " " '{print $3}'`
DevChannel=`./Version.sh | awk -F " " '{print $4}'`
DevPostfix=''
if [ "$DevChannel" != "0" ]; then
  DevPostfix='.dev'
fi

if [ ! -f "./../Win32/Deploy/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tupdate$AppVersion" ]; then
  echo "tupdate$AppVersion not found!"
  exit 1
fi

if [ ! -f "./../Win32/Deploy/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tportable.$AppVersionStr$DevPostfix.zip" ]; then
  echo "tportable.$AppVersionStr$DevPostfix.zip not found!"
  exit 1
fi

if [ ! -f "./../Win32/Deploy/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tsetup.$AppVersionStr$DevPostfix.exe" ]; then
  echo "tsetup.$AppVersionStr$DevPostfix.exe not found!"
  exit 1
fi

if [ ! -d "/z/TBuild/tother/tsetup/$AppVersionStrMajor" ]; then
  mkdir "/z/TBuild/tother/tsetup/$AppVersionStrMajor"
fi

if [ ! -d "/z/TBuild/tother/tsetup/$AppVersionStrMajor/$AppVersionStr$DevPostfix" ]; then
  mkdir "/z/TBuild/tother/tsetup/$AppVersionStrMajor/$AppVersionStr$DevPostfix"
fi

cp -v ./../Win32/Deploy/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tupdate$AppVersion /z/TBuild/tother/tsetup/$AppVersionStrMajor/$AppVersionStr$DevPostfix/
cp -v ./../Win32/Deploy/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tportable.$AppVersionStr$DevPostfix.zip /z/TBuild/tother/tsetup/$AppVersionStrMajor/$AppVersionStr$DevPostfix/
cp -v ./../Win32/Deploy/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tsetup.$AppVersionStr$DevPostfix.exe /z/TBuild/tother/tsetup/$AppVersionStrMajor/$AppVersionStr$DevPostfix/
cp -v ./../Win32/Deploy/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/Telegram.pdb /z/TBuild/tother/tsetup/$AppVersionStrMajor/$AppVersionStr$DevPostfix/
cp -v ./../Win32/Deploy/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/Updater.exe /z/TBuild/tother/tsetup/$AppVersionStrMajor/$AppVersionStr$DevPostfix/
cp -v ./../Win32/Deploy/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/Updater.pdb /z/TBuild/tother/tsetup/$AppVersionStrMajor/$AppVersionStr$DevPostfix/

