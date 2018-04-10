set -e
FullExecPath=$PWD
pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

Param1="$1"
Param2="$2"
Param3="$3"
Param4="$4"

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

while IFS='' read -r line || [[ -n "$line" ]]; do
  set $line
  eval $1="$2"
done < "$FullScriptPath/version"

VersionForPacker="$AppVersion"
if [ "$BetaVersion" != "0" ]; then
  Error "No releases for closed beta versions"
elif [ "$AlphaChannel" == "0" ]; then
  AppVersionStrFull="$AppVersionStr"
  AlphaBetaParam=''
else
  AppVersionStrFull="$AppVersionStr.alpha"
  AlphaBetaParam='-alpha'
fi

cd "$FullScriptPath"
python3 release.py $AppVersionStr $Param1 $Param2 $Param3 $Param4
