set -e

pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

python3 $FullScriptPath/set_version.py $1

exit
