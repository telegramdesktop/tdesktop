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

if [ "$#" == "0" ]; then
  set -- bash
fi

CpuCount=$(nproc)
DockerCpus=$((CpuCount > 4 ? CpuCount - 2 : CpuCount))
MemTotalGb=$(($(grep MemTotal /proc/meminfo | awk '{print $2}') / 1048576))
DockerMemoryGb=$((MemTotalGb > 12 ? MemTotalGb - 4 : MemTotalGb))

docker run -it --rm --cpus=$DockerCpus --memory=${DockerMemoryGb}g -u $(id -u) -v $HOME/Telegram/DesktopPrivate:/usr/src/DesktopPrivate -v $HOME/Telegram/tdesktop:/usr/src/tdesktop tdesktop:centos_env "$@"
