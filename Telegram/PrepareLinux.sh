AppVersion=`./Version.sh | awk -F " " '{print $1}'`
AppVersionStr=`./Version.sh | awk -F " " '{print $2}'`
DevChannel=`./Version.sh | awk -F " " '{print $3}'`
DevPostfix=''
DevParam=''
if [ "$DevChannel" != "0" ]; then
  DevPostfix='.dev'
  DevParam='-dev'
fi

if [ -d "./../Linux/Release/deploy/$AppVersionStr.dev" ]; then
  echo "Deploy folder for version $AppVersionStr.dev already exists!"
  exit 1
fi

if [ -d "./../Linux/Release/deploy/$AppVersionStr" ]; then
  echo "Deploy folder for version $AppVersionStr already exists!"
  exit 1
fi

if [ -f "./../Linux/Release/tlinuxupd$AppVersion" ]; then
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
echo "Copying Telegram, Updater and tlinuxupd$AppVersion to deploy/$AppVersionStr$DevPostfix..";
mkdir "./../Linux/Release/deploy/$AppVersionStr$DevPostfix"
mkdir "./../Linux/Release/deploy/$AppVersionStr$DevPostfix/Telegram"
mv ./../Linux/Release/Telegram ./../Linux/Release/deploy/$AppVersionStr$DevPostfix/Telegram/
mv ./../Linux/Release/Updater ./../Linux/Release/deploy/$AppVersionStr$DevPostfix/Telegram/
mv ./../Linux/Release/tlinuxupd$AppVersion ./../Linux/Release/deploy/$AppVersionStr$DevPostfix/
cd ./../Linux/Release/deploy/$AppVersionStr$DevPostfix && tar -cJvf tsetup.$AppVersionStr$DevPostfix.tar.xz Telegram/ && cd ./../../../../Telegram
echo "Version $AppVersionStr$DevPostfix prepared!";

