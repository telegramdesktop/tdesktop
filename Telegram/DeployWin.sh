AppVersionStr=0.6.8
AppVersion=6008

if [ ! -f "./../Win32/Deploy/deploy/$AppVersionStr/tupdate$AppVersion" ]; then
    echo "tupdate$AppVersion not found!"
    exit 1
fi

if [ ! -f "./../Win32/Deploy/deploy/$AppVersionStr/tportable.$AppVersionStr.zip" ]; then
    echo "tportable.$AppVersionStr.zip not found!"
    exit 1
fi

if [ ! -f "./../Win32/Deploy/deploy/$AppVersionStr/tsetup.$AppVersionStr.exe" ]; then
    echo "tsetup.$AppVersionStr.exe not found!"
    exit 1
fi

cp -v ./../Win32/Deploy/deploy/$AppVersionStr/tupdate$AppVersion /z/TBuild/tother/tsetup/
cp -v ./../Win32/Deploy/deploy/$AppVersionStr/tportable.$AppVersionStr.zip /z/TBuild/tother/tsetup/
cp -v ./../Win32/Deploy/deploy/$AppVersionStr/tsetup.$AppVersionStr.exe /z/TBuild/tother/tsetup/
