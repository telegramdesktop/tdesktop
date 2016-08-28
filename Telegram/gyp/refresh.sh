set -e
FullExecPath=$PWD
pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

cd $FullScriptPath
gyp --depth=. --generator-output=../.. -Goutput_dir=out Telegram.gyp --format=xcode
cd ../..

cd $FullExecPath
exit

