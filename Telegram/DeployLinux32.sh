while IFS='' read -r line || [[ -n "$line" ]]; do
    set $line
    eval $1="$2"
done < Version

DevPostfix=''
if [ "$DevChannel" != "0" ]; then
  DevPostfix='.dev'
fi

if [ ! -f "./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tlinux32upd$AppVersion" ]; then
    echo "tlinux32upd$AppVersion not found!"
    exit 1
fi

if [ ! -f "./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tsetup32.$AppVersionStr$DevPostfix.tar.xz" ]; then
    echo "tsetup32.$AppVersionStr$DevPostfix.zip not found!"
    exit 1
fi

scp ./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tlinux32upd$AppVersion tmaster:tdesktop/www/tlinux32/
scp ./../Linux/Release/deploy/$AppVersionStrMajor/$AppVersionStr$DevPostfix/tsetup32.$AppVersionStr$DevPostfix.tar.xz tmaster:tdesktop/www/tlinux32/

