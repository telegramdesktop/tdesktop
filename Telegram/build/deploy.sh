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
DeployMac="0"
DeployMac32="0"
DeployWin="0"
DeployLinux="0"
DeployLinux32="0"
if [ "$DeployTarget" == "mac" ]; then
  DeployMac="1"
  echo "Deploying version $AppVersionStrFull for OS X 10.8+.."
elif [ "$DeployTarget" == "mac32" ]; then
  DeployMac32="1"
  echo "Deploying version $AppVersionStrFull for OS X 10.6 and 10.7.."
elif [ "$DeployTarget" == "win" ]; then
  DeployWin="1"
  echo "Deploying version $AppVersionStrFull for Windows.."
elif [ "$DeployTarget" == "linux" ]; then
  DeployLinux="1"
  echo "Deploying version $AppVersionStrFull for Linux 64 bit.."
elif [ "$DeployTarget" == "linux32" ]; then
  DeployLinux32="1"
  echo "Deploying version $AppVersionStrFull for Linux 32 bit.."
else
  DeployMac="1"
  DeployWin="1"
  DeployLinux="1"
  if [ "$BetaVersion" == "0" ]; then
    DeployMac32="1"
    DeployLinux32="1"
    echo "Deploying five versions of $AppVersionStrFull: for Windows, OS X 10.6 and 10.7, OS X 10.8+, Linux 64 bit and Linux 32 bit.."
  else
    echo "Deploying three versions of $AppVersionStrFull: for Windows, OS X 10.8+ and Linux 64 bit.."
  fi
fi
if [ "$BuildTarget" == "mac" ]; then
  BackupPath="$HOME/Telegram/backup"
elif [ "$BuildTarget" == "linux" ]; then
  BackupPath="/media/psf/Home/Telegram/backup"
else
  Error "Can't deploy here"
fi
MacDeployPath="$BackupPath/$AppVersionStrMajor/$AppVersionStrFull/tmac"
MacUpdateFile="tmacupd$AppVersion"
MacSetupFile="tsetup.$AppVersionStrFull.dmg"
MacRemoteFolder="tmac"
Mac32DeployPath="$BackupPath/$AppVersionStrMajor/$AppVersionStrFull/tmac32"
Mac32UpdateFile="tmac32upd$AppVersion"
Mac32SetupFile="tsetup32.$AppVersionStrFull.dmg"
Mac32RemoteFolder="tmac32"
WinDeployPath="$BackupPath/$AppVersionStrMajor/$AppVersionStrFull/tsetup"
WinUpdateFile="tupdate$AppVersion"
WinSetupFile="tsetup.$AppVersionStrFull.exe"
WinPortableFile="tportable.$AppVersionStrFull.zip"
WinRemoteFolder="tsetup"
LinuxDeployPath="$BackupPath/$AppVersionStrMajor/$AppVersionStrFull/tlinux"
LinuxUpdateFile="tlinuxupd$AppVersion"
LinuxSetupFile="tsetup.$AppVersionStrFull.tar.xz"
LinuxRemoteFolder="tlinux"
Linux32DeployPath="$BackupPath/$AppVersionStrMajor/$AppVersionStrFull/tlinux32"
Linux32UpdateFile="tlinux32upd$AppVersion"
Linux32SetupFile="tsetup32.$AppVersionStrFull.tar.xz"
Linux32RemoteFolder="tlinux32"
DeployPath="$BackupPath/$AppVersionStrMajor/$AppVersionStrFull"

if [ "$BetaVersion" != "0" ]; then
  if [ "$DeployTarget" == "win" ]; then
    BetaFilePath="$WinDeployPath/$BetaKeyFile"
  elif [ "$DeployTarget" == "mac32" ]; then
    BetaFilePath="$Mac32DeployPath/$BetaKeyFile"
  elif [ "$DeployTarget" == "linux" ]; then
    BetaFilePath="$LinuxDeployPath/$BetaKeyFile"
  elif [ "$DeployTarget" == "linux32" ]; then
    BetaFilePath="$Linux32DeployPath/$BetaKeyFile"
  else
    BetaFilePath="$MacDeployPath/$BetaKeyFile"
  fi
  if [ ! -f "$BetaFilePath" ]; then
    Error "Beta key file for $AppVersionStrFull not found."
  fi

  while IFS='' read -r line || [[ -n "$line" ]]; do
    BetaSignature="$line"
  done < "$BetaFilePath"

  MacUpdateFile="${MacUpdateFile}_${BetaSignature}"
  MacSetupFile="tbeta${BetaVersion}_${BetaSignature}.zip"
  Mac32UpdateFile="${Mac32UpdateFile}_${BetaSignature}"
  Mac32SetupFile="tbeta${BetaVersion}_${BetaSignature}.zip"
  WinUpdateFile="${WinUpdateFile}_${BetaSignature}"
  WinPortableFile="tbeta${BetaVersion}_${BetaSignature}.zip"
  LinuxUpdateFile="${LinuxUpdateFile}_${BetaSignature}"
  LinuxSetupFile="tbeta${BetaVersion}_${BetaSignature}.tar.xz"
  Linux32UpdateFile="${Linux32UpdateFile}_${BetaSignature}"
  Linux32SetupFile="tbeta${BetaVersion}_${BetaSignature}.tar.xz"
fi

if [ "$DeployMac" == "1" ]; then
  if [ ! -f "$MacDeployPath/$MacUpdateFile" ]; then
    Error "$MacDeployPath/$MacUpdateFile not found!";
  fi
  if [ ! -f "$MacDeployPath/$MacSetupFile" ]; then
    Error "$MacDeployPath/$MacSetupFile not found!"
  fi
fi
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
if [ "$DeployLinux" == "1" ]; then
  if [ ! -f "$LinuxDeployPath/$LinuxUpdateFile" ]; then
    Error "$LinuxDeployPath/$LinuxUpdateFile not found!"
  fi
  if [ ! -f "$LinuxDeployPath/$LinuxSetupFile" ]; then
    Error "$LinuxDeployPath/$LinuxSetupFile not found!"
  fi
fi
if [ "$DeployLinux32" == "1" ]; then
  if [ ! -f "$Linux32DeployPath/$Linux32UpdateFile" ]; then
    Error "$Linux32DeployPath/$Linux32UpdateFile not found!"
  fi
  if [ ! -f "$Linux32DeployPath/$Linux32SetupFile" ]; then
    Error "$Linux32DeployPath/$Linux32SetupFile not found!"
  fi
fi

$FullScriptPath/../../../TelegramPrivate/mount.sh

declare -a Files
if [ "$DeployMac" == "1" ]; then
  Files+=("tmac/$MacUpdateFile" "tmac/$MacSetupFile")
fi
if [ "$DeployMac32" == "1" ]; then
  Files+=("tmac32/$Mac32UpdateFile" "tmac32/$Mac32SetupFile")
fi
if [ "$DeployWin" == "1" ]; then
  Files+=("tsetup/$WinUpdateFile" "tsetup/$WinPortableFile")
  if [ "$BetaVersion" == "0" ]; then
    Files+=("tsetup/$WinSetupFile")
  fi
fi
if [ "$DeployLinux" == "1" ]; then
  Files+=("tlinux/$LinuxUpdateFile" "tlinux/$LinuxSetupFile")
fi
if [ "$DeployLinux32" == "1" ]; then
  Files+=("tlinux32/$Linux32UpdateFile" "tlinux32/$Linux32SetupFile")
fi
cd $DeployPath
rsync -avR --progress ${Files[@]} "$FullScriptPath/../../../TelegramPrivate/remote/files"

echo "Version $AppVersionStrFull was deployed!"
cd $FullExecPath

