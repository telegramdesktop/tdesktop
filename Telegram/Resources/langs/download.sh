#!/usr/bin/env bash
set -e
FullExecPath=$PWD
pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

if [ ! -d "$FullScriptPath/../../../../TelegramPrivate" ]; then
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

cd $FullScriptPath/../../../../
while IFS='' read -r line || [[ -n "$line" ]]; do
  tx pull -f -l $line --minimum-perc=100
done < tdesktop/Telegram/Resources/langs/list
cd translations/telegram-desktop.langstrings/
for file in *.strings; do
  iconv -f "UTF-16LE" -t "UTF-8" "$file" > "../../tdesktop/Telegram/Resources/langs/lang_$file.tmp"
  awk '{ if (NR==1) sub(/^\xef\xbb\xbf/,""); sub(//,""); print }' "../../tdesktop/Telegram/Resources/langs/lang_$file.tmp" > "../../tdesktop/Telegram/Resources/langs/lang_$file"
  rm "../../tdesktop/Telegram/Resources/langs/lang_$file.tmp"
done
touch $FullScriptPath/../telegram.qrc

cd $FullExecPath
