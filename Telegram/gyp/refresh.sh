#!/usr/bin/env bash
set -e

pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

python $FullScriptPath/generate.py $1 $2 $3 $4 $5 $6

exit
