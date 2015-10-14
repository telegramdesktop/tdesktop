set -e

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

if [ ! -f "Target" ]; then
  echo "Build target not found!"
  exit 1
fi

while IFS='' read -r line || [[ -n "$line" ]]; do
  BuildTarget="$line"
done < Target

echo ""
if [ "$BuildTarget" == "linux" ]; then
  echo "Deploying version $AppVersionStrFull for Linux 64bit.."
  UpdateFile="tlinuxupd$AppVersion"
  SetupFile="tsetup.$AppVersionStrFull.tar.xz"
  ReleasePath="./../Linux/Release"
  RemoteFolder="tlinux"
elif [ "$BuildTarget" == "linux32" ]; then
  echo "Deploying version $AppVersionStrFull for Linux 32bit.."
  UpdateFile="tlinux32upd$AppVersion"
  SetupFile="tsetup32.$AppVersionStrFull.tar.xz"
  ReleasePath="./../Linux/Release"
  RemoteFolder="tlinux32"
elif [ "$BuildTarget" == "mac" ]; then
  echo "Deploying three versions of $AppVersionStrFull: for Windows, OS X 10.6 and 10.7 and OS X 10.8+.."
  UpdateFile="tmacupd$AppVersion"
  SetupFile="tsetup.$AppVersionStrFull.dmg"
  ReleasePath="./../Mac/Release"
  RemoteFolder="tmac"
  Mac32DeployPath="./../../tother/tmac32/$AppVersionStrMajor/$AppVersionStrFull"
  Mac32UpdateFile="tmac32upd$AppVersion"
  Mac32SetupFile="tsetup32.$AppVersionStrFull.dmg"
  Mac32RemoteFolder="tmac32"
  WinDeployPath="./../../tother/tsetup/$AppVersionStrMajor/$AppVersionStrFull"
  WinUpdateFile="tupdate$AppVersion"
  WinSetupFile="tsetup.$AppVersionStrFull.exe"
  WinPortableFile="tportable.$AppVersionStrFull.zip"
  WinRemoteFolder="tsetup"
  DropboxPath="./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor"
  DropboxDeployPath="$DropboxPath/$AppVersionStrFull"
elif [ "$BuildTarget" == "mac32" ] || [ "$BuildTarget" = "macstore" ]; then
  echo "No need to deploy this target."
  exit
else
  echo "Invalid target!"
  exit 1
fi

DeployPath="$ReleasePath/deploy/$AppVersionStrMajor/$AppVersionStrFull"

#if [ "$BuildTarget" == "linux" ] || [ "$BuildTarget" == "linux32" ] || [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "mac32" ] || [ "$BuildTarget" == "macstore" ]; then

  if [ ! -f "$DeployPath/$UpdateFile" ]; then
      echo "$UpdateFile not found!";
      exit 1
  fi

  if [ ! -f "$DeployPath/$SetupFile" ]; then
      echo "$SetupFile not found!"
      exit 1
  fi

  if [ "$BuildTarget" == "mac" ]; then
    if [ ! -f "$Mac32DeployPath/$Mac32UpdateFile" ]; then
      echo "$Mac32UpdateFile not found!"
      exit 1
    fi

    if [ ! -f "$Mac32DeployPath/$Mac32SetupFile" ]; then
      echo "$Mac32SetupFile not found!"
      exit 1
    fi

    if [ ! -f "$WinDeployPath/$WinUpdateFile" ]; then
      echo "$WinUpdateFile not found!"
      exit 1
    fi

    if [ ! -f "$WinDeployPath/$WinSetupFile" ]; then
      echo "$WinSetupFile not found!"
      exit 1
    fi

    if [ ! -f "$WinDeployPath/$WinPortableFile" ]; then
      echo "$WinPortableFile not found!"
      exit 1
    fi

    if [ ! -d "./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor" ]; then
      mkdir "./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor"
    fi
  fi
#fi

if [ "$BuildTarget" == "linux" ] || [ "$BuildTarget" == "linux32" ] || [ "$BuildTarget" == "mac" ]; then
  scp "$DeployPath/$UpdateFile" "tmaster:tdesktop/www/$RemoteFolder/"
  scp "$DeployPath/$SetupFile" "tmaster:tdesktop/www/$RemoteFolder/"

  if [ "$BuildTarget" == "mac" ]; then
    scp "$Mac32DeployPath/$Mac32UpdateFile" "tmaster:tdesktop/www/$Mac32RemoteFolder/"
    scp "$Mac32DeployPath/$Mac32SetupFile" "tmaster:tdesktop/www/$Mac32RemoteFolder/"
    scp "$WinDeployPath/$WinUpdateFile" "tmaster:tdesktop/www/$WinRemoteFolder/"
    scp "$WinDeployPath/$WinSetupFile" "tmaster:tdesktop/www/$WinRemoteFolder/"
    scp "$WinDeployPath/$WinPortableFile" "tmaster:tdesktop/www/$WinRemoteFolder/"

    mv -v "$WinDeployPath" "$DropboxPath/"

    cp -v "$DeployPath/$UpdateFile" "$DropboxDeployPath/"
    cp -v "$DeployPath/$SetupFile" "$DropboxDeployPath/"
    cp -rv "$DeployPath/Telegram.app.dSYM" "$DropboxDeployPath/"
    cp -v "$Mac32DeployPath/$Mac32UpdateFile" "$DropboxDeployPath/"
    cp -v "$Mac32DeployPath/$Mac32SetupFile" "$DropboxDeployPath/"
    cp -rv "$DeployPath/Telegram.app.dSYM" "$DropboxDeployPath/Telegram32.app.dSYM"
  fi
fi

echo "Version $AppVersionStrFull was deployed!";
