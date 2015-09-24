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

if [ -d "./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStr.dev" ]; then
  echo "Deploy folder for version $AppVersionStr.dev already exists!"
  exit 1
fi

if [ -d "./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStr" ]; then
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

echo "Preparing version $AppVersionStrFull, executing Packer.."
cd ./../Linux/Release && ./Packer -path Telegram -path Updater -version $AppVersion $DevParam && cd ./../../Telegram
echo "Packer done!"

if [ ! -d "./../Linux/Release/deploy" ]; then
  mkdir "./../Linux/Release/deploy"
fi

if [ ! -d "./../Linux/Release/deploy/$AppVersionStrMajor" ]; then
  mkdir "./../Linux/Release/deploy/$AppVersionStrMajor"
fi

echo "Copying Telegram, Updater and tlinux32upd$AppVersion to deploy/$AppVersionStrMajor/$AppVersionStrFull..";
mkdir "./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull"
mkdir "./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/Telegram"
mv ./../Linux/Release/Telegram ./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/Telegram/
mv ./../Linux/Release/Updater ./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/Telegram/
mv ./../Linux/Release/tlinux32upd$AppVersion ./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/
cd ./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull && tar -cJvf tsetup32.$AppVersionStrFull.tar.xz Telegram/ && cd ./../../../../../Telegram
echo "Version $AppVersionStrFull prepared!";

