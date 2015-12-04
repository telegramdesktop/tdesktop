set -e

DeployTarget="$1"

while IFS='' read -r line || [[ -n "$line" ]]; do
  set $line
  eval $1="$2"
done < Version

if [ "$BetaVersion" != "0" ]; then
  AppVersion="$BetaVersion"
  AppVersionStrFull="${AppVersionStr}_${BetaVersion}"
  DevParam="-beta $BetaVersion"
  BetaKeyFile="tbeta_${AppVersion}_key"
elif [ "$DevChannel" == "0" ]; then
  AppVersionStrFull="$AppVersionStr"
  DevParam=''
else
  AppVersionStrFull="$AppVersionStr.dev"
  DevParam='-dev'
fi

if [ ! -f "Target" ]; then
  echo "Deploy target not found!"
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
  DeployMac="0"
  DeployMac32="0"
  DeployWin="0"
  if [ "$DeployTarget" == "mac" ]; then
    DeployMac="1"
    echo "Deploying version $AppVersionStrFull for OS X 10.8+.."
  elif [ "$DeployTarget" == "mac32" ]; then
    DeployMac32="1"
    echo "Deploying version $AppVersionStrFull for OS X 10.6 and 10.7.."
  elif [ "$DeployTarget" == "win" ]; then
    DeployWin="1"
    echo "Deploying version $AppVersionStrFull for Windows.."
  else
    DeployMac="1"
    DeployMac32="1"
    DeployWin="1"
    echo "Deploying three versions of $AppVersionStrFull: for Windows, OS X 10.6 and 10.7 and OS X 10.8+.."
  fi
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
  DropboxSetupFile="$SetupFile"
  DropboxMac32SetupFile="$Mac32SetupFile"
elif [ "$BuildTarget" == "mac32" ] || [ "$BuildTarget" = "macstore" ]; then
  echo "No need to deploy this target."
  exit
else
  echo "Invalid target!"
  exit 1
fi

DeployPath="$ReleasePath/deploy/$AppVersionStrMajor/$AppVersionStrFull"

if [ "$BetaVersion" != "0" ]; then
  if [ "$DeployTarget" == "win" ]; then
    BetaFilePath="$WinDeployPath/$BetaKeyFile"
  elif [ "$DeployTarget" == "mac32" ]; then
    BetaFilePath="$Mac32DeployPath/$BetaKeyFile"
  else
    BetaFilePath="$DeployPath/$BetaKeyFile"
  fi
  if [ ! -f "$BetaFilePath" ]; then
    echo "Beta key file for $AppVersionStrFull not found :("
    exit 1
  fi

  while IFS='' read -r line || [[ -n "$line" ]]; do
    BetaSignature="$line"
  done < "$BetaFilePath"

  UpdateFile="${UpdateFile}_${BetaSignature}"
  if [ "$BuildTarget" == "linux" ] || [ "$BuildTarget" == "linux32" ]; then
    SetupFile="tbeta${BetaVersion}_${BetaSignature}.tar.xz"
  elif [ "$BuildTarget" == "mac" ]; then
    SetupFile="tbeta${BetaVersion}_${BetaSignature}.zip"
    DropboxSetupFile="tbeta${BetaVersion}_${BetaSignature}_mac.zip"
    Mac32UpdateFile="${Mac32UpdateFile}_${BetaSignature}"
    Mac32SetupFile="tbeta${BetaVersion}_${BetaSignature}.zip"
    DropboxMac32SetupFile="tbeta${BetaVersion}_${BetaSignature}_mac32.zip"
    WinUpdateFile="${WinUpdateFile}_${BetaSignature}"
    WinPortableFile="tbeta${BetaVersion}_${BetaSignature}.zip"
  fi
fi

#if [ "$BuildTarget" == "linux" ] || [ "$BuildTarget" == "linux32" ] || [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "mac32" ] || [ "$BuildTarget" == "macstore" ]; then

  if [ "$BuildTarget" != "mac" ] || [ "$DeployMac" == "1" ]; then
    if [ ! -f "$DeployPath/$UpdateFile" ]; then
      echo "$UpdateFile not found!";
      exit 1
    fi

    if [ ! -f "$DeployPath/$SetupFile" ]; then
      echo "$SetupFile not found!"
      exit 1
    fi
  fi

  if [ "$BuildTarget" == "mac" ]; then
    if [ "$DeployMac32" == "1" ]; then
      if [ ! -f "$Mac32DeployPath/$Mac32UpdateFile" ]; then
        echo "$Mac32UpdateFile not found!"
        exit 1
      fi

      if [ ! -f "$Mac32DeployPath/$Mac32SetupFile" ]; then
        echo "$Mac32SetupFile not found!"
        exit 1
      fi
    fi

    if [ "$DeployWin" == "1" ]; then
      if [ ! -f "$WinDeployPath/$WinUpdateFile" ]; then
        echo "$WinUpdateFile not found!"
        exit 1
      fi

      if [ "$BetaVersion" == "0" ]; then
        if [ ! -f "$WinDeployPath/$WinSetupFile" ]; then
          echo "$WinSetupFile not found!"
          exit 1
        fi
      fi

      if [ ! -f "$WinDeployPath/$WinPortableFile" ]; then
        echo "$WinPortableFile not found!"
        exit 1
      fi
    fi

    if [ ! -d "$DropboxPath" ]; then
      mkdir "$DropboxPath"
    fi

    if [ ! -d "$DropboxDeployPath" ]; then
      mkdir "$DropboxDeployPath"
    fi
  fi
#fi

if [ "$BuildTarget" == "linux" ] || [ "$BuildTarget" == "linux32" ] || [ "$BuildTarget" == "mac" ]; then
  if [ "$BuildTarget" != "mac" ] || [ "$DeployMac" == "1" ]; then
    scp "$DeployPath/$UpdateFile" "tmaster:tdesktop/www/$RemoteFolder/"
    scp "$DeployPath/$SetupFile" "tmaster:tdesktop/www/$RemoteFolder/"
  fi
  if [ "$BuildTarget" == "mac" ]; then
    if [ "$DeployMac32" == "1" ]; then
      scp "$Mac32DeployPath/$Mac32UpdateFile" "tmaster:tdesktop/www/$Mac32RemoteFolder/"
      scp "$Mac32DeployPath/$Mac32SetupFile" "tmaster:tdesktop/www/$Mac32RemoteFolder/"
    fi
    if [ "$DeployWin" == "1" ]; then
      scp "$WinDeployPath/$WinUpdateFile" "tmaster:tdesktop/www/$WinRemoteFolder/"
      if [ "$BetaVersion" == "0" ]; then
        scp "$WinDeployPath/$WinSetupFile" "tmaster:tdesktop/www/$WinRemoteFolder/"
      fi
      scp "$WinDeployPath/$WinPortableFile" "tmaster:tdesktop/www/$WinRemoteFolder/"
    fi

    if [ "$DeployMac" == "1" ]; then
      cp -v "$DeployPath/$UpdateFile" "$DropboxDeployPath/"
      cp -v "$DeployPath/$SetupFile" "$DropboxDeployPath/$DropboxSetupFile"
      if [ -d "$DropboxDeployPath/Telegram.app.dSYM" ]; then
        rm -rf "$DropboxDeployPath/Telegram.app.dSYM"
      fi
      cp -rv "$DeployPath/Telegram.app.dSYM" "$DropboxDeployPath/"
    fi
    if [ "$DeployMac32" == "1" ]; then
      mv -v "$Mac32DeployPath/$Mac32UpdateFile" "$DropboxDeployPath/"
      mv -v "$Mac32DeployPath/$Mac32SetupFile" "$DropboxDeployPath/$DropboxMac32SetupFile"
      if [ -d "$DropboxDeployPath/Telegram32.app.dSYM" ]; then
        rm -rf "$DropboxDeployPath/Telegram32.app.dSYM"
      fi
      mv -v "$Mac32DeployPath/Telegram.app.dSYM" "$DropboxDeployPath/Telegram32.app.dSYM"
    fi
    if [ "$DeployWin" == "1" ]; then
      mv -v "$WinDeployPath/Telegram.pdb" "$DropboxDeployPath/"
      mv -v "$WinDeployPath/Updater.exe" "$DropboxDeployPath/"
      mv -v "$WinDeployPath/Updater.pdb" "$DropboxDeployPath/"
      mv -v "$WinDeployPath/$WinUpdateFile" "$DropboxDeployPath/"
      if [ "$BetaVersion" == "0" ]; then
        mv -v "$WinDeployPath/$WinSetupFile" "$DropboxDeployPath/"
      fi
      mv -v "$WinDeployPath/$WinPortableFile" "$DropboxDeployPath/"
    fi
  fi
fi

echo "Version $AppVersionStrFull was deployed!";
