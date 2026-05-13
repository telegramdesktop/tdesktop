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

VersionForPacker="$AppVersion"
if [ "$AlphaVersion" != "0" ]; then
  AppVersion="$AlphaVersion"
  AppVersionStrFull="${AppVersionStr}_${AlphaVersion}"
  AlphaBetaParam="-alpha $AlphaVersion"
  AlphaKeyFile="talpha_${AppVersion}_key"
elif [ "$BetaChannel" == "0" ]; then
  AppVersionStrFull="$AppVersionStr"
  AlphaBetaParam=''
else
  AppVersionStrFull="$AppVersionStr.beta"
  AlphaBetaParam='-beta'
fi

echo ""
HomePath="$FullScriptPath/.."
if [ "$BuildTarget" != "macstore" ]; then
  Error "Invalid target!"
fi
if [ "$AlphaVersion" != "0" ]; then
  Error "Can't upload macstore alpha version!"
fi

echo "Uploading version $AppVersionStrFull to Mac App Store.."
ProjectPath="$HomePath/../out"
ReleasePath="$ProjectPath/Release"
BinaryName="Telegram Lite"
DeployPath="$ReleasePath/deploy/$AppVersionStrMajor/$AppVersionStrFull"
PackageFile="$DeployPath/$BinaryName.pkg"

set +e
xcrun altool --upload-app --username "$AC_USERNAME" --password "@keychain:AC_PASSWORD" -t macOS -f "$PackageFile"
set -e

echo -en "\007";
sleep 1;
echo -en "\007";
sleep 1;
echo -en "\007";
