AppVersionStrMajor=`./Version.sh | awk -F " " '{print $1}'`
AppVersion=`./Version.sh | awk -F " " '{print $2}'`
AppVersionStr=`./Version.sh | awk -F " " '{print $3}'`
DevChannel=`./Version.sh | awk -F " " '{print $4}'`
DevPostfix=''
if [ "$DevChannel" != "0" ]; then
  DevPostfix='.dev'
fi

if [ ! -f "./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tmac32upd$AppVersion" ]; then
  echo "tmac32upd$AppVersion not found!"
  exit 1
fi

if [ ! -f "./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tsetup32.$AppVersionStr$DevPostfix.dmg" ]; then
  echo "tsetup32.$AppVersionStr$DevPostfix.dmg not found!"
  exit 1
fi

if [ ! -d "./../../../TBuild/tother/tmac32/$AppVersionStrMajor" ]; then
  mkdir "./../../../TBuild/tother/tmac32/$AppVersionStrMajor"
fi

if [ ! -d "./../../../TBuild/tother/tmac32/$AppVersionStrMajor/$AppVersionStr$DevPostfix" ]; then
  mkdir "./../../../TBuild/tother/tmac32/$AppVersionStrMajor/$AppVersionStr$DevPostfix"
fi

cp -v ./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tmac32upd$AppVersion ./../../../TBuild/tother/tmac32/$AppVersionStrMajor/$AppVersionStr$DevPostfix/
cp -v ./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tsetup32.$AppVersionStr$DevPostfix.dmg ./../../../TBuild/tother/tmac32/$AppVersionStrMajor/$AppVersionStr$DevPostfix/
cp -rv ./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/Telegram.app.dSYM ./../../../TBuild/tother/tmac32/$AppVersionStrMajor/$AppVersionStr$DevPostfix/
