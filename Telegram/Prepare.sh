AppVersionStr=0.5.5
AppVersion=5005

if [ -d "deploy/$AppVersionStr" ]; then
  echo "Deploy folder for version $AppVersionStr already exists!"
  exit 1
fi

if [ -f "tupdate$AppVersion" ]; then
  echo "Update file for version $AppVersion already exists!"
  exit 1
fi

if [ ! -d "Telegram.app" ]; then
  echo "Telegram.app not found!"
  exit 1
fi
echo "Preparing version $AppVersionStr, executing Packer.."
./Packer.app/Contents/MacOS/Packer -path Telegram.app -version $AppVersion
echo "Packer done!"

if [ ! -d "deploy/" ]; then
  mkdir "deploy"
fi
echo "Copying Telegram.app and tmacupd$AppVersion to deploy/$AppVersionStr..";
mkdir "deploy/$AppVersionStr"
mkdir "deploy/$AppVersionStr/Telegram"
mv Telegram.app deploy/$AppVersionStr/Telegram/
mv tmacupd$AppVersion deploy/$AppVersionStr/
mv Telegram.dmg deploy/$AppVersionStr/
echo "Version $AppVersionStr prepared!";

