AppVersionStr=0.5.7
AppVersion=5007

if [ -d "./../Mac/Release/deploy/$AppVersionStr" ]; then
  echo "Deploy folder for version $AppVersionStr already exists!"
  exit 1
fi

if [ -f "./../Mac/Release/tupdate$AppVersion" ]; then
  echo "Update file for version $AppVersion already exists!"
  exit 1
fi

if [ ! -d "./../Mac/Release/Telegram.app" ]; then
  echo "Telegram.app not found!"
  exit 1
fi

if [ ! -f "./../Mac/Release/Telegram.app/Contents/Resources/Icon.icns" ]; then
  echo "Icon.icns not found in Resources!"
  exit 1
fi

if [ ! -f "./../Mac/Release/Telegram.app/Contents/MacOS/Telegram" ]; then
  echo "Telegram not found in MacOS!"
  exit 1
fi

if [ ! -f "./../Mac/Release/Telegram.app/Contents/Frameworks/Updater" ]; then
  echo "Icon.icns not found in Resources!"
  exit 1
fi

if [ ! -f "./../Mac/Release/Telegram.app.dmg" ]; then
  echo "Telegram.app.dmg not found!"
  exit 1
fi

echo "Preparing version $AppVersionStr, executing Packer.."
cd ./../Mac/Release && ./Packer.app/Contents/MacOS/Packer -path Telegram.app -version $AppVersion && cd ./../../Telegram
echo "Packer done!"

if [ ! -d "./../Mac/Release/deploy/" ]; then
  mkdir "./../Mac/Release/deploy"
fi
echo "Copying Telegram.app and tmacupd$AppVersion to deploy/$AppVersionStr..";
mkdir "./../Mac/Release/deploy/$AppVersionStr"
mkdir "./../Mac/Release/deploy/$AppVersionStr/Telegram"
cp -r ./../Mac/Release/Telegram.app ./../Mac/Release/deploy/$AppVersionStr/Telegram/
rm ./../Mac/Release/Telegram.app/Contents/MacOS/Telegram
rm ./../Mac/Release/Telegram.app/Contents/Frameworks/Updater
mv ./../Mac/Release/tmacupd$AppVersion ./../Mac/Release/deploy/$AppVersionStr/
mv ./../Mac/Release/Telegram.app.dmg ./../Mac/Release/deploy/$AppVersionStr/tsetup.$AppVersionStr.dmg
echo "Version $AppVersionStr prepared!";

