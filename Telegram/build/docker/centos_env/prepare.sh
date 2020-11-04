set -e
FullExecPath=$PWD
pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

docker build -t tdesktop:centos_env "$FullScriptPath/"
