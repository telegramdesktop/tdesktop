while IFS='' read -r line || [[ -n "$line" ]]; do
    set $line
    eval $1="$2"
done < Version

DevPostfix=''
if [ "$DevChannel" != "0" ]; then
  DevPostfix='.dev'
fi

if [ ! -f "./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tlinuxupd$AppVersion" ]; then
    echo "tlinuxupd$AppVersion not found!";
    exit 1
fi

if [ ! -f "./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tsetup.$AppVersionStr$DevPostfix.tar.xz" ]; then
    echo "tsetup.$AppVersionStr$DevPostfix.tar.xz not found!"
    exit 1
fi

scp ./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tlinuxupd$AppVersion tmaster:tdesktop/www/tlinux/
scp ./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tsetup.$AppVersionStr$DevPostfix.tar.xz tmaster:tdesktop/www/tlinux/
