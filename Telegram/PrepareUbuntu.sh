AppVersionStr=0.5.7
AppVersion=5007

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

echo "Preparing version $AppVersionStr, executing Packer.."
cd ./../Linux/Release && ./Packer -path Telegram -path Updater -version $AppVersion && cd ./../../Telegram
echo "Packer done!"

if [ ! -d "./../Linux/Release/deploy" ]; then
  mkdir "./../Linux/Release/deploy"
fi
echo "Copying Telegram, Updater and tlinuxupd$AppVersion to deploy/$AppVersionStr..";
mkdir "./../Linux/Release/deploy/$AppVersionStr"
mkdir "./../Linux/Release/deploy/$AppVersionStr/Telegram"
mv ./../Linux/Release/Telegram ./../Linux/Release/deploy/$AppVersionStr/Telegram/
mv ./../Linux/Release/Updater ./../Linux/Release/deploy/$AppVersionStr/Telegram/
mv ./../Linux/Release/tlinuxupd$AppVersion ./../Linux/Release/deploy/$AppVersionStr/
cd ./../Linux/Release/deploy/$AppVersionStr && tar -czvf tsetup.$AppVersionStr.tar.gz Telegram/ && cd ./../../../../Telegram
echo "Version $AppVersionStr prepared!";

