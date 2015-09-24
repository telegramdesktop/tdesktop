while IFS='' read -r line || [[ -n "$line" ]]; do
    set $line
    eval $1="$2"
done < Version

AppVersionStrFull="$AppVersionStr"
DevParam=''
if [ "$DevChannel" != "0" ]; then
  AppVersionStrFull="$AppVersionStr.dev"
  DevParam='-dev'
fi

echo ""
echo "Preparing version $AppVersionStrFull.."
echo ""

if [ -d "./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStr.dev" ]; then
  echo "Deploy folder for version $AppVersionStr.dev already exists!"
  exit 1
fi

if [ -d "./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStr" ]; then
  echo "Deploy folder for version $AppVersionStr already exists!"
  exit 1
fi

if [ -f "./../Mac/Release/tmac32upd$AppVersion" ]; then
  echo "Update file for version $AppVersion already exists!"
  exit 1
fi

if [ ! -d "./../Mac/Release/Telegram.app" ]; then
  echo "Telegram.app not found!"
  exit 1
fi

if [ ! -d "./../Mac/Release/Telegram.app.dSYM" ]; then
  echo "Telegram.app.dSYM not found!"
  exit 1
fi

AppUUID=`dwarfdump -u "./../Mac/Release/Telegram.app/Contents/MacOS/Telegram" | awk -F " " '{print $2}'`
DsymUUID=`dwarfdump -u "./../Mac/Release/Telegram.app.dSYM" | awk -F " " '{print $2}'`
if [ "$AppUUID" != "$DsymUUID" ]; then
  echo "UUID of binary '$AppUUID' and dSYM '$DsymUUID' differ!"
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
  echo "Updater not found in Frameworks!"
  exit 1
fi

if [ ! -d "./../Mac/Release/Telegram.app/Contents/_CodeSignature" ]; then
  echo "Telegram signature not found!"
  exit 1
fi

cd ./../Mac/Release
temppath=`hdiutil attach -readwrite tsetup.dmg | awk -F "\t" 'END {print $3}'`
cp -R ./Telegram.app "$temppath/"
bless --folder "$temppath/" --openfolder "$temppath/"
hdiutil detach "$temppath"
hdiutil convert tsetup.dmg -format UDZO -imagekey zlib-level=9 -ov -o tsetup32.$AppVersionStrFull.dmg
cd ./../../Telegram
cd ./../Mac/Release && ./Packer.app/Contents/MacOS/Packer -path Telegram.app -version $AppVersion $DevParam && cd ./../../Telegram

if [ ! -d "./../Mac/Release/deploy" ]; then
  mkdir "./../Mac/Release/deploy"
fi

if [ ! -d "./../Mac/Release/deploy/$AppVersionStrMajor" ]; then
  mkdir "./../Mac/Release/deploy/$AppVersionStrMajor"
fi

echo "Copying Telegram.app and tmac32upd$AppVersion to deploy/$AppVersionStrMajor/$AppVersionStr..";
mkdir "./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull"
mkdir "./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/Telegram"
cp -r ./../Mac/Release/Telegram.app ./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/Telegram/
mv ./../Mac/Release/Telegram.app.dSYM ./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/
rm ./../Mac/Release/Telegram.app/Contents/MacOS/Telegram
rm ./../Mac/Release/Telegram.app/Contents/Frameworks/Updater
rm -rf ./../Mac/Release/Telegram.app/Contents/_CodeSignature
mv ./../Mac/Release/tmac32upd$AppVersion ./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/
mv ./../Mac/Release/tsetup32.$AppVersionStrFull.dmg ./../Mac/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/tsetup32.$AppVersionStrFull.dmg

echo "Version $AppVersionStrFull prepared!";

