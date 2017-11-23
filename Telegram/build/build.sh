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

Error () {
  cd $FullExecPath
  echo "$1"
  exit 1
}

if [ ! -f "$FullScriptPath/target" ]; then
  Error "Build target not found!"
fi

while IFS='' read -r line || [[ -n "$line" ]]; do
  BuildTarget="$line"
done < "$FullScriptPath/target"

while IFS='' read -r line || [[ -n "$line" ]]; do
  set $line
  eval $1="$2"
done < "$FullScriptPath/version"

VersionForPacker="$AppVersion"
if [ "$BetaVersion" != "0" ]; then
  AppVersion="$BetaVersion"
  AppVersionStrFull="${AppVersionStr}_${BetaVersion}"
  AlphaBetaParam="-beta $BetaVersion"
  BetaKeyFile="tbeta_${AppVersion}_key"
elif [ "$AlphaChannel" == "0" ]; then
  AppVersionStrFull="$AppVersionStr"
  AlphaBetaParam=''
else
  AppVersionStrFull="$AppVersionStr.alpha"
  AlphaBetaParam='-alpha'
fi

echo ""
HomePath="$FullScriptPath/.."
if [ "$BuildTarget" == "linux" ]; then
  echo "Building version $AppVersionStrFull for Linux 64bit.."
  UpdateFile="tlinuxupd$AppVersion"
  SetupFile="tsetup.$AppVersionStrFull.tar.xz"
  ReleasePath="$HomePath/../out/Release"
  BinaryName="Telegram"
elif [ "$BuildTarget" == "linux32" ]; then
  echo "Building version $AppVersionStrFull for Linux 32bit.."
  UpdateFile="tlinux32upd$AppVersion"
  SetupFile="tsetup32.$AppVersionStrFull.tar.xz"
  ReleasePath="$HomePath/../out/Release"
  BinaryName="Telegram"
elif [ "$BuildTarget" == "mac" ]; then
  echo "Building version $AppVersionStrFull for OS X 10.8+.."
  UpdateFile="tmacupd$AppVersion"
  SetupFile="tsetup.$AppVersionStrFull.dmg"
  ReleasePath="$HomePath/../out/Release"
  BinaryName="Telegram"
elif [ "$BuildTarget" == "mac32" ]; then
  echo "Building version $AppVersionStrFull for OS X 10.6 and 10.7.."
  UpdateFile="tmac32upd$AppVersion"
  SetupFile="tsetup32.$AppVersionStrFull.dmg"
  ReleasePath="$HomePath/../out/Release"
  BinaryName="Telegram"
elif [ "$BuildTarget" == "macstore" ]; then
  if [ "$BetaVersion" != "0" ]; then
    Error "Can't build macstore beta version!"
  fi

  echo "Building version $AppVersionStrFull for Mac App Store.."
  ReleasePath="$HomePath/../out/Release"
  BinaryName="Telegram Desktop"
else
  Error "Invalid target!"
fi

#if [ "$BuildTarget" == "linux" ] || [ "$BuildTarget" == "linux32" ] || [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "mac32" ] || [ "$BuildTarget" == "macstore" ]; then
  if [ "$BetaVersion" != "0" ]; then
    if [ -f "$ReleasePath/$BetaKeyFile" ]; then
      Error "Beta version key file for version $AppVersion already exists!"
    fi

    if [ -d "$ReleasePath/deploy/$AppVersionStrMajor/$AppVersionStrFull" ]; then
      Error "Deploy folder for version $AppVersionStrFull already exists!"
    fi
  else
    if [ -d "$ReleasePath/deploy/$AppVersionStrMajor/$AppVersionStr.alpha" ]; then
      Error "Deploy folder for version $AppVersionStr.alpha already exists!"
    fi

    if [ -d "$ReleasePath/deploy/$AppVersionStrMajor/$AppVersionStr.dev" ]; then
      Error "Deploy folder for version $AppVersionStr.dev already exists!"
    fi

    if [ -d "$ReleasePath/deploy/$AppVersionStrMajor/$AppVersionStr" ]; then
      Error "Deploy folder for version $AppVersionStr already exists!"
    fi

    if [ -f "$ReleasePath/$UpdateFile" ]; then
      Error "Update file for version $AppVersion already exists!"
    fi
  fi

  DeployPath="$ReleasePath/deploy/$AppVersionStrMajor/$AppVersionStrFull"
#fi

if [ "$BuildTarget" == "linux" ] || [ "$BuildTarget" == "linux32" ]; then

  DropboxSymbolsPath="/media/psf/Dropbox/Telegram/symbols"
  if [ ! -d "$DropboxSymbolsPath" ]; then
    Error "Dropbox path not found!"
  fi

  gyp/refresh.sh

  cd $ReleasePath
  make -j4
  echo "$BinaryName build complete!"

  if [ ! -f "$ReleasePath/$BinaryName" ]; then
    Error "$BinaryName not found!"
  fi

  BadCount=`objdump -T $ReleasePath/$BinaryName | grep GLIBC_2\.1[6-9] | wc -l`
  if [ "$BadCount" != "0" ]; then
    Error "Bad GLIBC usages found: $BadCount"
  fi

  BadCount=`objdump -T $ReleasePath/$BinaryName | grep GLIBC_2\.2[0-9] | wc -l`
  if [ "$BadCount" != "0" ]; then
    Error "Bad GLIBC usages found: $BadCount"
  fi

  BadCount=`objdump -T $ReleasePath/$BinaryName | grep GCC_4\.[3-9] | wc -l`
  if [ "$BadCount" != "0" ]; then
    Error "Bad GCC usages found: $BadCount"
  fi

  BadCount=`objdump -T $ReleasePath/$BinaryName | grep GCC_[5-9]\. | wc -l`
  if [ "$BadCount" != "0" ]; then
    Error "Bad GCC usages found: $BadCount"
  fi

  if [ ! -f "$ReleasePath/Updater" ]; then
    Error "Updater not found!"
  fi

  BadCount=`objdump -T $ReleasePath/Updater | grep GLIBC_2\.1[6-9] | wc -l`
  if [ "$BadCount" != "0" ]; then
    Error "Bad GLIBC usages found: $BadCount"
  fi

  BadCount=`objdump -T $ReleasePath/Updater | grep GLIBC_2\.2[0-9] | wc -l`
  if [ "$BadCount" != "0" ]; then
    Error "Bad GLIBC usages found: $BadCount"
  fi

  BadCount=`objdump -T $ReleasePath/Updater | grep GCC_4\.[3-9] | wc -l`
  if [ "$BadCount" != "0" ]; then
    Error "Bad GCC usages found: $BadCount"
  fi

  BadCount=`objdump -T $ReleasePath/Updater | grep GCC_[5-9]\. | wc -l`
  if [ "$BadCount" != "0" ]; then
    Error "Bad GCC usages found: $BadCount"
  fi

  echo "Dumping debug symbols.."
  "$HomePath/../../Libraries/breakpad/out/Default/dump_syms" "$ReleasePath/$BinaryName" > "$ReleasePath/$BinaryName.sym"
  echo "Done!"

  echo "Stripping the executable.."
  strip -s "$ReleasePath/$BinaryName"
  echo "Done!"

  echo "Preparing version $AppVersionStrFull, executing Packer.."
  cd "$ReleasePath"
  "./Packer" -path "$BinaryName" -path Updater -version $VersionForPacker $AlphaBetaParam
  echo "Packer done!"

  if [ "$BetaVersion" != "0" ]; then
    if [ ! -f "$ReleasePath/$BetaKeyFile" ]; then
      Error "Beta version key file not found!"
    fi

    while IFS='' read -r line || [[ -n "$line" ]]; do
      BetaSignature="$line"
    done < "$ReleasePath/$BetaKeyFile"

    UpdateFile="${UpdateFile}_${BetaSignature}"
    SetupFile="tbeta${BetaVersion}_${BetaSignature}.tar.xz"
  fi

  SymbolsHash=`head -n 1 "$ReleasePath/$BinaryName.sym" | awk -F " " 'END {print $4}'`
  echo "Copying $BinaryName.sym to $DropboxSymbolsPath/$BinaryName/$SymbolsHash"
  mkdir -p "$DropboxSymbolsPath/$BinaryName/$SymbolsHash"
  cp "$ReleasePath/$BinaryName.sym" "$DropboxSymbolsPath/$BinaryName/$SymbolsHash/"
  echo "Done!"

  if [ ! -d "$ReleasePath/deploy" ]; then
    mkdir "$ReleasePath/deploy"
  fi

  if [ ! -d "$ReleasePath/deploy/$AppVersionStrMajor" ]; then
    mkdir "$ReleasePath/deploy/$AppVersionStrMajor"
  fi

  echo "Copying $BinaryName, Updater and $UpdateFile to deploy/$AppVersionStrMajor/$AppVersionStrFull..";
  mkdir "$DeployPath"
  mkdir "$DeployPath/$BinaryName"
  mv "$ReleasePath/$BinaryName" "$DeployPath/$BinaryName/"
  mv "$ReleasePath/Updater" "$DeployPath/$BinaryName/"
  mv "$ReleasePath/$UpdateFile" "$DeployPath/"
  if [ "$BetaVersion" != "0" ]; then
    mv "$ReleasePath/$BetaKeyFile" "$DeployPath/"
  fi
  cd "$DeployPath"
  tar -cJvf "$SetupFile" "$BinaryName/"
fi

if [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "mac32" ] || [ "$BuildTarget" == "macstore" ]; then

  DropboxSymbolsPath="$HOME/Dropbox/Telegram/symbols"
  if [ ! -d "$DropboxSymbolsPath" ]; then
    Error "Dropbox path not found!"
  fi

  gyp/refresh.sh
  xcodebuild -project Telegram.xcodeproj -alltargets -configuration Release build

  if [ ! -d "$ReleasePath/$BinaryName.app" ]; then
    Error "$BinaryName.app not found!"
  fi

  if [ ! -d "$ReleasePath/$BinaryName.app.dSYM" ]; then
    Error "$BinaryName.app.dSYM not found!"
  fi

  if [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "mac32" ]; then
    if [ ! -f "$ReleasePath/$BinaryName.app/Contents/Frameworks/Updater" ]; then
      Error "Updater not found!"
    fi
    if [ ! -f "$ReleasePath/$BinaryName.app/Contents/Helpers/crashpad_handler" ]; then
      Error "crashpad_handler not found!"
    fi
  fi
  if [ "$BuildTarget" == "macstore" ]; then
    if [ ! -d "$ReleasePath/$BinaryName.app/Contents/Frameworks/Breakpad.framework" ]; then
      Error "Breakpad.framework not found!"
    fi
  fi

  echo "Dumping debug symbols.."
  "$HomePath/../../Libraries/breakpad/src/tools/mac/dump_syms/build/Release/dump_syms" "$ReleasePath/$BinaryName.app.dSYM" > "$ReleasePath/$BinaryName.sym" 2>/dev/null
  echo "Done!"

  echo "Stripping the executable.."
  strip "$ReleasePath/$BinaryName.app/Contents/MacOS/$BinaryName"
  echo "Done!"

  echo "Signing the application.."
  if [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "mac32" ]; then
    codesign --force --deep --sign "Developer ID Application: John Preston" "$ReleasePath/$BinaryName.app"
  elif [ "$BuildTarget" == "macstore" ]; then
    codesign --force --deep --sign "3rd Party Mac Developer Application: TELEGRAM MESSENGER LLP (6N38VWS5BX)" "$ReleasePath/$BinaryName.app" --entitlements "$HomePath/Telegram/Telegram Desktop.entitlements"
    echo "Making an installer.."
    productbuild --sign "3rd Party Mac Developer Installer: TELEGRAM MESSENGER LLP (6N38VWS5BX)" --component "$ReleasePath/$BinaryName.app" /Applications "$ReleasePath/$BinaryName.pkg"
  fi
  echo "Done!"

  AppUUID=`dwarfdump -u "$ReleasePath/$BinaryName.app/Contents/MacOS/$BinaryName" | awk -F " " '{print $2}'`
  DsymUUID=`dwarfdump -u "$ReleasePath/$BinaryName.app.dSYM" | awk -F " " '{print $2}'`
  if [ "$AppUUID" != "$DsymUUID" ]; then
    Error "UUID of binary '$AppUUID' and dSYM '$DsymUUID' differ!"
  fi

  if [ ! -f "$ReleasePath/$BinaryName.app/Contents/Resources/Icon.icns" ]; then
    Error "Icon.icns not found in Resources!"
  fi

  if [ ! -f "$ReleasePath/$BinaryName.app/Contents/MacOS/$BinaryName" ]; then
    Error "$BinaryName not found in MacOS!"
  fi

  if [ ! -d "$ReleasePath/$BinaryName.app/Contents/_CodeSignature" ]; then
    Error "$BinaryName signature not found!"
  fi

  if [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "mac32" ]; then
    if [ ! -f "$ReleasePath/$BinaryName.app/Contents/Frameworks/Updater" ]; then
      Error "Updater not found in Frameworks!"
    fi
  elif [ "$BuildTarget" == "macstore" ]; then
    if [ ! -f "$ReleasePath/$BinaryName.pkg" ]; then
      Error "$BinaryName.pkg not found!"
    fi
  fi

  SymbolsHash=`head -n 1 "$ReleasePath/$BinaryName.sym" | awk -F " " 'END {print $4}'`
  echo "Copying $BinaryName.sym to $DropboxSymbolsPath/$BinaryName/$SymbolsHash"
  mkdir -p "$DropboxSymbolsPath/$BinaryName/$SymbolsHash"
  cp "$ReleasePath/$BinaryName.sym" "$DropboxSymbolsPath/$BinaryName/$SymbolsHash/"
  echo "Done!"

  if [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "mac32" ]; then
    if [ "$BetaVersion" == "0" ]; then
      cd "$ReleasePath"
      cp -f tsetup_template.dmg tsetup.temp.dmg
      TempDiskPath=`hdiutil attach -nobrowse -noautoopenrw -readwrite tsetup.temp.dmg | awk -F "\t" 'END {print $3}'`
      cp -R "./$BinaryName.app" "$TempDiskPath/"
      bless --folder "$TempDiskPath/" --openfolder "$TempDiskPath/"
      hdiutil detach "$TempDiskPath"
      hdiutil convert tsetup.temp.dmg -format UDZO -imagekey zlib-level=9 -ov -o "$SetupFile"
      rm tsetup.temp.dmg
    fi
    cd "$ReleasePath"
    "./Packer" -path "$BinaryName.app" -target "$BuildTarget" -version $VersionForPacker $AlphaBetaParam
    echo "Packer done!"

    if [ "$BetaVersion" != "0" ]; then
      if [ ! -f "$ReleasePath/$BetaKeyFile" ]; then
        Error "Beta version key file not found!"
      fi

      while IFS='' read -r line || [[ -n "$line" ]]; do
        BetaSignature="$line"
      done < "$ReleasePath/$BetaKeyFile"

      UpdateFile="${UpdateFile}_${BetaSignature}"
      SetupFile="tbeta${BetaVersion}_${BetaSignature}.zip"
    fi
  fi

  if [ ! -d "$ReleasePath/deploy" ]; then
    mkdir "$ReleasePath/deploy"
  fi

  if [ ! -d "$ReleasePath/deploy/$AppVersionStrMajor" ]; then
    mkdir "$ReleasePath/deploy/$AppVersionStrMajor"
  fi

  if [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "mac32" ]; then
    echo "Copying $BinaryName.app and $UpdateFile to deploy/$AppVersionStrMajor/$AppVersionStr..";
    mkdir "$DeployPath"
    mkdir "$DeployPath/$BinaryName"
    cp -r "$ReleasePath/$BinaryName.app" "$DeployPath/$BinaryName/"
    if [ "$BetaVersion" != "0" ]; then
      cd "$DeployPath"
      zip -r "$SetupFile" "$BinaryName"
      mv "$SetupFile" "$ReleasePath/"
      mv "$ReleasePath/$BetaKeyFile" "$DeployPath/"
    fi
    mv "$ReleasePath/$BinaryName.app.dSYM" "$DeployPath/"
    rm "$ReleasePath/$BinaryName.app/Contents/MacOS/$BinaryName"
    rm "$ReleasePath/$BinaryName.app/Contents/Frameworks/Updater"
    rm "$ReleasePath/$BinaryName.app/Contents/Info.plist"
    rm -rf "$ReleasePath/$BinaryName.app/Contents/_CodeSignature"
    mv "$ReleasePath/$UpdateFile" "$DeployPath/"
    mv "$ReleasePath/$SetupFile" "$DeployPath/"

    if [ "$BuildTarget" == "mac32" ]; then
      ReleaseToPath="$HomePath/../../deploy_temp/tmac32"
      DeployToPath="$ReleaseToPath/$AppVersionStrMajor/$AppVersionStrFull"
      if [ ! -d "$ReleaseToPath/$AppVersionStrMajor" ]; then
        mkdir "$ReleaseToPath/$AppVersionStrMajor"
      fi

      if [ ! -d "$DeployToPath" ]; then
        mkdir "$DeployToPath"
      fi

      cp -v "$DeployPath/$UpdateFile" "$DeployToPath/"
      cp -v "$DeployPath/$SetupFile" "$DeployToPath/"
      if [ "$BetaVersion" != "0" ]; then
        cp -v "$DeployPath/$BetaKeyFile" "$DeployToPath/"
      fi
    fi
  elif [ "$BuildTarget" == "macstore" ]; then
    echo "Copying $BinaryName.app to deploy/$AppVersionStrMajor/$AppVersionStr..";
    mkdir "$DeployPath"
    cp -r "$ReleasePath/$BinaryName.app" "$DeployPath/"
    mv "$ReleasePath/$BinaryName.pkg" "$DeployPath/"
    mv "$ReleasePath/$BinaryName.app.dSYM" "$DeployPath/"
    rm "$ReleasePath/$BinaryName.app/Contents/MacOS/$BinaryName"
    rm "$ReleasePath/$BinaryName.app/Contents/Info.plist"
    rm -rf "$ReleasePath/$BinaryName.app/Contents/_CodeSignature"
  fi
fi

echo "Version $AppVersionStrFull is ready!";
