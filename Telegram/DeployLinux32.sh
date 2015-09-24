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

if [ ! -f "./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/tlinux32upd$AppVersion" ]; then
    echo "tlinux32upd$AppVersion not found!"
    exit 1
fi

if [ ! -f "./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/tsetup32.$AppVersionStrFull.tar.xz" ]; then
    echo "tsetup32.$AppVersionStrFull.zip not found!"
    exit 1
fi

scp ./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/tlinux32upd$AppVersion tmaster:tdesktop/www/tlinux32/
scp ./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/tsetup32.$AppVersionStrFull.tar.xz tmaster:tdesktop/www/tlinux32/

