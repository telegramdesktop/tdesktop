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

if [ ! -f "./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/tlinuxupd$AppVersion" ]; then
    echo "tlinuxupd$AppVersion not found!";
    exit 1
fi

if [ ! -f "./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/tsetup.$AppVersionStrFull.tar.xz" ]; then
    echo "tsetup.$AppVersionStrFull.tar.xz not found!"
    exit 1
fi

scp ./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/tlinuxupd$AppVersion tmaster:tdesktop/www/tlinux/
scp ./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStrFull/tsetup.$AppVersionStrFull.tar.xz tmaster:tdesktop/www/tlinux/
