AppVersion=`./Version.sh | awk -F " " '{print $1}'`
AppVersionStr=`./Version.sh | awk -F " " '{print $2}'`

echo ""
echo "Preparing version $AppVersionStr.."
echo ""

if [ -d "./../Mac/Release/deploy/$AppVersionStr" ]; then
  echo "Deploy folder for version $AppVersionStr already exists!"
  exit 1
fi

if [ ! -d "./../Mac/Release/Telegram Desktop.app" ]; then
  echo "Telegram Desktop.app not found!"
  exit 1
fi

if [ ! -f "./../Mac/Release/Telegram Desktop.app/Contents/Resources/Icon.icns" ]; then
  echo "Icon.icns not found in Resources!"
  exit 1
fi

if [ ! -f "./../Mac/Release/Telegram Desktop.app/Contents/MacOS/Telegram" ]; then
  echo "Telegram not found in MacOS!"
  exit 1
fi

if [ ! -f "./../Mac/Release/Telegram Desktop.app/Contents/Frameworks/Updater" ]; then
  echo "Updater not found in Frameworks!"
  exit 1
fi

cd ./../Mac/Release && codesign --force --deep --sign "Mac Developer: Peter Iakovlev (88P695A564)" "Telegram Desktop.app" && cd ./../../Telegram

if [ ! -d "./../Mac/Release/Telegram Desktop.app/Contents/_CodeSignature" ]; then
  echo "Telegram signature not found!"
  exit 1
fi

if [ ! -d "./../Mac/Release/deploy/" ]; then
  mkdir "./../Mac/Release/deploy"
fi

echo "Copying Telegram Desktop.app to deploy/$AppVersionStr..";
mkdir "./../Mac/Release/deploy/$AppVersionStr"
cp -r ./../Mac/Release/Telegram.app ./../Mac/Release/deploy/$AppVersionStr/
rm ./../Mac/Release/Telegram.app/Contents/MacOS/Telegram
rm ./../Mac/Release/Telegram.app/Contents/Frameworks/Updater
echo "Version $AppVersionStr prepared!";

