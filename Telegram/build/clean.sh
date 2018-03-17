set -e
FullExecPath=$PWD
pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

while IFS='' read -r line || [[ -n "$line" ]]; do
	BuildTarget="$line"
done < "$FullScriptPath/target"

while IFS='' read -r line || [[ -n "$line" ]]; do
	set $line
	eval $1="$2"
done < "$FullScriptPath/version"

if [ "$BetaVersion" != "0" ]; then
  AppVersionStrFull="${AppVersionStr}_${BetaVersion}"
elif [ "$AlphaChannel" == "0" ]; then
  AppVersionStrFull="$AppVersionStr"
else
  AppVersionStrFull="$AppVersionStr.alpha"
fi

HomePath="$FullScriptPath/.."
if [ "$BuildTarget" == "linux" ]; then
  ReleasePath="$HomePath/../out/Release"
elif [ "$BuildTarget" == "linux32" ]; then
  ReleasePath="$HomePath/../out/Release"
elif [ "$BuildTarget" == "mac" ]; then
  ReleasePath="$HomePath/../out/Release"
elif [ "$BuildTarget" == "mac32" ]; then
  ReleasePath="$HomePath/../out/Release"
elif [ "$BuildTarget" == "macstore" ]; then
  ReleasePath="$HomePath/../out/Release"
fi

DeployPath="$ReleasePath/deploy/$AppVersionStrMajor/$AppVersionStrFull"


if [ ! -d "$DeployPath" ]; then
	echo "Depoly Path Not Exists!"
	exit
fi

echo Remove $DeployPath in 3 second
sleep 1
echo 2 second
sleep 1
echo 1 second
sleep 1
rm -r $DeployPath
echo Done
