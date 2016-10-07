set -e
FullExecPath=$PWD
pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

if [ ! -d "$FullScriptPath/../../../TelegramPrivate" ]; then
  echo ""
  echo "This script is for building the production version of Telegram Desktop."
  echo ""
  echo "For building custom versions please visit the build instructions page at:"
  echo "https://github.com/telegramdesktop/tdesktop/#build-instructions"
  exit
fi

Error () {
  cd $FullExecPath
  echo "$1"
  exit 1
}

DeployTarget="$1"

if [ ! -f "$FullScriptPath/target" ]; then
  Error "Build target not found!"
fi

while IFS='' read -r line || [[ -n "$line" ]]; do
  BuildTarget="$line"
done < "$FullScriptPath/target"

while IFS='' read -r line || [[ -n "$line" ]]; do
  set $line
  eval $1="$2"
done < "$FullScriptPath/version"

if [ "$BetaVersion" != "0" ]; then
  AppVersion="$BetaVersion"
  AppVersionStrFull="${AppVersionStr}_${BetaVersion}"
  BetaKeyFile="tbeta_${AppVersion}_key"
elif [ "$AlphaChannel" == "0" ]; then
  AppVersionStrFull="$AppVersionStr"
else
  AppVersionStrFull="$AppVersionStr.alpha"
fi

echo ""
HomePath="$FullScriptPath/.."
if [ "$BuildTarget" == "linux" ]; then
  echo "Deploying version $AppVersionStrFull for Linux 64bit.."
  UpdateFile="tlinuxupd$AppVersion"
  SetupFile="tsetup.$AppVersionStrFull.tar.xz"
  ReleasePath="$HomePath/../out/Release"
  RemoteFolder="tlinux"
elif [ "$BuildTarget" == "linux32" ]; then
  echo "Deploying version $AppVersionStrFull for Linux 32bit.."
  UpdateFile="tlinux32upd$AppVersion"
  SetupFile="tsetup32.$AppVersionStrFull.tar.xz"
  ReleasePath="$HomePath/../out/Release"
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
    if [ "$BetaVersion" != "0" ]; then
      DeployMac32="0"
    else
      DeployMac32="1"
    fi
    DeployWin="1"
    echo "Deploying three versions of $AppVersionStrFull: for Windows, OS X 10.6 and 10.7 and OS X 10.8+.."
  fi
  UpdateFile="tmacupd$AppVersion"
  SetupFile="tsetup.$AppVersionStrFull.dmg"
  ReleasePath="$HomePath/../out/Release"
  RemoteFolder="tmac"
  Mac32DeployPath="$HomePath/../../tother/tmac32/$AppVersionStrMajor/$AppVersionStrFull"
  Mac32UpdateFile="tmac32upd$AppVersion"
  Mac32SetupFile="tsetup32.$AppVersionStrFull.dmg"
  Mac32RemoteFolder="tmac32"
  WinDeployPath="$HomePath/../../tother/tsetup/$AppVersionStrMajor/$AppVersionStrFull"
  WinUpdateFile="tupdate$AppVersion"
  WinSetupFile="tsetup.$AppVersionStrFull.exe"
  WinPortableFile="tportable.$AppVersionStrFull.zip"
  WinRemoteFolder="tsetup"
  DropboxPath="/Volumes/Storage/Dropbox/Telegram/deploy/$AppVersionStrMajor"
  DropboxDeployPath="$DropboxPath/$AppVersionStrFull"
  DropboxSetupFile="$SetupFile"
  DropboxMac32SetupFile="$Mac32SetupFile"
elif [ "$BuildTarget" == "mac32" ] || [ "$BuildTarget" = "macstore" ]; then
  Error "No need to deploy this target."
else
  Error "Invalid target!"
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
    Error "Beta key file for $AppVersionStrFull not found :("
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
      Error "$UpdateFile not found!";
    fi

    if [ ! -f "$DeployPath/$SetupFile" ]; then
      Error "$SetupFile not found!"
    fi
  fi

  if [ "$BuildTarget" == "mac" ]; then
    if [ "$DeployMac32" == "1" ]; then
      if [ ! -f "$Mac32DeployPath/$Mac32UpdateFile" ]; then
        Error "$Mac32UpdateFile not found!"
      fi

      if [ ! -f "$Mac32DeployPath/$Mac32SetupFile" ]; then
        Error "$Mac32SetupFile not found!"
      fi
    fi

    if [ "$DeployWin" == "1" ]; then
      if [ ! -f "$WinDeployPath/$WinUpdateFile" ]; then
        Error "$WinUpdateFile not found!"
      fi

      if [ "$BetaVersion" == "0" ]; then
        if [ ! -f "$WinDeployPath/$WinSetupFile" ]; then
          Error "$WinSetupFile not found!"
        fi
      fi

      if [ ! -f "$WinDeployPath/$WinPortableFile" ]; then
        Error "$WinPortableFile not found!"
      fi
    fi

    mkdir -p "$DropboxDeployPath"
  fi
#fi

if [ "$BuildTarget" == "linux" ] || [ "$BuildTarget" == "linux32" ] || [ "$BuildTarget" == "mac" ]; then
  if [ "$BuildTarget" != "mac" ] || [ "$DeployMac" == "1" ]; then
    rsync -av --progress "$DeployPath/$UpdateFile" "$DeployPath/$SetupFile" "tmaster:tdesktop/www/$RemoteFolder/"
  fi
  if [ "$BuildTarget" == "mac" ]; then
    if [ "$DeployMac32" == "1" ]; then
      rsync -av --progress "$Mac32DeployPath/$Mac32UpdateFile" "$Mac32DeployPath/$Mac32SetupFile" "tmaster:tdesktop/www/$Mac32RemoteFolder/"
    fi
    if [ "$DeployWin" == "1" ]; then
      if [ "$BetaVersion" == "0" ]; then
        rsync -av --progress "$WinDeployPath/$WinUpdateFile" "$WinDeployPath/$WinSetupFile" "$WinDeployPath/$WinPortableFile" "tmaster:tdesktop/www/$WinRemoteFolder/"
      else
        rsync -av --progress "$WinDeployPath/$WinUpdateFile" "$WinDeployPath/$WinPortableFile" "tmaster:tdesktop/www/$WinRemoteFolder/"
      fi
    fi

    if [ "$DeployMac" == "1" ]; then
      cp -v "$DeployPath/$SetupFile" "$DropboxDeployPath/$DropboxSetupFile"
      if [ -d "$DropboxDeployPath/Telegram.app.dSYM" ]; then
        rm -rf "$DropboxDeployPath/Telegram.app.dSYM"
      fi
      cp -rv "$DeployPath/Telegram.app.dSYM" "$DropboxDeployPath/"
    fi
    if [ "$DeployMac32" == "1" ]; then
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
      if [ "$BetaVersion" == "0" ]; then
        mv -v "$WinDeployPath/$WinSetupFile" "$DropboxDeployPath/"
      fi
      mv -v "$WinDeployPath/$WinPortableFile" "$DropboxDeployPath/"
    fi
  fi
fi

echo "Version $AppVersionStrFull was deployed!"
cd $FullExecPath

