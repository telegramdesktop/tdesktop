#!/bin/bash

Run () {
  scl enable devtoolset-8 -- "$@"
}

HomePath=/usr/src/tdesktop/Telegram
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

# BadCount=`objdump -T $ReleasePath/$BinaryName | grep GLIBC_2\.1[6-9] | wc -l`
# if [ "$BadCount" != "0" ]; then
#   Error "Bad GLIBC usages found: $BadCount"
# fi

# BadCount=`objdump -T $ReleasePath/$BinaryName | grep GLIBC_2\.2[0-9] | wc -l`
# if [ "$BadCount" != "0" ]; then
#   Error "Bad GLIBC usages found: $BadCount"
# fi

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

BadCount=`objdump -T $ReleasePath/Updater | grep GLIBC_2\.1[6-9] | wc -l`
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

echo "Dumping debug symbols.."
/dump_syms "$ReleasePath/$BinaryName" > "$ReleasePath/$BinaryName.sym"
echo "Done!"

echo "Stripping the executable.."
strip -s "$ReleasePath/$BinaryName"
echo "Done!"

rm -rf "$ReleasePath/root"
mkdir "$ReleasePath/root"
mv "$ReleasePath/$BinaryName" "$ReleasePath/root/"
mv "$ReleasePath/$BinaryName.sym" "$ReleasePath/root/"
mv "$ReleasePath/Updater" "$ReleasePath/root/"
mv "$ReleasePath/Packer" "$ReleasePath/root/"
