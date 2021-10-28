set -e
FullExecPath=$PWD
pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

if [ ! -d "$FullScriptPath/../../../DesktopPrivate" ]; then
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

if [ "$AlphaVersion" != "0" ]; then
  AppVersion="$AlphaVersion"
  AppVersionStrFull="${AppVersionStr}_${AlphaVersion}"
  AlphaKeyFile="talpha_${AppVersion}_key"
elif [ "$BetaChannel" == "0" ]; then
  AppVersionStrFull="$AppVersionStr"
else
  AppVersionStrFull="$AppVersionStr.beta"
fi

echo ""
HomePath="$FullScriptPath/.."
DeployMac="0"
DeployWin="0"
DeployWin64="0"
DeployLinux="0"
if [ "$DeployTarget" == "mac" ]; then
  DeployMac="1"
  echo "Deploying version $AppVersionStrFull for macOS.."
elif [ "$DeployTarget" == "win" ]; then
  DeployWin="1"
  echo "Deploying version $AppVersionStrFull for Windows 32 bit.."
elif [ "$DeployTarget" == "win64" ]; then
  DeployWin64="1"
  echo "Deploying version $AppVersionStrFull for Windows 64 bit.."
elif [ "$DeployTarget" == "linux" ]; then
  DeployLinux="1"
  echo "Deploying version $AppVersionStrFull for Linux 64 bit.."
else
  DeployMac="1"
  DeployWin="1"
  DeployWin64="1"
  DeployLinux="1"
  echo "Deploying four versions of $AppVersionStrFull: for Windows 32 bit, Windows 64 bit, macOS and Linux 64 bit.."
fi
if [ "$BuildTarget" == "mac" ]; then
  BackupPath="$HOME/Projects/backup/tdesktop"
elif [ "$BuildTarget" == "linux" ]; then
  BackupPath="/media/psf/Home/Projects/backup/tdesktop"
else
  Error "Can't deploy here"
fi
MacDeployPath="$BackupPath/$AppVersionStrMajor/$AppVersionStrFull/tmac"
MacUpdateFile="tmacupd$AppVersion"
ARMacUpdateFile="tarmacupd$AppVersion"
MacSetupFile="tsetup.$AppVersionStrFull.dmg"
MacRemoteFolder="tmac"
WinDeployPath="$BackupPath/$AppVersionStrMajor/$AppVersionStrFull/tsetup"
WinUpdateFile="tupdate$AppVersion"
WinSetupFile="tsetup.$AppVersionStrFull.exe"
WinPortableFile="tportable.$AppVersionStrFull.zip"
WinRemoteFolder="tsetup"
Win64DeployPath="$BackupPath/$AppVersionStrMajor/$AppVersionStrFull/tx64"
Win64UpdateFile="tx64upd$AppVersion"
Win64SetupFile="tsetup-x64.$AppVersionStrFull.exe"
Win64PortableFile="tportable-x64.$AppVersionStrFull.zip"
Win64RemoteFolder="tx64"
LinuxDeployPath="$BackupPath/$AppVersionStrMajor/$AppVersionStrFull/tlinux"
LinuxUpdateFile="tlinuxupd$AppVersion"
LinuxSetupFile="tsetup.$AppVersionStrFull.tar.xz"
LinuxRemoteFolder="tlinux"
DeployPath="$BackupPath/$AppVersionStrMajor/$AppVersionStrFull"

if [ "$AlphaVersion" != "0" ]; then
  if [ "$DeployTarget" == "win" ]; then
    AlphaFilePath="$WinDeployPath/$AlphaKeyFile"
  elif [ "$DeployTarget" == "win64" ]; then
    AlphaFilePath="$Win64DeployPath/$AlphaKeyFile"
  elif [ "$DeployTarget" == "linux" ]; then
    AlphaFilePath="$LinuxDeployPath/$AlphaKeyFile"
  else
    AlphaFilePath="$MacDeployPath/$AlphaKeyFile"
  fi
  if [ ! -f "$AlphaFilePath" ]; then
    Error "Alpha key file for $AppVersionStrFull not found."
  fi

  while IFS='' read -r line || [[ -n "$line" ]]; do
    AlphaSignature="$line"
  done < "$AlphaFilePath"

  MacUpdateFile="${MacUpdateFile}_${AlphaSignature}"
  ARMacUpdateFile="${ARMacUpdateFile}_${AlphaSignature}"
  MacSetupFile="talpha${AlphaVersion}_${AlphaSignature}.zip"
  WinUpdateFile="${WinUpdateFile}_${AlphaSignature}"
  WinPortableFile="talpha${AlphaVersion}_${AlphaSignature}.zip"
  Win64UpdateFile="${Win64UpdateFile}_${AlphaSignature}"
  Win64PortableFile="talpha${AlphaVersion}_${AlphaSignature}.zip"
  LinuxUpdateFile="${LinuxUpdateFile}_${AlphaSignature}"
  LinuxSetupFile="talpha${AlphaVersion}_${AlphaSignature}.tar.xz"
fi

if [ "$DeployMac" == "1" ]; then
  if [ ! -f "$MacDeployPath/$MacUpdateFile" ]; then
    Error "$MacDeployPath/$MacUpdateFile not found!";
  fi
  if [ ! -f "$MacDeployPath/$ARMacUpdateFile" ]; then
    Error "$MacDeployPath/$ARMacUpdateFile not found!";
  fi
  if [ ! -f "$MacDeployPath/$MacSetupFile" ]; then
    Error "$MacDeployPath/$MacSetupFile not found!"
  fi
fi
if [ "$DeployWin" == "1" ]; then
  if [ ! -f "$WinDeployPath/$WinUpdateFile" ]; then
    Error "$WinUpdateFile not found!"
  fi
  if [ "$AlphaVersion" == "0" ]; then
    if [ ! -f "$WinDeployPath/$WinSetupFile" ]; then
      Error "$WinSetupFile not found!"
    fi
  fi
  if [ ! -f "$WinDeployPath/$WinPortableFile" ]; then
    Error "$WinPortableFile not found!"
  fi
fi
if [ "$DeployWin64" == "1" ]; then
  if [ ! -f "$Win64DeployPath/$Win64UpdateFile" ]; then
    Error "$Win64UpdateFile not found!"
  fi
  if [ "$AlphaVersion" == "0" ]; then
    if [ ! -f "$Win64DeployPath/$Win64SetupFile" ]; then
      Error "$Win64SetupFile not found!"
    fi
  fi
  if [ ! -f "$Win64DeployPath/$Win64PortableFile" ]; then
    Error "$Win64PortableFile not found!"
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

$FullScriptPath/../../../DesktopPrivate/mount.sh

declare -a Files
if [ "$DeployMac" == "1" ]; then
  Files+=("tmac/$MacUpdateFile" "tmac/$ARMacUpdateFile" "tmac/$MacSetupFile")
fi
if [ "$DeployWin" == "1" ]; then
  Files+=("tsetup/$WinUpdateFile" "tsetup/$WinPortableFile")
  if [ "$AlphaVersion" == "0" ]; then
    Files+=("tsetup/$WinSetupFile")
  fi
fi
if [ "$DeployWin64" == "1" ]; then
  Files+=("tx64/$Win64UpdateFile" "tx64/$Win64PortableFile")
  if [ "$AlphaVersion" == "0" ]; then
    Files+=("tx64/$Win64SetupFile")
  fi
fi
if [ "$DeployLinux" == "1" ]; then
  Files+=("tlinux/$LinuxUpdateFile" "tlinux/$LinuxSetupFile")
fi
cd $DeployPath
rsync -avR --progress ${Files[@]} "$FullScriptPath/../../../DesktopPrivate/remote/files"

echo "Version $AppVersionStrFull was deployed!"
cd $FullExecPath

