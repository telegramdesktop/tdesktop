AppVersion=`./Version.sh | awk -F " " '{print $1}'`
AppVersionStr=`./Version.sh | awk -F " " '{print $2}'`

if [ -d "./../Linux/Release/deploy/$AppVersionStr" ]; then
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

echo "Preparing version $AppVersionStr, executing Packer.."
cd ./../Linux/Release && ./Packer -path Telegram -path Updater -version $AppVersion && cd ./../../Telegram
echo "Packer done!"

if [ ! -d "./../Linux/Release/deploy" ]; then
  mkdir "./../Linux/Release/deploy"
fi
echo "Copying Telegram, Updater and tlinux32upd$AppVersion to deploy/$AppVersionStr..";
mkdir "./../Linux/Release/deploy/$AppVersionStr"
mkdir "./../Linux/Release/deploy/$AppVersionStr/Telegram"
mv ./../Linux/Release/Telegram ./../Linux/Release/deploy/$AppVersionStr/Telegram/
mv ./../Linux/Release/Updater ./../Linux/Release/deploy/$AppVersionStr/Telegram/
mv ./../Linux/Release/tlinux32upd$AppVersion ./../Linux/Release/deploy/$AppVersionStr/
cd ./../Linux/Release/deploy/$AppVersionStr && tar -cJvf tsetup32.$AppVersionStr.tar.xz Telegram/ && cd ./../../../../Telegram
echo "Version $AppVersionStr prepared!";

