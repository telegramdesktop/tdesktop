AppVersionStr=0.5.6
AppVersion=5006

if [ -d "deploy/$AppVersionStr" ]; then
  echo "Deploy folder for version $AppVersionStr already exists!"
  exit 1
fi

if [ -f "tlinuxupd$AppVersion" ]; then
  echo "Update file for version $AppVersion already exists!"
  exit 1
fi

if [ ! -f "Telegram" ]; then
  echo "Telegram not found!"
  exit 1
fi

if [ ! -f "Updater" ]; then
  echo "Updater not found!"
  exit 1
fi

echo "Preparing version $AppVersionStr, executing Packer.."
./Packer -path Telegram -path Updater -version $AppVersion
echo "Packer done!"

if [ ! -d "deploy/" ]; then
  mkdir "deploy"
fi
echo "Copying Telegram, Updater and tlinuxupd$AppVersion to deploy/$AppVersionStr..";
mkdir "deploy/$AppVersionStr"
mkdir "deploy/$AppVersionStr/Telegram"
mv Telegram deploy/$AppVersionStr/Telegram/
mv Updater deploy/$AppVersionStr/Telegram/
mv tlinuxupd$AppVersion deploy/$AppVersionStr/
cd deploy/$AppVersionStr && tar -czvf tsetup.$AppVersionStr.tar.gz Telegram/ && cd ../..
echo "Version $AppVersionStr prepared!";

