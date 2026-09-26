#!/bin/bash
set -e

FullExecPath=$PWD
pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

"$FullScriptPath/run.sh" bash -lc "cd /usr/src/tdesktop && cmake --build out --config Debug --target Telegram"
cd "$FullExecPath"
