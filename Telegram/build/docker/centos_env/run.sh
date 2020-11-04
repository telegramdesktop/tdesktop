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

Error () {
  cd $FullExecPath
  echo "$1"
  exit 1
}

if [ "$1" == "" ]; then
  Error "Script not found!"
fi

docker run -it --rm --cpus=8 --memory=10g -v $HOME/Telegram/DesktopPrivate:/usr/src/DesktopPrivate -v $HOME/Telegram/tdesktop:/usr/src/tdesktop tdesktop:centos_env $1
