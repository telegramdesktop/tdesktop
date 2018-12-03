set -e
FullExecPath=$PWD
pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

if [ ! -d "$FullScriptPath/../../../TelegramPrivate" ]; then
  echo ""
  echo "This script is for building the production version of Telegram Desktop."
  echo ""
  echo "For building custom versions please visit the build instructions page at:"
  echo "https://github.com/telegramdesktop/tdesktop/#build-instructions"
  exit
fi

pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

python $FullScriptPath/updates.py $1 $2 $3 $4 $5 $6

exit
