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
  VersionAlpha=0
  VersionBeta=0
elif [ "${VersionParts[3]}" == "beta" ]; then
  VersionAlpha=0
  VersionBeta=1
else
  VersionAlpha="${VersionParts[3]}"
  VersionBeta=0
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
if [ "$VersionBeta" != "0" ]; then
  if [ "$VersionBeta" != "1" ]; then
    Error "Bad beta version!"
  fi
  VersionBetaBool=true
else
  VersionBetaBool=false
fi

VersionFull=`echo "$VersionMajor * 1000000 + $VersionMinor * 1000 + $VersionPatch" | bc`
if [ "$VersionAlpha" != "0" ]; then
  VersionAlphaCleared=`echo "$VersionAlpha % 1000" | bc`
  if [ "$VersionAlphaCleared" != "$VersionAlpha" ]; then
    Error "Bad alpha version!"
  fi
  VersionAlphaMul=`echo "$VersionAlpha + 1000" | bc`
  VersionFullAlpha="$VersionFull${VersionAlphaMul:1}"
else
  VersionFullAlpha=0
fi

VersionStr="$VersionMajor.$VersionMinor.$VersionPatch"
if [ "$VersionPatch" != "0" ]; then
  VersionStrSmall="$VersionStr"
else
  VersionStrSmall="$VersionMajor.$VersionMinor"
fi

if [ "$VersionBeta" != "0" ]; then
  echo "Setting version: $VersionStr beta"
elif [ "$VersionAlpha" != "0" ]; then
  echo "Setting version: $VersionStr.$VersionAlpha closed alpha"
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

echo "Checking changelog..."
ChangelogFile="$FullScriptPath/../../changelog.txt"
ChangelogCommand="grep -sc '^$VersionStr ' $ChangelogFile"
set +e
FoundCount=`eval $ChangelogCommand`
set -e
if [ "$FoundCount" == "0" ]; then
  ChangelogCommand="grep -sc '^$VersionStrSmall ' $ChangelogFile"
  set +e
  FoundCount=`eval $ChangelogCommand`
  set -e
  if [ "$FoundCount" == "0" ]; then
    Error "Changelog entry not found!"
  elif [ "$FoundCount" != "1" ]; then
    Error "Wrong changelog entries count found: $FoundCount"
  fi
elif [ "$FoundCount" != "1" ]; then
  Error "Wrong changelog entries count found: $FoundCount"
fi

echo "Patching build/version..."
VersionFilePath="$FullScriptPath/version"
repl "\(AppVersion\) \([ ]*\)[0-9][0-9]*" "\1\2 $VersionFull" "$VersionFilePath"
repl "\(AppVersionStrMajor\) \([ ]*\)[0-9][0-9\.]*" "\1\2 $VersionMajor.$VersionMinor" "$VersionFilePath"
repl "\(AppVersionStrSmall\) \([ ]*\)[0-9][0-9\.]*" "\1\2 $VersionStrSmall" "$VersionFilePath"
repl "\(AppVersionStr\) \([ ]*\)[0-9][0-9\.]*" "\1\2 $VersionStr" "$VersionFilePath"
repl "\(BetaChannel\) \([ ]*\)[0-9][0-9]*" "\1\2 $VersionBeta" "$VersionFilePath"
repl "\(AlphaVersion\) \([ ]*\)[0-9][0-9]*" "\1\2 $VersionFullAlpha" "$VersionFilePath"

echo "Patching core/version.h..."
VersionHeaderPath="$FullScriptPath/../SourceFiles/core/version.h"
repl "\(ALPHA_VERSION_MACRO [ ]*\)([0-9][0-9]*ULL)" "\1(${VersionFullAlpha}ULL)" "$VersionHeaderPath"
repl "\(AppVersion [ ]*=\) \([ ]*\)[0-9][0-9]*" "\1\2 $VersionFull" "$VersionHeaderPath"
repl "\(AppVersionStr [ ]*=\) \([ ]*\)[^;][^;]*" "\1\2 \"$VersionStrSmall\"" "$VersionHeaderPath"
repl "\(AppBetaVersion [ ]*=\) \([ ]*\)[a-z][a-z]*" "\1\2 $VersionBetaBool" "$VersionHeaderPath"

echo "Patching Telegram.rc..."
ResourcePath="$FullScriptPath/../Resources/winrc/Telegram.rc"
repl "\(FILEVERSION\) \([ ]*\)[0-9][0-9]*,[0-9][0-9]*,[0-9][0-9]*,[0-9][0-9]*" "\1\2 $VersionMajor,$VersionMinor,$VersionPatch,$VersionAlpha" "$ResourcePath"
repl "\(PRODUCTVERSION\) \([ ]*\)[0-9][0-9]*,[0-9][0-9]*,[0-9][0-9]*,[0-9][0-9]*" "\1\2 $VersionMajor,$VersionMinor,$VersionPatch,$VersionAlpha" "$ResourcePath"
repl "\(\"FileVersion\",\) \([ ]*\)\"[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\"" "\1\2 \"$VersionMajor.$VersionMinor.$VersionPatch.$VersionAlpha\"" "$ResourcePath"
repl "\(\"ProductVersion\",\) \([ ]*\)\"[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\"" "\1\2 \"$VersionMajor.$VersionMinor.$VersionPatch.$VersionAlpha\"" "$ResourcePath"

echo "Patching Updater.rc..."
ResourcePath="$FullScriptPath/../Resources/winrc/Updater.rc"
repl "\(FILEVERSION\) \([ ]*\)[0-9][0-9]*,[0-9][0-9]*,[0-9][0-9]*,[0-9][0-9]*" "\1\2 $VersionMajor,$VersionMinor,$VersionPatch,$VersionAlpha" "$ResourcePath"
repl "\(PRODUCTVERSION\) \([ ]*\)[0-9][0-9]*,[0-9][0-9]*,[0-9][0-9]*,[0-9][0-9]*" "\1\2 $VersionMajor,$VersionMinor,$VersionPatch,$VersionAlpha" "$ResourcePath"
repl "\(\"FileVersion\",\) \([ ]*\)\"[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\"" "\1\2 \"$VersionMajor.$VersionMinor.$VersionPatch.$VersionAlpha\"" "$ResourcePath"
repl "\(\"ProductVersion\",\) \([ ]*\)\"[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\"" "\1\2 \"$VersionMajor.$VersionMinor.$VersionPatch.$VersionAlpha\"" "$ResourcePath"

echo "Patching appxmanifest.xml..."
ResourcePath="$FullScriptPath/../Resources/uwp/AppX/AppxManifest.xml"
repl " \(Version=\)\"[0-9][0-9]*.[0-9][0-9]*.[0-9][0-9]*.[0-9][0-9]*\"" " \1\"$VersionMajor.$VersionMinor.$VersionPatch.$VersionAlpha\"" "$ResourcePath"
