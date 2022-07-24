set -e
FullExecPath=$PWD
pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null


cd $FullScriptPath/../docker/centos_env
poetry run gen_dockerfile | docker build -t tdesktop:centos_env -
cd $FullExecPath
