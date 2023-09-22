#!/bin/bash

set -e
FullExecPath=$PWD
pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

if [ ! -d "$FullScriptPath/../../../../DesktopPrivate" ]; then
  echo ""
  echo "This script is for building the production version of Telegram Desktop."
  echo ""
  echo "For building custom versions please visit the build instructions page at:"
  echo "https://github.com/telegramdesktop/tdesktop/#build-instructions"
  exit
fi

HomePath="$FullScriptPath/../.."
cd $HomePath

ProjectPath="$HomePath/../out"
ReleasePath="$ProjectPath/Release"
BinaryName="Telegram"

if [ ! -f "/usr/bin/cmake" ]; then
  ln -s cmake3 /usr/bin/cmake
fi

./configure.sh

cd $ProjectPath
cmake --build . --config Release --target Telegram
cd $ReleasePath

echo "$BinaryName build complete!"

Error () {
  cd $FullExecPath
  echo "$1"
  exit 1
}

if [ ! -f "$ReleasePath/$BinaryName" ]; then
  Error "$BinaryName not found!"
fi

if [ ! -f "$ReleasePath/Updater" ]; then
  Error "Updater not found!"
fi

rm -rf "$ReleasePath/root"
mkdir "$ReleasePath/root"
mv "$ReleasePath/$BinaryName" "$ReleasePath/root/"
mv "$ReleasePath/Updater" "$ReleasePath/root/"
mv "$ReleasePath/Packer" "$ReleasePath/root/"
