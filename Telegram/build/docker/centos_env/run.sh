#set -e
#FullExecPath=$PWD
#pushd `dirname $0` > /dev/null
#FullScriptPath=`pwd`
#popd > /dev/null

#if [ ! -d "$FullScriptPath/../../../../../DesktopPrivate" ]; then
#  echo ""
#  echo "This script is for building the production version of Telegram Desktop."
#  echo ""h
#  echo "For building custom versions please visit the build instructions page at:"
#  echo "https://github.com/telegramdesktop/tdesktop/#build-instructions"
#  exit
#

#Command="$1"
#if [ "$Command" == "" ]; thenn
Command="scl enable llvm-toolset-7.0 -- scl enable devtoolset-11 -- bash"
#fi

docker run -it --rm --cpus=8 --memory=22g --net host --privileged --env DISPLAY=$DISPLAY \
                                                                                           --volume="$HOME/.Xauthority:/root/.Xauthority:rw"  \
                                                                                           -v /home/replikeit/Codes/borec/tdesktop:/usr/src/tdesktop tdesktop:centos_env $Command
