AppVersionStrMajor=`./Version.sh | awk -F " " '{print $1}'`
AppVersion=`./Version.sh | awk -F " " '{print $2}'`
AppVersionStr=`./Version.sh | awk -F " " '{print $3}'`
DevChannel=`./Version.sh | awk -F " " '{print $4}'`
DevPostfix=''
DevParam=''
if [ "$DevChannel" != "0" ]; then
  DevPostfix='.dev'
  DevParam='-dev'
fi

if [ -d "./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStr.dev" ]; then
  echo "Deploy folder for version $AppVersionStr.dev already exists!"
  exit 1
fi

if [ -d "./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStr" ]; then
  echo "Deploy folder for version $AppVersionStr already exists!"
  exit 1
fi

if [ -f "./../Linux/Release/tlinux32upd$AppVersion" ]; then
  echo "Update file for version $AppVersion already exists!"
  exit 1
fi

if [ ! -f "./../Linux/Release/Telegram" ]; then
  echo "Telegram not found!"
  exit 1
fi

if [ ! -f "./../Linux/Release/Updater" ]; then
  echo "Updater not found!"
  exit 1
fi

echo "Preparing version $AppVersionStr$DevPostfix, executing Packer.."
cd ./../Linux/Release && ./Packer -path Telegram -path Updater -version $AppVersion $DevParam && cd ./../../Telegram
echo "Packer done!"

if [ ! -d "./../Linux/Release/deploy" ]; then
  mkdir "./../Linux/Release/deploy"
fi

if [ ! -d "./../Linux/Release/deploy/$AppVersionStrMajor" ]; then
  mkdir "./../Linux/Release/deploy/$AppVersionStrMajor"
fi

echo "Copying Telegram, Updater and tlinux32upd$AppVersion to deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix..";
mkdir "./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix"
mkdir "./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/Telegram"
mv ./../Linux/Release/Telegram ./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/Telegram/
mv ./../Linux/Release/Updater ./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/Telegram/
mv ./../Linux/Release/tlinux32upd$AppVersion ./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/
cd ./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix && tar -cJvf tsetup32.$AppVersionStr$DevPostfix.tar.xz Telegram/ && cd ./../../../../../Telegram
echo "Version $AppVersionStr$DevPostfix prepared!";

