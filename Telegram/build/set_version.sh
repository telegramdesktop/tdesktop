set -e
pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

Error () {
  echo "$1"
  exit 1
}

InputVersion="$1"

IFS='.' read -ra VersionParts <<< "$InputVersion"
VersionMajor="${VersionParts[0]}"
VersionMinor="${VersionParts[1]}"
VersionPatch="${VersionParts[2]}"
if [ "${VersionParts[3]}" == "" ]; then
  VersionBeta=0
  VersionAlpha=0
elif [ "${VersionParts[3]}" == "alpha" ]; then
  VersionBeta=0
  VersionAlpha=1
else
  VersionBeta="${VersionParts[3]}"
  VersionAlpha=0
fi

VersionMajorCleared=`echo "$VersionMajor % 1000" | bc`
if [ "$VersionMajorCleared" != "$VersionMajor" ]; then
  Error "Bad major version!"
fi
VersionMinorCleared=`echo "$VersionMinor % 1000" | bc`
if [ "$VersionMinorCleared" != "$VersionMinor" ]; then
  Error "Bad minor version!"
fi
VersionPatchCleared=`echo "$VersionPatch % 1000" | bc`
if [ "$VersionPatchCleared" != "$VersionPatch" ]; then
  Error "Bad patch version!"
fi
if [ "$VersionAlpha" != "0" ]; then
  if [ "$VersionAlpha" != "1" ]; then
    Error "Bad alpha version!"
  fi
  VersionAlphaBool=true
else
  VersionAlphaBool=false
fi

VersionFull=`echo "$VersionMajor * 1000000 + $VersionMinor * 1000 + $VersionPatch" | bc`
if [ "$VersionBeta" != "0" ]; then
  VersionBetaCleared=`echo "$VersionBeta % 1000" | bc`
  if [ "$VersionBetaCleared" != "$VersionBeta" ]; then
    Error "Bad beta version!"
  fi
  VersionBetaMul=`echo "$VersionBeta + 1000" | bc`
  VersionFullBeta="$VersionFull${VersionBetaMul:1}"
else
  VersionFullBeta=0
fi

VersionStr="$VersionMajor.$VersionMinor.$VersionPatch"
if [ "$VersionPatch" != "0" ]; then
  VersionStrSmall="$VersionStr"
else
  VersionStrSmall="$VersionMajor.$VersionMinor"
fi

if [ "$VersionAlpha" != "0" ]; then
  echo "Setting version: $VersionStr alpha"
elif [ "$VersionBeta" != "0" ]; then
  echo "Setting version: $VersionStr.$VersionBeta closed beta"
else
  echo "Setting version: $VersionStr stable"
fi

repl () {
  Pattern="$1"
  Replacement="$2"
  File="$3"
  CheckCommand="grep -sc '$Pattern' $File"
  set +e
  CheckCount=`eval $CheckCommand`
  set -e
  if [ "$CheckCount" -gt 0 ]; then
    ReplaceCommand="sed -i'.~' 's/$Pattern/$Replacement/g' $File"
    eval $ReplaceCommand
  else
    echo "Not found $Pattern"
    Error "While processing $File"
  fi
}

echo "Patching build/version..."
VersionFilePath="$FullScriptPath/version"
repl "\(AppVersion\) \([ ]*\)[0-9][0-9]*" "\1\2 $VersionFull" "$VersionFilePath"
repl "\(AppVersionStrMajor\) \([ ]*\)[0-9][0-9\.]*" "\1\2 $VersionMajor.$VersionMinor" "$VersionFilePath"
repl "\(AppVersionStrSmall\) \([ ]*\)[0-9][0-9\.]*" "\1\2 $VersionStrSmall" "$VersionFilePath"
repl "\(AppVersionStr\) \([ ]*\)[0-9][0-9\.]*" "\1\2 $VersionStr" "$VersionFilePath"
repl "\(AlphaChannel\) \([ ]*\)[0-9][0-9]*" "\1\2 $VersionAlpha" "$VersionFilePath"
repl "\(BetaVersion\) \([ ]*\)[0-9][0-9]*" "\1\2 $VersionFullBeta" "$VersionFilePath"

echo "Patching core/version.h..."
VersionHeaderPath="$FullScriptPath/../SourceFiles/core/version.h"
repl "\(BETA_VERSION_MACRO [ ]*\)([0-9][0-9]*ULL)" "\1(${VersionFullBeta}ULL)" "$VersionHeaderPath"
repl "\(AppVersion [ ]*=\) \([ ]*\)[0-9][0-9]*" "\1\2 $VersionFull" "$VersionHeaderPath"
repl "\(AppVersionStr [ ]*=\) \([ ]*\)[^;][^;]*" "\1\2 \"$VersionStrSmall\"" "$VersionHeaderPath"
repl "\(AppAlphaVersion [ ]*=\) \([ ]*\)[a-z][a-z]*" "\1\2 $VersionAlphaBool" "$VersionHeaderPath"

echo "Patching Telegram.rc..."
ResourcePath="$FullScriptPath/../Resources/winrc/Telegram.rc"
repl "\(FILEVERSION\) \([ ]*\)[0-9][0-9]*,[0-9][0-9]*,[0-9][0-9]*,[0-9][0-9]*" "\1\2 $VersionMajor,$VersionMinor,$VersionPatch,$VersionBeta" "$ResourcePath"
repl "\(PRODUCTVERSION\) \([ ]*\)[0-9][0-9]*,[0-9][0-9]*,[0-9][0-9]*,[0-9][0-9]*" "\1\2 $VersionMajor,$VersionMinor,$VersionPatch,$VersionBeta" "$ResourcePath"
repl "\(\"FileVersion\",\) \([ ]*\)\"[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\"" "\1\2 \"$VersionMajor.$VersionMinor.$VersionPatch.$VersionBeta\"" "$ResourcePath"
repl "\(\"ProductVersion\",\) \([ ]*\)\"[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\"" "\1\2 \"$VersionMajor.$VersionMinor.$VersionPatch.$VersionBeta\"" "$ResourcePath"

echo "Patching Updater.rc..."
ResourcePath="$FullScriptPath/../Resources/winrc/Updater.rc"
repl "\(FILEVERSION\) \([ ]*\)[0-9][0-9]*,[0-9][0-9]*,[0-9][0-9]*,[0-9][0-9]*" "\1\2 $VersionMajor,$VersionMinor,$VersionPatch,$VersionBeta" "$ResourcePath"
repl "\(PRODUCTVERSION\) \([ ]*\)[0-9][0-9]*,[0-9][0-9]*,[0-9][0-9]*,[0-9][0-9]*" "\1\2 $VersionMajor,$VersionMinor,$VersionPatch,$VersionBeta" "$ResourcePath"
repl "\(\"FileVersion\",\) \([ ]*\)\"[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\"" "\1\2 \"$VersionMajor.$VersionMinor.$VersionPatch.$VersionBeta\"" "$ResourcePath"
repl "\(\"ProductVersion\",\) \([ ]*\)\"[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\"" "\1\2 \"$VersionMajor.$VersionMinor.$VersionPatch.$VersionBeta\"" "$ResourcePath"

