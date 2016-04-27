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
  exit 1
fi

Error () {
  cd $FullExecPath
  echo "$1"
  exit 1
}

if [ ! -f "$FullScriptPath/target" ]; then
  Error "Build target not found."
fi

while IFS='' read -r line || [[ -n "$line" ]]; do
  BuildTarget="$line"
done < "$FullScriptPath/target"

LocalDirPath="\/usr\/local\/lib"
if [ "$BuildTarget" == "linux" ]; then
  ArchDirPath="\/usr\/lib\/x86_64\-linux\-gnu"
elif [ "$BuildTarget" == "linux32" ]; then
  ArchDirPath="\/usr\/lib\/i386\-linux\-gnu"
else
  Error "Bad build target."
fi

Replace () {
    CheckCommand="grep -ci '$1' Makefile"
    CheckCount=$(eval $CheckCommand)
    if [ "$CheckCount" -gt 0 ]; then
        echo "Requested '$1' to '$2', found - replacing.."
        ReplaceCommand="sed -i 's/$1/$2/g' Makefile"
        eval $ReplaceCommand
    else
        echo "Skipping '$1' to '$2'"
    fi
}

Replace '\-llzma' "$ArchDirPath\/liblzma\.a"
Replace '\-lXi' "$ArchDirPath\/libXi\.a $ArchDirPath\/libXext\.a"
Replace '\-lSM' "$ArchDirPath\/libSM\.a"
Replace '\-lICE' "$ArchDirPath\/libICE\.a"
Replace '\-lfontconfig' "$ArchDirPath\/libfontconfig\.a $ArchDirPath\/libexpat\.a"
Replace '\-lfreetype' "$ArchDirPath\/libfreetype\.a"
Replace '\-lXext' "$ArchDirPath\/libXext\.a"
Replace '\-lopus' "$LocalDirPath\/libopus\.a"
Replace '\-lopenal' "$LocalDirPath\/libopenal\.a"
Replace '\-lavformat' "$LocalDirPath\/libavformat\.a"
Replace '\-lavcodec' "$LocalDirPath\/libavcodec\.a"
Replace '\-lswresample' "$LocalDirPath\/libswresample\.a"
Replace '\-lswscale' "$LocalDirPath\/libswscale\.a"
Replace '\-lavutil' "$LocalDirPath\/libavutil\.a"
Replace '\-lva' "$LocalDirPath\/libva\.a"
