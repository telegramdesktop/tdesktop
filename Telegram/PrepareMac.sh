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

if [ ! -f "./../Mac/Release/Telegram Desktop.pkg" ]; then
  echo "Telegram Desktop.pkg not found!"
  exit 1
fi

if [ ! -d "./../Mac/Release/Telegram Desktop.app.dSYM" ]; then
  echo "Telegram Desktop.app.dSYM not found!"
  exit 1
fi

AppUUID=`dwarfdump -u "./../Mac/Release/Telegram Desktop.app/Contents/MacOS/Telegram" | awk -F " " '{print $2}'`
DsymUUID=`dwarfdump -u "./../Mac/Release/Telegram Desktop.app.dSYM" | awk -F " " '{print $2}'`
if [ "$AppUUID" != "$DsymUUID" ]; then
  echo "UUID of binary '$AppUUID' and dSYM '$DsymUUID' differ!"
  exit 1
fi

if [ ! -f "./../Mac/Release/Telegram Desktop.app/Contents/Resources/Icon.icns" ]; then
  echo "Icon.icns not found in Resources!"
  exit 1
fi

if [ ! -f "./../Mac/Release/Telegram Desktop.app/Contents/MacOS/Telegram Desktop" ]; then
  echo "Telegram Desktop not found in MacOS!"
  exit 1
fi

if [ ! -d "./../Mac/Release/Telegram Desktop.app/Contents/_CodeSignature" ]; then
  echo "Telegram Desktop signature not found!"
  exit 1
fi

if [ ! -d "./../Mac/Release/deploy/" ]; then
  mkdir "./../Mac/Release/deploy"
fi

echo "Copying Telegram Desktop.app to deploy/$AppVersionStr..";
mkdir "./../Mac/Release/deploy/$AppVersionStr"
cp -r "./../Mac/Release/Telegram Desktop.app" ./../Mac/Release/deploy/$AppVersionStr/
mv "./../Mac/Release/Telegram Desktop.pkg" ./../Mac/Release/deploy/$AppVersionStr/
mv "./../Mac/Release/Telegram Desktop.app.dSYM" ./../Mac/Release/deploy/$AppVersionStr/
rm "./../Mac/Release/Telegram Desktop.app/Contents/MacOS/Telegram Desktop"
rm -rf "./../Mac/Release/Telegram Desktop.app/Contents/_CodeSignature"
echo "Version $AppVersionStr prepared!";

