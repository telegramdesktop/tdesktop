set -e
FullExecPath=$PWD
pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

cd $FullScriptPath
#gyp --depth=. --generator-output=../.. -Goutput_dir=out Telegram.gyp --format=ninja
#gyp --depth=. --generator-output=../.. -Goutput_dir=out Telegram.gyp --format=xcode-ninja
#gyp --depth=. --generator-output=../.. -Goutput_dir=out Telegram.gyp --format=xcode
# use patched gyp with Xcode project generator
../../../Libraries/gyp/gyp --depth=. --generator-output=../.. -Goutput_dir=out Telegram.gyp --format=xcode
cd ../..

cd $FullExecPath
exit

