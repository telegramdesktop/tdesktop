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

if [ "$AppVersion" -lt 7002000 ]; then
  Error "The v2 update format requires version 7.2 or newer."
fi
if [ "$AlphaVersion" != "0" ]; then
  Error "The v2 update format has no alpha channel."
fi
case "$AppVersionStr" in
  *.*.*) ;;
  *) Error "AppVersionStr '$AppVersionStr' must have three components for the v2 names." ;;
esac

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
DeployWinArm="0"
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
elif [ "$DeployTarget" == "winarm" ]; then
  DeployWinArm="1"
  echo "Deploying version $AppVersionStrFull for Windows on ARM.."
elif [ "$DeployTarget" == "linux" ]; then
  DeployLinux="1"
  echo "Deploying version $AppVersionStrFull for Linux 64 bit.."
else
  DeployMac="1"
  DeployWin="1"
  DeployWin64="1"
  DeployWinArm="1"
  DeployLinux="1"
  echo "Deploying five versions of $AppVersionStrFull: for Windows 32 bit / 64 bit / on ARM, macOS and Linux 64 bit.."
fi
if [ "$BuildTarget" == "mac" ]; then
  BackupPath="$HOME/Projects/backup/tdesktop"
elif [ "$BuildTarget" == "linux" ]; then
  BackupPath="/media/psf/Home/Projects/backup/tdesktop"
  if [ ! -d "$BackupPath" ]; then
    BackupPath="/mnt/c/Telegram/Projects/backup/tdesktop"
  fi
else
  Error "Can't deploy here"
fi
DeployPath="$BackupPath/$AppVersionStrMajor/$AppVersionStrFull"

ArtifactSuffix=""
if [ "$BetaChannel" != "0" ]; then
  ArtifactSuffix="-beta"
fi
MacUpdateFile="td-update-mac-x64-$AppVersion$ArtifactSuffix"
ARMacUpdateFile="td-update-mac-arm-$AppVersion$ArtifactSuffix"
MacSetupFile="td-setup-mac-$AppVersionStr$ArtifactSuffix.dmg"
WinUpdateFile="td-update-win-x86-$AppVersion$ArtifactSuffix"
WinSetupFile="td-setup-win-x86-$AppVersionStr$ArtifactSuffix.exe"
WinPortableFile="td-portable-win-x86-$AppVersionStr$ArtifactSuffix.zip"
Win64UpdateFile="td-update-win-x64-$AppVersion$ArtifactSuffix"
Win64SetupFile="td-setup-win-x64-$AppVersionStr$ArtifactSuffix.exe"
Win64PortableFile="td-portable-win-x64-$AppVersionStr$ArtifactSuffix.zip"
WinArmUpdateFile="td-update-win-arm-$AppVersion$ArtifactSuffix"
WinArmSetupFile="td-setup-win-arm-$AppVersionStr$ArtifactSuffix.exe"
WinArmPortableFile="td-portable-win-arm-$AppVersionStr$ArtifactSuffix.zip"
LinuxUpdateFile="td-update-linux-x64-$AppVersion$ArtifactSuffix"
LinuxSetupFile="td-setup-linux-x64-$AppVersionStr$ArtifactSuffix.tar.xz"

MacRemoteFolder="mac"
WinRemoteFolder="win-x86"
Win64RemoteFolder="win-x64"
WinArmRemoteFolder="win-arm"
LinuxRemoteFolder="linux-x64"

MacDeployPath="$DeployPath/$MacRemoteFolder"
WinDeployPath="$DeployPath/$WinRemoteFolder"
Win64DeployPath="$DeployPath/$Win64RemoteFolder"
WinArmDeployPath="$DeployPath/$WinArmRemoteFolder"
LinuxDeployPath="$DeployPath/$LinuxRemoteFolder"

if [ "$AlphaVersion" != "0" ]; then
  if [ "$DeployTarget" == "win" ]; then
    AlphaFilePath="$WinDeployPath/$AlphaKeyFile"
  elif [ "$DeployTarget" == "win64" ]; then
    AlphaFilePath="$Win64DeployPath/$AlphaKeyFile"
  elif [ "$DeployTarget" == "winarm" ]; then
    AlphaFilePath="$WinArmDeployPath/$AlphaKeyFile"
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
  WinArmUpdateFile="${WinArmUpdateFile}_${AlphaSignature}"
  WinArmPortableFile="talpha${AlphaVersion}_${AlphaSignature}.zip"
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
if [ "$DeployWinArm" == "1" ]; then
  if [ ! -f "$WinArmDeployPath/$WinArmUpdateFile" ]; then
    Error "$WinArmUpdateFile not found!"
  fi
  if [ "$AlphaVersion" == "0" ]; then
    if [ ! -f "$WinArmDeployPath/$WinArmSetupFile" ]; then
      Error "$WinArmSetupFile not found!"
    fi
  fi
  if [ ! -f "$WinArmDeployPath/$WinArmPortableFile" ]; then
    Error "$WinArmPortableFile not found!"
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
  Files+=("$MacRemoteFolder/$MacUpdateFile" "$MacRemoteFolder/$ARMacUpdateFile" "$MacRemoteFolder/$MacSetupFile")
fi
if [ "$DeployWin" == "1" ]; then
  Files+=("$WinRemoteFolder/$WinUpdateFile" "$WinRemoteFolder/$WinPortableFile")
  if [ "$AlphaVersion" == "0" ]; then
    Files+=("$WinRemoteFolder/$WinSetupFile")
  fi
fi
if [ "$DeployWin64" == "1" ]; then
  Files+=("$Win64RemoteFolder/$Win64UpdateFile" "$Win64RemoteFolder/$Win64PortableFile")
  if [ "$AlphaVersion" == "0" ]; then
    Files+=("$Win64RemoteFolder/$Win64SetupFile")
  fi
fi
if [ "$DeployWinArm" == "1" ]; then
  Files+=("$WinArmRemoteFolder/$WinArmUpdateFile" "$WinArmRemoteFolder/$WinArmPortableFile")
  if [ "$AlphaVersion" == "0" ]; then
    Files+=("$WinArmRemoteFolder/$WinArmSetupFile")
  fi
fi
if [ "$DeployLinux" == "1" ]; then
  Files+=("$LinuxRemoteFolder/$LinuxUpdateFile" "$LinuxRemoteFolder/$LinuxSetupFile")
fi
cd $DeployPath
rsync -avR --no-g --progress ${Files[@]} "$FullScriptPath/../../../DesktopPrivate/remote/files"

echo "Version $AppVersionStrFull was deployed!"
cd $FullExecPath

