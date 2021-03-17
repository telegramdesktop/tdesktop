set -e
FullExecPath=$PWD
pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

if [ ! -d "$FullScriptPath/../../../../../DesktopPrivate" ]; then
  echo ""
  echo "This script is for building the production version of Telegram Desktop."
  echo ""
  echo "For building custom versions please visit the build instructions page at:"
  echo "https://github.com/telegramdesktop/tdesktop/#build-instructions"
  exit
fi

Command="$1"
if [ "$Command" == "" ]; then
  Command="scl enable devtoolset-9 -- bash"
fi

docker run -it --rm --cpus=8 --memory=22g -v $HOME/Telegram/DesktopPrivate:/usr/src/DesktopPrivate -v $HOME/Telegram/tdesktop:/usr/src/tdesktop tdesktop:centos_env $Command
