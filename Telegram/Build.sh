set -e

while IFS='' read -r line || [[ -n "$line" ]]; do
  set $line
  eval $1="$2"
done < Version

VersionForPacker="$AppVersion"
if [ "$BetaVersion" != "0" ]; then
  AppVersion="$BetaVersion"
  AppVersionStrFull="${AppVersionStr}_${BetaVersion}"
  DevParam="-beta $BetaVersion"
  BetaKeyFile="tbeta_${AppVersion}_key"
elif [ "$DevChannel" == "0" ]; then
  AppVersionStrFull="$AppVersionStr"
  DevParam=''
else
  AppVersionStrFull="$AppVersionStr.dev"
  DevParam='-dev'
fi

if [ ! -f "Target" ]; then
  echo "Build target not found!"
  exit 1
fi

while IFS='' read -r line || [[ -n "$line" ]]; do
  BuildTarget="$line"
done < Target

echo ""
HomePath="./../../Telegram"
if [ "$BuildTarget" == "linux" ]; then
  echo "Building version $AppVersionStrFull for Linux 64bit.."
  UpdateFile="tlinuxupd$AppVersion"
  SetupFile="tsetup.$AppVersionStrFull.tar.xz"
  WorkPath="./../Linux"
  FixScript="$HomePath/FixMake.sh"
  ReleasePath="./../Linux/Release"
  BinaryName="Telegram"
elif [ "$BuildTarget" == "linux32" ]; then
  echo "Building version $AppVersionStrFull for Linux 32bit.."
  UpdateFile="tlinux32upd$AppVersion"
  SetupFile="tsetup32.$AppVersionStrFull.tar.xz"
  WorkPath="./../Linux"
  FixScript="$HomePath/FixMake32.sh"
  ReleasePath="./../Linux/Release"
  BinaryName="Telegram"
elif [ "$BuildTarget" == "mac" ]; then
  echo "Building version $AppVersionStrFull for OS X 10.8+.."
  UpdateFile="tmacupd$AppVersion"
  SetupFile="tsetup.$AppVersionStrFull.dmg"
  ReleasePath="./../Mac/Release"
  BinaryName="Telegram"
elif [ "$BuildTarget" == "mac32" ]; then
  echo "Building version $AppVersionStrFull for OS X 10.6 and 10.7.."
  UpdateFile="tmac32upd$AppVersion"
  SetupFile="tsetup32.$AppVersionStrFull.dmg"
  ReleasePath="./../Mac/Release"
  BinaryName="Telegram"
elif [ "$BuildTarget" == "macstore" ]; then
  if [ "$BetaVersion" != "0" ]; then
    echo "Can't build macstore beta version!"
    exit 1
  fi

  echo "Building version $AppVersionStrFull for Mac App Store.."
  ReleasePath="./../Mac/Release"
  BinaryName="Telegram Desktop"
  DropboxPath="./../../../Dropbox/Telegram/deploy/$AppVersionStrMajor"
  DropboxDeployPath="$DropboxPath/$AppVersionStrFull"
else
  echo "Invalid target!"
  exit 1
fi

#if [ "$BuildTarget" == "linux" ] || [ "$BuildTarget" == "linux32" ] || [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "mac32" ] || [ "$BuildTarget" == "macstore" ]; then
  if [ "$BetaVersion" != "0" ]; then
    if [ -f "$ReleasePath/$BetaKeyFile" ]; then
      echo "Beta version key file for version $AppVersion already exists!"
      exit 1
    fi

    if [ -d "$ReleasePath/deploy/$AppVersionStrMajor/$AppVersionStrFull" ]; then
      echo "Deploy folder for version $AppVersionStrFull already exists!"
      exit 1
    fi
  else
    if [ -d "$ReleasePath/deploy/$AppVersionStrMajor/$AppVersionStr.dev" ]; then
      echo "Deploy folder for version $AppVersionStr.dev already exists!"
      exit 1
    fi

    if [ -f "$ReleasePath/$UpdateFile" ]; then
      echo "Update file for version $AppVersion already exists!"
      exit 1
    fi

    if [ -d "$ReleasePath/deploy/$AppVersionStrMajor/$AppVersionStr" ]; then
      echo "Deploy folder for version $AppVersionStr already exists!"
      exit 1
    fi
  fi

  DeployPath="$ReleasePath/deploy/$AppVersionStrMajor/$AppVersionStrFull"
#fi

if [ "$BuildTarget" == "linux" ] || [ "$BuildTarget" == "linux32" ]; then

  DropboxSymbolsPath="/media/psf/Home/Dropbox/Telegram/symbols"

  mkdir -p "$WorkPath/ReleaseIntermediateUpdater"
  cd "$WorkPath/ReleaseIntermediateUpdater"
  /usr/local/Qt-5.5.1/bin/qmake "$HomePath/Updater.pro"
  make
  echo "Updater build complete!"
  cd "$HomePath"

  mkdir -p "$WorkPath/ReleaseIntermediate"
  cd "$WorkPath/ReleaseIntermediate"
  /usr/local/Qt-5.5.1/bin/qmake "$HomePath/Telegram.pro"
  eval "$FixScript"
  make
  echo "Telegram build complete!"
  cd "$HomePath"
  if [ ! -f "$ReleasePath/$BinaryName" ]; then
    echo "$BinaryName not found!"
    exit 1
  fi

  if [ ! -f "$ReleasePath/Updater" ]; then
    echo "Updater not found!"
    exit 1
  fi

  echo "Dumping debug symbols.."
  "./../../Libraries/breakpad/src/tools/linux/dump_syms/dump_syms" "$ReleasePath/$BinaryName" > "$ReleasePath/$BinaryName.sym"
  echo "Done!"

  echo "Stripping the executable.."
  strip -s "$ReleasePath/$BinaryName"
  echo "Done!"

  echo "Preparing version $AppVersionStrFull, executing Packer.."
  cd "$ReleasePath" && "./Packer" -path "$BinaryName" -path Updater -version $VersionForPacker $DevParam && cd "$HomePath"
  echo "Packer done!"

  if [ "$BetaVersion" != "0" ]; then
    if [ ! -f "$ReleasePath/$BetaKeyFile" ]; then
      echo "Beta version key file not found!"
      exit 1
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
  mv "$ReleasePath/$BinaryName" "$DeployPath/Telegram/"
  mv "$ReleasePath/Updater" "$DeployPath/Telegram/"
  mv "$ReleasePath/$UpdateFile" "$DeployPath/"
  if [ "$BetaVersion" != "0" ]; then
    mv "$ReleasePath/$BetaKeyFile" "$DeployPath/"
  fi
  cd "$DeployPath" && tar -cJvf "$SetupFile" "Telegram/" && cd "./../../../$HomePath"
fi

if [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "mac32" ] || [ "$BuildTarget" == "macstore" ]; then

  DropboxSymbolsPath="./../../../Dropbox/Telegram/symbols"

  touch "./SourceFiles/telegram.qrc"
  xcodebuild -project Telegram.xcodeproj -alltargets -configuration Release build

  if [ ! -d "$ReleasePath/$BinaryName.app" ]; then
    echo "$BinaryName.app not found!"
    exit 1
  fi

  if [ ! -d "$ReleasePath/$BinaryName.app.dSYM" ]; then
    echo "$BinaryName.app.dSYM not found!"
    exit 1
  fi

  if [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "mac32" ]; then
    echo "Removing Updater debug symbols.."
    rm -rf "$ReleasePath/$BinaryName.app/Contents/Frameworks/Updater.dSYM"
    echo "Done!"
  fi

  echo "Dumping debug symbols.."
  "./../../Libraries/breakpad/src/tools/mac/dump_syms/build/Release/dump_syms" "$ReleasePath/$BinaryName.app.dSYM" > "$ReleasePath/$BinaryName.sym" 2>/dev/null
  echo "Done!"

  echo "Stripping the executable.."
  strip "$ReleasePath/$BinaryName.app/Contents/MacOS/$BinaryName"
  echo "Done!"

  echo "Signing the application.."
  if [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "mac32" ]; then
    codesign --force --deep --sign "Developer ID Application: John Preston" "$ReleasePath/$BinaryName.app"
  elif [ "$BuildTarget" == "macstore" ]; then
    codesign --force --deep --sign "3rd Party Mac Developer Application: TELEGRAM MESSENGER LLP (6N38VWS5BX)" "$ReleasePath/$BinaryName.app" --entitlements "Telegram/Telegram Desktop.entitlements"
  fi
  echo "Done!"

  AppUUID=`dwarfdump -u "$ReleasePath/$BinaryName.app/Contents/MacOS/$BinaryName" | awk -F " " '{print $2}'`
  DsymUUID=`dwarfdump -u "$ReleasePath/$BinaryName.app.dSYM" | awk -F " " '{print $2}'`
  if [ "$AppUUID" != "$DsymUUID" ]; then
    echo "UUID of binary '$AppUUID' and dSYM '$DsymUUID' differ!"
    exit 1
  fi

  if [ ! -f "$ReleasePath/$BinaryName.app/Contents/Resources/Icon.icns" ]; then
    echo "Icon.icns not found in Resources!"
    exit 1
  fi

  if [ ! -f "$ReleasePath/$BinaryName.app/Contents/MacOS/$BinaryName" ]; then
    echo "$BinaryName not found in MacOS!"
    exit 1
  fi

  if [ ! -d "$ReleasePath/$BinaryName.app/Contents/_CodeSignature" ]; then
    echo "$BinaryName signature not found!"
    exit 1
  fi

  if [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "mac32" ]; then
    if [ ! -f "$ReleasePath/$BinaryName.app/Contents/Frameworks/Updater" ]; then
      echo "Updater not found in Frameworks!"
      exit 1
    fi
  elif [ "$BuildTarget" == "macstore" ]; then
    if [ ! -f "$ReleasePath/$BinaryName.pkg" ]; then
      echo "$BinaryName.pkg not found!"
      exit 1
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
      temppath=`hdiutil attach -readwrite tsetup.dmg | awk -F "\t" 'END {print $3}'`
      cp -R "./$BinaryName.app" "$temppath/"
      bless --folder "$temppath/" --openfolder "$temppath/"
      hdiutil detach "$temppath"
      hdiutil convert tsetup.dmg -format UDZO -imagekey zlib-level=9 -ov -o "$SetupFile"
      cd "./../../Telegram"
    fi
    cd "$ReleasePath" && "./Packer.app/Contents/MacOS/Packer" -path "$BinaryName.app" -version $VersionForPacker $DevParam && cd "$HomePath"
    echo "Packer done!"

    if [ "$BetaVersion" != "0" ]; then
      if [ ! -f "$ReleasePath/$BetaKeyFile" ]; then
        echo "Beta version key file not found!"
        exit 1
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
    mkdir "$DeployPath/Telegram"
    cp -r "$ReleasePath/$BinaryName.app" "$DeployPath/Telegram/"
    if [ "$BetaVersion" != "0" ]; then
      cd "$DeployPath" && zip -r "$SetupFile" "Telegram" && mv "$SetupFile" "./../../../" && cd "./../../../$HomePath"
      mv "$ReleasePath/$BetaKeyFile" "$DeployPath/"
    fi
    mv "$ReleasePath/$BinaryName.app.dSYM" "$DeployPath/"
    rm "$ReleasePath/$BinaryName.app/Contents/MacOS/$BinaryName"
    rm "$ReleasePath/$BinaryName.app/Contents/Frameworks/Updater"
    rm -rf "$ReleasePath/$BinaryName.app/Contents/_CodeSignature"
    mv "$ReleasePath/$UpdateFile" "$DeployPath/"
    mv "$ReleasePath/$SetupFile" "$DeployPath/"

    if [ "$BuildTarget" == "mac32" ]; then
      ReleaseToPath="./../../../TBuild/tother/tmac32"
      DeployToPath="$ReleaseToPath/$AppVersionStrMajor/$AppVersionStrFull"
      if [ ! -d "$ReleaseToPath/$AppVersionStrMajor" ]; then
        mkdir "$ReleaseToPath/$AppVersionStrMajor"
      fi

      if [ ! -d "$DeployToPath" ]; then
        mkdir "$DeployToPath"
      fi

      cp -v "$DeployPath/$UpdateFile" "$DeployToPath/"
      cp -v "$DeployPath/$SetupFile" "$DeployToPath/"
      cp -rv "$DeployPath/$BinaryName.app.dSYM" "$DeployToPath/"
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
    rm -rf "$ReleasePath/$BinaryName.app/Contents/_CodeSignature"

    mkdir -p "$DropboxDeployPath"
    cp -v "$DeployPath/$BinaryName.app" "$DropboxDeployPath/"
    cp -rv "$DeployPath/$BinaryName.app.dSYM" "$DropboxDeployPath/"
  fi
fi

echo "Version $AppVersionStrFull is ready!";
