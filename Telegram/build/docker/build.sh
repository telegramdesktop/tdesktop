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

Run () {
  scl enable devtoolset-9 -- "$@"
}

HomePath="$FullScriptPath/../.."
cd $HomePath

ProjectPath="$HomePath/../out"
ReleasePath="$ProjectPath/Release"
BinaryName="Telegram"

if [ ! -f "/usr/bin/cmake" ]; then
  ln -s cmake3 /usr/bin/cmake
fi

Run ./configure.sh

cd $ProjectPath
Run cmake --build . --config Release --target Telegram -- -j8
cd $ReleasePath

echo "$BinaryName build complete!"

if [ ! -f "$ReleasePath/$BinaryName" ]; then
Error "$BinaryName not found!"
fi

BadCount=`objdump -T $ReleasePath/$BinaryName | grep GLIBC_2\.1[8-9] | wc -l`
if [ "$BadCount" != "0" ]; then
  Error "Bad GLIBC usages found: $BadCount"
fi

BadCount=`objdump -T $ReleasePath/$BinaryName | grep GLIBC_2\.2[0-9] | wc -l`
if [ "$BadCount" != "0" ]; then
  Error "Bad GLIBC usages found: $BadCount"
fi

BadCount=`objdump -T $ReleasePath/$BinaryName | grep GCC_4\.[3-9] | wc -l`
if [ "$BadCount" != "0" ]; then
  Error "Bad GCC usages found: $BadCount"
fi

BadCount=`objdump -T $ReleasePath/$BinaryName | grep GCC_[5-9]\. | wc -l`
if [ "$BadCount" != "0" ]; then
  Error "Bad GCC usages found: $BadCount"
fi

if [ ! -f "$ReleasePath/Updater" ]; then
  Error "Updater not found!"
fi

BadCount=`objdump -T $ReleasePath/Updater | grep GLIBC_2\.1[8-9] | wc -l`
if [ "$BadCount" != "0" ]; then
  Error "Bad GLIBC usages found: $BadCount"
fi

BadCount=`objdump -T $ReleasePath/Updater | grep GLIBC_2\.2[0-9] | wc -l`
if [ "$BadCount" != "0" ]; then
  Error "Bad GLIBC usages found: $BadCount"
fi

BadCount=`objdump -T $ReleasePath/Updater | grep GCC_4\.[3-9] | wc -l`
if [ "$BadCount" != "0" ]; then
  Error "Bad GCC usages found: $BadCount"
fi

BadCount=`objdump -T $ReleasePath/Updater | grep GCC_[5-9]\. | wc -l`
if [ "$BadCount" != "0" ]; then
  Error "Bad GCC usages found: $BadCount"
fi

rm -rf "$ReleasePath/root"
mkdir "$ReleasePath/root"
mv "$ReleasePath/$BinaryName" "$ReleasePath/root/"
mv "$ReleasePath/Updater" "$ReleasePath/root/"
mv "$ReleasePath/Packer" "$ReleasePath/root/"
