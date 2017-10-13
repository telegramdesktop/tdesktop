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
  Mac32DeployPath="$HomePath/../../deploy_temp/tmac32/$AppVersionStrMajor/$AppVersionStrFull"
  Mac32UpdateFile="tmac32upd$AppVersion"
  Mac32SetupFile="tsetup32.$AppVersionStrFull.dmg"
  Mac32RemoteFolder="tmac32"
  WinDeployPath="$HomePath/../../deploy_temp/tsetup/$AppVersionStrMajor/$AppVersionStrFull"
  WinUpdateFile="tupdate$AppVersion"
  WinSetupFile="tsetup.$AppVersionStrFull.exe"
  WinPortableFile="tportable.$AppVersionStrFull.zip"
  WinRemoteFolder="tsetup"
  BackupPath="$HOME/Telegram/backup/$AppVersionStrMajor/$AppVersionStrFull"
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
    Mac32UpdateFile="${Mac32UpdateFile}_${BetaSignature}"
    Mac32SetupFile="tbeta${BetaVersion}_${BetaSignature}.zip"
    WinUpdateFile="${WinUpdateFile}_${BetaSignature}"
    WinPortableFile="tbeta${BetaVersion}_${BetaSignature}.zip"
  fi
elif [ "$BuildTarget" == "linux" ] || [ "$BuildTarget" == "linux32" ]; then
  BackupPath="/media/psf/backup/$AppVersionStrMajor/$AppVersionStrFull/t$BuildTarget"
  if [ ! -d "/media/psf/backup" ]; then
    Error "Backup folder not found!"
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
  fi
#fi

if [ "$BuildTarget" == "linux" ] || [ "$BuildTarget" == "linux32" ] || [ "$BuildTarget" == "mac" ]; then
  if [ "$BuildTarget" != "mac" ] || [ "$DeployMac" == "1" ]; then
    rsync -av --progress "$DeployPath/$UpdateFile" "$DeployPath/$SetupFile" "tmaster:tdesktop/www/$RemoteFolder/"
  fi
  if [ "$BuildTarget" == "linux" ] || [ "$BuildTarget" == "linux32" ]; then
    if [ "$BetaVersion" == "0" ]; then
      mkdir -p "$BackupPath"
      cp "$DeployPath/$SetupFile" "$BackupPath"
    fi
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
      if [ "$BetaVersion" == "0" ]; then
        mkdir -p "$BackupPath/tmac"
        mv -v "$DeployPath/$SetupFile" "$BackupPath/tmac/"
      fi
    fi
    if [ "$DeployMac32" == "1" ]; then
      if [ "$BetaVersion" == "0" ]; then
        mkdir -p "$BackupPath/tmac32"
        mv -v "$Mac32DeployPath/$Mac32SetupFile" "$BackupPath/tmac32/"
      fi
    fi
    if [ "$DeployWin" == "1" ]; then
      if [ "$BetaVersion" == "0" ]; then
        mkdir -p "$BackupPath/tsetup"
        mv -v "$WinDeployPath/$WinSetupFile" "$BackupPath/tsetup/"
        mv -v "$WinDeployPath/$WinPortableFile" "$BackupPath/tsetup/"
      fi
    fi
  fi
fi

echo "Version $AppVersionStrFull was deployed!"
cd $FullExecPath

