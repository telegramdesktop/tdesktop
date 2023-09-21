set -e
FullExecPath=$PWD
pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

arg1="$1"
arg2="$2"
arg3="$3"

if [ ! -d "$FullScriptPath/../../../DesktopPrivate" ]; then
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
if [ "$AlphaVersion" != "0" ]; then
  AppVersion="$AlphaVersion"
  AppVersionStrFull="${AppVersionStr}_${AlphaVersion}"
  AlphaBetaParam="-alpha $AlphaVersion"
  AlphaKeyFile="talpha_${AppVersion}_key"
elif [ "$BetaChannel" == "0" ]; then
  AppVersionStrFull="$AppVersionStr"
  AlphaBetaParam=''
else
  AppVersionStrFull="$AppVersionStr.beta"
  AlphaBetaParam='-beta'
fi

echo ""
HomePath="$FullScriptPath/.."
if [ "$BuildTarget" == "linux" ]; then
  echo "Building version $AppVersionStrFull for Linux 64bit.."
  UpdateFile="tlinuxupd$AppVersion"
  SetupFile="tsetup.$AppVersionStrFull.tar.xz"
  ProjectPath="$HomePath/../out"
  ReleasePath="$ProjectPath/Release"
  BinaryName="Telegram"
elif [ "$BuildTarget" == "mac" ] ; then
  if [ "$arg1" == "x86_64" ] || [ "$arg1" == "arm64" ]; then
    echo "Building version $AppVersionStrFull for macOS 10.13+ ($arg1).."
    MacArch="$arg1"
    if [ "$arg2" == "request_uuid" ] && [ "$arg3" != "" ]; then
      NotarizeRequestId="$arg3"
    fi
  else
    echo "Building version $AppVersionStrFull for macOS 10.13+.."
    if [ "$arg2" != "" ]; then
      if [ "$arg1" == "request_uuid_x86_64" ]; then
        NotarizeRequestIdAMD64="$arg2"
      elif [ "$arg1" == "request_uuid_arm64" ]; then
        NotarizeRequestIdARM64="$arg2"
      elif [ "$arg1" == "request_uuid" ]; then
        NotarizeRequestId="$arg2"
      fi
    fi
  fi

  if [ "$AC_USERNAME" == "" ]; then
    Error "AC_USERNAME not found!"
  fi
  UpdateFileAMD64="tmacupd$AppVersion"
  UpdateFileARM64="tarmacupd$AppVersion"
  if [ "$MacArch" == "arm64" ]; then
    UpdateFile="$UpdateFileARM64"
  elif [ "$MacArch" == "x86_64" ]; then
    UpdateFile="$UpdateFileAMD64"
  fi
  ProjectPath="$HomePath/../out"
  ReleasePath="$ProjectPath/Release"
  BinaryName="Telegram"
  if [ "$MacArch" != "" ]; then
    BundleName="$BinaryName.$MacArch.app"
    SetupFile="tsetup.$MacArch.$AppVersionStrFull.dmg"
  else
    BundleName="$BinaryName.app"
    SetupFile="tsetup.$AppVersionStrFull.dmg"
  fi
elif [ "$BuildTarget" == "macstore" ]; then
  if [ "$AlphaVersion" != "0" ]; then
    Error "Can't build macstore alpha version!"
  fi

  echo "Building version $AppVersionStrFull for Mac App Store.."
  ProjectPath="$HomePath/../out"
  ReleasePath="$ProjectPath/Release"
  BinaryName="Telegram Lite"
  BundleName="$BinaryName.app"
else
  Error "Invalid target!"
fi

if [ "$AlphaVersion" != "0" ]; then
  if [ -d "$ReleasePath/deploy/$AppVersionStrMajor/$AppVersionStrFull" ]; then
    Error "Deploy folder for version $AppVersionStrFull already exists!"
  fi
else
  if [ -d "$ReleasePath/deploy/$AppVersionStrMajor/$AppVersionStr.alpha" ]; then
    Error "Deploy folder for version $AppVersionStr.alpha already exists!"
  fi

  if [ -d "$ReleasePath/deploy/$AppVersionStrMajor/$AppVersionStr.beta" ]; then
    Error "Deploy folder for version $AppVersionStr.beta already exists!"
  fi

  if [ -d "$ReleasePath/deploy/$AppVersionStrMajor/$AppVersionStr" ]; then
    Error "Deploy folder for version $AppVersionStr already exists!"
  fi

  if [ -f "$ReleasePath/$UpdateFile" ]; then
    Error "Update file for version $AppVersion already exists!"
  fi
fi

DeployPath="$ReleasePath/deploy/$AppVersionStrMajor/$AppVersionStrFull"

if [ "$BuildTarget" == "linux" ]; then

  DropboxSymbolsPath="/media/psf/Dropbox/Telegram/symbols"
  if [ ! -d "$DropboxSymbolsPath" ]; then
    DropboxSymbolsPath="/mnt/c/Telegram/Dropbox/Telegram/symbols"
    if [ ! -d "$DropboxSymbolsPath" ]; then
      Error "Dropbox path not found!"
    fi
  fi

  BackupPath="/media/psf/backup/tdesktop/$AppVersionStrMajor/$AppVersionStrFull/t$BuildTarget"
  if [ ! -d "/media/psf/backup/tdesktop" ]; then
    BackupPath="/mnt/c/Telegram/Projects/backup/tdesktop/$AppVersionStrMajor/$AppVersionStrFull/t$BuildTarget"
    if [ ! -d "/mnt/c/Telegram/Projects/backup/tdesktop" ]; then
      Error "Backup folder not found!"
    fi
  fi

  ./build/docker/centos_env/run.sh /usr/src/tdesktop/Telegram/build/docker/build.sh

  echo "Copying from docker result folder."
  cp "$ReleasePath/root/$BinaryName" "$ReleasePath/$BinaryName"
  cp "$ReleasePath/root/Updater" "$ReleasePath/Updater"
  cp "$ReleasePath/root/Packer" "$ReleasePath/Packer"

  echo "Dumping debug symbols.."
  "$ReleasePath/dump_syms" "$ReleasePath/$BinaryName" > "$ReleasePath/$BinaryName.sym"
  echo "Done!"

  echo "Stripping the executable.."
  strip -s "$ReleasePath/$BinaryName"
  echo "Done!"

  echo "Preparing version $AppVersionStrFull, executing Packer.."
  cd "$ReleasePath"
  "./Packer" -path "$BinaryName" -path Updater -version $VersionForPacker $AlphaBetaParam
  echo "Packer done!"

  if [ "$AlphaVersion" != "0" ]; then
    if [ ! -f "$ReleasePath/$AlphaKeyFile" ]; then
      Error "Alpha version key file not found!"
    fi

    while IFS='' read -r line || [[ -n "$line" ]]; do
      AlphaSignature="$line"
    done < "$ReleasePath/$AlphaKeyFile"

    UpdateFile="${UpdateFile}_${AlphaSignature}"
    SetupFile="talpha${AlphaVersion}_${AlphaSignature}.tar.xz"
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
  if [ "$AlphaVersion" != "0" ]; then
    mv "$ReleasePath/$AlphaKeyFile" "$DeployPath/"
  fi
  cd "$DeployPath"
  tar -cJvf "$SetupFile" "$BinaryName/"

  mkdir -p $BackupPath
  cp "$SetupFile" "$BackupPath/"
  cp "$UpdateFile" "$BackupPath/"
  if [ "$AlphaVersion" != "0" ]; then
    cp -v "$AlphaKeyFile" "$BackupPath/"
  fi
fi

if [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "macstore" ]; then

  DropboxSymbolsPath="$HOME/Dropbox/Telegram/symbols"
  if [ ! -d "$DropboxSymbolsPath" ]; then
    Error "Dropbox path not found!"
  fi

  BackupPath="$HOME/Projects/backup/tdesktop/$AppVersionStrMajor/$AppVersionStrFull"
  if [ ! -d "$HOME/Projects/backup/tdesktop" ]; then
    Error "Backup path not found!"
  fi

  if [ "$MacArch" != "" ]; then
    cd $ReleasePath

    echo "Preparing single $MacArch .app.."
    rm -rf $BundleName
    cp -R $BinaryName.app $BundleName
    lipo -thin $MacArch $BinaryName.app/Contents/MacOS/$BinaryName -output $BundleName/Contents/MacOS/$BinaryName
    lipo -thin $MacArch $BinaryName.app/Contents/Frameworks/Updater -output $BundleName/Contents/Frameworks/Updater
    lipo -thin $MacArch $BinaryName.app/Contents/Helpers/crashpad_handler -output $BundleName/Contents/Helpers/crashpad_handler
    echo "Done!"
  elif [ "$NotarizeRequestId" == "" ]; then
    if [ "$NotarizeRequestIdAMD64" == "" ] && [ "$NotarizeRequestIdARM64" == "" ]; then
      if [ -f "$ReleasePath/$BinaryName.app/Contents/Info.plist" ]; then
        rm "$ReleasePath/$BinaryName.app/Contents/Info.plist"
      fi
      if [ -f "$ProjectPath/Telegram/CMakeFiles/Telegram.dir/Info.plist" ]; then
        rm "$ProjectPath/Telegram/CMakeFiles/Telegram.dir/Info.plist"
      fi
      rm -rf "$ReleasePath/$BinaryName.app/Contents/_CodeSignature"
      rm -rf "$ReleasePath/Updater"

      ./configure.sh -D DESKTOP_APP_MAC_ARCH="arm64;x86_64"

      cd $ProjectPath
      cmake --build . --config Release --target Telegram
    fi

    if [ ! -d "$ReleasePath/$BinaryName.app" ]; then
      Error "$BinaryName.app not found!"
    fi

    cd $FullExecPath

    if [ "$BuildTarget" == "mac" ]; then
      if [ "$NotarizeRequestIdAMD64" == "" ]; then
        echo "Preparing single arm64 update.."
        ./$0 arm64 request_uuid $NotarizeRequestIdARM64
      fi

      echo "Preparing single x86_64 update.."
      ./$0 x86_64 request_uuid $NotarizeRequestIdAMD64

      echo "Done."
    fi
    cd $ReleasePath
  fi
  if [ "$NotarizeRequestId" == "" ]; then
    if [ "$BuildTarget" == "mac" ]; then
      if [ ! -f "$ReleasePath/$BundleName/Contents/Frameworks/Updater" ]; then
        Error "Updater not found!"
      fi
      if [ ! -f "$ReleasePath/$BundleName/Contents/Helpers/crashpad_handler" ]; then
        Error "crashpad_handler not found!"
      fi
    fi
    if [ "$BuildTarget" == "macstore" ]; then
      if [ ! -d "$ReleasePath/$BundleName/Contents/Frameworks/Breakpad.framework" ]; then
        Error "Breakpad.framework not found!"
      fi
    fi

    if [ "$MacArch" == "" ]; then
      echo "Dumping debug symbols x86_64 from universal.."
      "$HomePath/../../Libraries/breakpad/src/tools/mac/dump_syms/build/Release/dump_syms" "-a" "x86_64" "$ReleasePath/$BinaryName.app/Contents/MacOS/$BinaryName" > "$ReleasePath/$BinaryName.x86_64.sym" 2>/dev/null
      echo "Done!"

      SymbolsHash=`head -n 1 "$ReleasePath/$BinaryName.x86_64.sym" | awk -F " " 'END {print $4}'`
      echo "Copying $BinaryName.x86_64.sym to $DropboxSymbolsPath/$BinaryName/$SymbolsHash"
      mkdir -p "$DropboxSymbolsPath/$BinaryName/$SymbolsHash"
      cp "$ReleasePath/$BinaryName.x86_64.sym" "$DropboxSymbolsPath/$BinaryName/$SymbolsHash/$BinaryName.sym"
      echo "Done!"

      echo "Dumping debug symbols arm64 from universal.."
      "$HomePath/../../Libraries/breakpad/src/tools/mac/dump_syms/build/Release/dump_syms" "-a" "arm64" "$ReleasePath/$BinaryName.app/Contents/MacOS/$BinaryName" > "$ReleasePath/$BinaryName.arm64.sym" 2>/dev/null
      echo "Done!"

      SymbolsHash=`head -n 1 "$ReleasePath/$BinaryName.arm64.sym" | awk -F " " 'END {print $4}'`
      echo "Copying $BinaryName.arm64.sym to $DropboxSymbolsPath/$BinaryName/$SymbolsHash"
      mkdir -p "$DropboxSymbolsPath/$BinaryName/$SymbolsHash"
      cp "$ReleasePath/$BinaryName.arm64.sym" "$DropboxSymbolsPath/$BinaryName/$SymbolsHash/$BinaryName.sym"
      echo "Done!"
    fi

    echo "Stripping the executable.."
    strip "$ReleasePath/$BundleName/Contents/MacOS/$BinaryName"
    if [ "$BuildTarget" == "mac" ]; then
      strip "$ReleasePath/$BundleName/Contents/Frameworks/Updater"
      strip "$ReleasePath/$BundleName/Contents/Helpers/crashpad_handler"
    fi
    echo "Done!"

    echo "Signing the application.."
    if [ "$BuildTarget" == "mac" ]; then
      codesign --force --deep --timestamp --options runtime --sign "Developer ID Application: John Preston" "$ReleasePath/$BundleName" --entitlements "$HomePath/Telegram/Telegram.entitlements"
    elif [ "$BuildTarget" == "macstore" ]; then
      codesign --force --timestamp --options runtime --sign "3rd Party Mac Developer Application: Telegram FZ-LLC (C67CF9S4VU)" "$ReleasePath/$BundleName/Contents/Frameworks/Breakpad.framework/Versions/A/Resources/breakpadUtilities.dylib" --entitlements "$HomePath/Telegram/Breakpad.entitlements"
      codesign --force --deep --timestamp --options runtime --sign "3rd Party Mac Developer Application: Telegram FZ-LLC (C67CF9S4VU)" "$ReleasePath/$BundleName" --entitlements "$HomePath/Telegram/Telegram Lite.entitlements"
      echo "Making an installer.."
      productbuild --sign "3rd Party Mac Developer Installer: Telegram FZ-LLC (C67CF9S4VU)" --component "$ReleasePath/$BundleName" /Applications "$ReleasePath/$BinaryName.pkg"
    fi
    echo "Done!"

    if [ ! -f "$ReleasePath/$BundleName/Contents/Resources/Icon.icns" ]; then
      Error "Icon.icns not found in Resources!"
    fi

    if [ ! -f "$ReleasePath/$BundleName/Contents/MacOS/$BinaryName" ]; then
      Error "$BinaryName not found in MacOS!"
    fi

    if [ ! -d "$ReleasePath/$BundleName/Contents/_CodeSignature" ]; then
      Error "$BinaryName signature not found!"
    fi

    if [ "$BuildTarget" == "macstore" ]; then
      if [ ! -f "$ReleasePath/$BinaryName.pkg" ]; then
        Error "$BinaryName.pkg not found!"
      fi
    fi
  fi

  if [ "$BuildTarget" == "mac" ]; then
    cd "$ReleasePath"

    if [ "$NotarizeRequestId" == "" ]; then
      if [ "$AlphaVersion" == "0" ]; then
        cp -f tsetup_template.dmg tsetup.temp.dmg
        TempDiskPath=`hdiutil attach -nobrowse -noautoopenrw -readwrite tsetup.temp.dmg | awk -F "\t" 'END {print $3}'`
        cp -R "./$BundleName" "$TempDiskPath/"
        bless --folder "$TempDiskPath/"
        hdiutil detach "$TempDiskPath"
        hdiutil convert tsetup.temp.dmg -format UDBZ -ov -o "$SetupFile"
        rm tsetup.temp.dmg
      fi
    fi

    if [ "$AlphaVersion" != "0" ]; then
      cd $ReleasePath
      "./Packer" -path "$BundleName" -target "$BuildTarget" -version $VersionForPacker $AlphaBetaParam -alphakey

      if [ ! -f "$AlphaKeyFile" ]; then
        Error "Alpha version key file not found!"
      fi

      while IFS='' read -r line || [[ -n "$line" ]]; do
        AlphaSignature="$line"
      done < "$ReleasePath/$AlphaKeyFile"

      UpdateFile="${UpdateFile}_${AlphaSignature}"
      UpdateFileAMD64="${UpdateFileAMD64}_${AlphaSignature}"
      UpdateFileARM64="${UpdateFileARM64}_${AlphaSignature}"
      if [ "$MacArch" != "" ]; then
        SetupFile="talpha${AlphaVersion}_${MacArch}_${AlphaSignature}.zip"
      else
        SetupFile="talpha${AlphaVersion}_${AlphaSignature}.zip"
      fi

      if [ "$NotarizeRequestId" == "" ]; then
        rm -rf "$ReleasePath/AlphaTemp"
        mkdir "$ReleasePath/AlphaTemp"
        mkdir "$ReleasePath/AlphaTemp/$BinaryName"
        cp -r "$ReleasePath/$BundleName" "$ReleasePath/AlphaTemp/$BinaryName/"
        cd "$ReleasePath/AlphaTemp"
        zip -r "$SetupFile" "$BinaryName"
        mv "$SetupFile" "$ReleasePath/"
        cd "$ReleasePath"
      fi
    fi
    echo "Beginning notarization process."
    xcrun notarytool submit "$SetupFile" --keychain-profile "preston" --wait
    xcrun stapler staple "$ReleasePath/$BundleName"

    if [ "$MacArch" != "" ]; then
      rm "$ReleasePath/$SetupFile"
      echo "Setup file $SetupFile removed."
    elif [ "$AlphaVersion" != "0" ]; then
      rm -rf "$ReleasePath/AlphaTemp"
      mkdir "$ReleasePath/AlphaTemp"
      mkdir "$ReleasePath/AlphaTemp/$BinaryName"
      cp -r "$ReleasePath/$BinaryName.app" "$ReleasePath/AlphaTemp/$BinaryName/"
      cd "$ReleasePath/AlphaTemp"
      zip -r "$SetupFile" "$BinaryName"
      mv "$SetupFile" "$ReleasePath/"
      cd "$ReleasePath"
      echo "Alpha archive re-created."
    else
      xcrun stapler staple "$ReleasePath/$SetupFile"
    fi

    if [ "$MacArch" != "" ]; then
      UpdatePackPath="$ReleasePath/update_pack_${MacArch}"
      rm -rf "$UpdatePackPath"
      mkdir "$UpdatePackPath"
      mv "$ReleasePath/$BundleName" "$UpdatePackPath/$BinaryName.app"
      cp "$ReleasePath/Packer" "$UpdatePackPath/"
      cd "$UpdatePackPath"
      "./Packer" -path "$BinaryName.app" -target "$BuildTarget" -version $VersionForPacker -arch $MacArch $AlphaBetaParam
      echo "Packer done!"
      mv "$UpdateFile" "$ReleasePath/"
      cd "$ReleasePath"
      rm -rf "$UpdatePackPath"
      exit
    fi
  fi

  if [ ! -d "$ReleasePath/deploy" ]; then
    mkdir "$ReleasePath/deploy"
  fi

  if [ ! -d "$ReleasePath/deploy/$AppVersionStrMajor" ]; then
    mkdir "$ReleasePath/deploy/$AppVersionStrMajor"
  fi

  if [ "$BuildTarget" == "mac" ]; then
    echo "Copying $BinaryName.app, $UpdateFileAMD64 and $UpdateFileARM64 to deploy/$AppVersionStrMajor/$AppVersionStr..";
    mkdir "$DeployPath"
    mkdir "$DeployPath/$BinaryName"
    cp -r "$ReleasePath/$BinaryName.app" "$DeployPath/$BinaryName/"
    if [ "$AlphaVersion" != "0" ]; then
      mv "$ReleasePath/$AlphaKeyFile" "$DeployPath/"
    fi
    rm "$ReleasePath/$BinaryName.app/Contents/MacOS/$BinaryName"
    rm "$ReleasePath/$BinaryName.app/Contents/Frameworks/Updater"
    mv "$ReleasePath/$UpdateFileAMD64" "$DeployPath/"
    mv "$ReleasePath/$UpdateFileARM64" "$DeployPath/"
    mv "$ReleasePath/$SetupFile" "$DeployPath/"

    if [ "$BuildTarget" == "mac" ]; then
      mkdir -p "$BackupPath/tmac"
      cp "$DeployPath/$UpdateFileAMD64" "$BackupPath/tmac/"
      cp "$DeployPath/$UpdateFileARM64" "$BackupPath/tmac/"
      cp "$DeployPath/$SetupFile" "$BackupPath/tmac/"
      if [ "$AlphaVersion" != "0" ]; then
        cp -v "$DeployPath/$AlphaKeyFile" "$BackupPath/tmac/"
      fi
    fi
  elif [ "$BuildTarget" == "macstore" ]; then
    echo "Copying $BinaryName.app to deploy/$AppVersionStrMajor/$AppVersionStr..";
    mkdir "$DeployPath"
    cp -r "$ReleasePath/$BinaryName.app" "$DeployPath/"
    mv "$ReleasePath/$BinaryName.pkg" "$DeployPath/"
    rm "$ReleasePath/$BinaryName.app/Contents/MacOS/$BinaryName"
  fi
fi

echo "Version $AppVersionStrFull is ready!";
echo -en "\007";
sleep 1;
echo -en "\007";
sleep 1;
echo -en "\007";
