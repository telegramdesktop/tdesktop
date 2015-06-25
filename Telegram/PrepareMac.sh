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

echo ""
echo "Preparing version $AppVersionStr$DevPostfix.."
echo ""

if [ -d "./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStr.dev" ]; then
  echo "Deploy folder for version $AppVersionStr.dev already exists!"
  exit 1
fi

if [ -d "./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStr" ]; then
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

AppUUID=`dwarfdump -u "./../Mac/Release/Telegram Desktop.app/Contents/MacOS/Telegram Desktop" | awk -F " " '{print $2}'`
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

if [ ! -d "./../Mac/Release/deploy/$AppVersionStrMajor" ]; then
  mkdir "./../Mac/Release/deploy/$AppVersionStrMajor"
fi

echo "Copying Telegram Desktop.app to deploy/$AppVersionStrMajor/$AppVersionStr..";
mkdir "./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStr"
cp -r "./../Mac/Release/Telegram Desktop.app" ./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStr/
mv "./../Mac/Release/Telegram Desktop.pkg" ./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStr/
mv "./../Mac/Release/Telegram Desktop.app.dSYM" ./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStr/
rm "./../Mac/Release/Telegram Desktop.app/Contents/MacOS/Telegram Desktop"
rm -rf "./../Mac/Release/Telegram Desktop.app/Contents/_CodeSignature"
echo "Version $AppVersionStr prepared!";

