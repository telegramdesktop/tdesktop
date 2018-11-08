set -e

pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

python $FullScriptPath/set_version.py $1

exit
