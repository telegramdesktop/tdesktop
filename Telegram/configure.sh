#!/usr/bin/env bash
set -e

pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

python3 $FullScriptPath/configure.py "$@"

exit
