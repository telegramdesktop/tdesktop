set -e
FullExecPath=$PWD
pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

if [ ! -d "$FullScriptPath/../../../DesktopPrivate" ]; then
  echo ""
  echo "This script is for building the production version of Telegram Desktop."
  echo ""
  echo "For building custom versions please visit the build instructions page at:"
  echo "https://github.com/telegramdesktop/tdesktop/#build-instructions"
  exit
fi

if [ "$1" == "request_uuid" ]; then
  if [ "$2" != "" ]; then
    NotarizeRequestId="$2"
  fi
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
elif [ "$BuildTarget" == "linux32" ]; then
  echo "Building version $AppVersionStrFull for Linux 32bit.."
  UpdateFile="tlinux32upd$AppVersion"
  SetupFile="tsetup32.$AppVersionStrFull.tar.xz"
  ProjectPath="$HomePath/../out"
  ReleasePath="$ProjectPath/Release"
  BinaryName="Telegram"
elif [ "$BuildTarget" == "mac" ]; then
  echo "Building version $AppVersionStrFull for macOS 10.12+.."
  if [ "$AC_USERNAME" == "" ]; then
    Error "AC_USERNAME not found!"
  fi
  UpdateFile="tmacupd$AppVersion"
  SetupFile="tsetup.$AppVersionStrFull.dmg"
  ProjectPath="$HomePath/../out"
  ReleasePath="$ProjectPath/Release"
  BinaryName="Telegram"
elif [ "$BuildTarget" == "osx" ]; then
  echo "Building version $AppVersionStrFull for OS X 10.10 and 10.11.."
  UpdateFile="tosxupd$AppVersion"
  SetupFile="tsetup-osx.$AppVersionStrFull.dmg"
  ProjectPath="$HomePath/../out"
  ReleasePath="$ProjectPath/Release"
  BinaryName="Telegram"
elif [ "$BuildTarget" == "macstore" ]; then
  if [ "$AlphaVersion" != "0" ]; then
    Error "Can't build macstore alpha version!"
  fi

  echo "Building version $AppVersionStrFull for Mac App Store.."
  ProjectPath="$HomePath/../out"
  ReleasePath="$ProjectPath/Release"
  BinaryName="Telegram Lite"
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

if [ "$BuildTarget" == "linux" ] || [ "$BuildTarget" == "linux32" ]; then

  DropboxSymbolsPath="/media/psf/Dropbox/Telegram/symbols"
  if [ ! -d "$DropboxSymbolsPath" ]; then
    Error "Dropbox path not found!"
  fi

  BackupPath="/media/psf/backup/tdesktop/$AppVersionStrMajor/$AppVersionStrFull/t$BuildTarget"
  if [ ! -d "/media/psf/backup/tdesktop" ]; then
    Error "Backup folder not found!"
  fi

  ./build/docker/centos_env/run.sh /usr/src/tdesktop/Telegram/build/docker/build.sh

  echo "Copying from docker result folder."
  cp "$ReleasePath/root/$BinaryName" "$ReleasePath/$BinaryName"
  cp "$ReleasePath/root/$BinaryName.sym" "$ReleasePath/$BinaryName.sym"
  cp "$ReleasePath/root/Updater" "$ReleasePath/Updater"
  cp "$ReleasePath/root/Packer" "$ReleasePath/Packer"

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

if [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "osx" ] || [ "$BuildTarget" == "macstore" ]; then

  DropboxSymbolsPath="$HOME/Dropbox/Telegram/symbols"
  if [ ! -d "$DropboxSymbolsPath" ]; then
    Error "Dropbox path not found!"
  fi

  BackupPath="$HOME/Projects/backup/tdesktop/$AppVersionStrMajor/$AppVersionStrFull"
  if [ ! -d "$HOME/Projects/backup/tdesktop" ]; then
    Error "Backup path not found!"
  fi

  if [ "$NotarizeRequestId" == "" ]; then
    if [ -f "$ReleasePath/$BinaryName.app/Contents/Info.plist" ]; then
      rm "$ReleasePath/$BinaryName.app/Contents/Info.plist"
    fi
    if [ -f "$ProjectPath/Telegram/CMakeFiles/Telegram.dir/Info.plist" ]; then
      rm "$ProjectPath/Telegram/CMakeFiles/Telegram.dir/Info.plist"
    fi
    rm -rf "$ReleasePath/$BinaryName.app/Contents/_CodeSignature"

    ./configure.sh

    cd $ProjectPath
    cmake --build . --config Release --target Telegram

    cd $ReleasePath

    if [ ! -d "$ReleasePath/$BinaryName.app" ]; then
      Error "$BinaryName.app not found!"
    fi

    if [ ! -d "$ReleasePath/$BinaryName.app.dSYM" ]; then
      Error "$BinaryName.app.dSYM not found!"
    fi

    if [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "osx" ]; then
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
    "$HomePath/../../Libraries/macos/breakpad/src/tools/mac/dump_syms/build/Release/dump_syms" "$ReleasePath/$BinaryName.app.dSYM" > "$ReleasePath/$BinaryName.sym" 2>/dev/null
    echo "Done!"

    echo "Stripping the executable.."
    strip "$ReleasePath/$BinaryName.app/Contents/MacOS/$BinaryName"
    echo "Done!"

    echo "Signing the application.."
    if [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "osx" ]; then
      codesign --force --deep --timestamp --options runtime --sign "Developer ID Application: John Preston" "$ReleasePath/$BinaryName.app" --entitlements "$HomePath/Telegram/Telegram.entitlements"
    elif [ "$BuildTarget" == "macstore" ]; then
      codesign --force --deep --sign "3rd Party Mac Developer Application: Telegram FZ-LLC (C67CF9S4VU)" "$ReleasePath/$BinaryName.app" --entitlements "$HomePath/Telegram/Telegram Lite.entitlements"
      echo "Making an installer.."
      productbuild --sign "3rd Party Mac Developer Installer: Telegram FZ-LLC (C67CF9S4VU)" --component "$ReleasePath/$BinaryName.app" /Applications "$ReleasePath/$BinaryName.pkg"
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

    if [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "osx" ]; then
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
  fi

  if [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "osx" ]; then
    cd "$ReleasePath"

    if [ "$NotarizeRequestId" == "" ]; then
      if [ "$AlphaVersion" == "0" ]; then
        cp -f tsetup_template.dmg tsetup.temp.dmg
        TempDiskPath=`hdiutil attach -nobrowse -noautoopenrw -readwrite tsetup.temp.dmg | awk -F "\t" 'END {print $3}'`
        cp -R "./$BinaryName.app" "$TempDiskPath/"
        bless --folder "$TempDiskPath/" --openfolder "$TempDiskPath/"
        hdiutil detach "$TempDiskPath"
        hdiutil convert tsetup.temp.dmg -format UDZO -imagekey zlib-level=9 -ov -o "$SetupFile"
        rm tsetup.temp.dmg
      fi
    fi

    if [ "$AlphaVersion" != "0" ]; then
      "./Packer" -path "$BinaryName.app" -target "$BuildTarget" -version $VersionForPacker $AlphaBetaParam -alphakey

      if [ ! -f "$ReleasePath/$AlphaKeyFile" ]; then
        Error "Alpha version key file not found!"
      fi

      while IFS='' read -r line || [[ -n "$line" ]]; do
        AlphaSignature="$line"
      done < "$ReleasePath/$AlphaKeyFile"

      UpdateFile="${UpdateFile}_${AlphaSignature}"
      SetupFile="talpha${AlphaVersion}_${AlphaSignature}.zip"

      if [ "$NotarizeRequestId" == "" ]; then
        rm -rf "$ReleasePath/AlphaTemp"
        mkdir "$ReleasePath/AlphaTemp"
        mkdir "$ReleasePath/AlphaTemp/$BinaryName"
        cp -r "$ReleasePath/$BinaryName.app" "$ReleasePath/AlphaTemp/$BinaryName/"
        cd "$ReleasePath/AlphaTemp"
        zip -r "$SetupFile" "$BinaryName"
        mv "$SetupFile" "$ReleasePath/"
        cd "$ReleasePath"
      fi
    fi
    if [ "$BuildTarget" == "mac" ]; then
      if [ "$NotarizeRequestId" == "" ]; then
        echo "Beginning notarization process."
        set +e
        xcrun altool --notarize-app --primary-bundle-id "com.tdesktop.Telegram" --username "$AC_USERNAME" --password "@keychain:AC_PASSWORD" --file "$SetupFile" > request_uuid.txt
        set -e
        while IFS='' read -r line || [[ -n "$line" ]]; do
          Prefix=$(echo $line | cut -d' ' -f 1)
          Value=$(echo $line | cut -d' ' -f 3)
          if [ "$Prefix" == "RequestUUID" ]; then
            RequestUUID=$Value
          fi
        done < "request_uuid.txt"
        if [ "$RequestUUID" == "" ]; then
          cat request_uuid.txt
          Error "Could not extract Request UUID."
        fi
        echo "Request UUID: $RequestUUID"
        rm request_uuid.txt
      else
        RequestUUID=$NotarizeRequestId
        echo "Continue notarization process with Request UUID: $RequestUUID"
      fi

      RequestStatus=
      LogFile=
      while [[ "$RequestStatus" == "" ]]; do
        sleep 5
        xcrun altool --notarization-info "$RequestUUID" --username "$AC_USERNAME" --password "@keychain:AC_PASSWORD" > request_result.txt
        while IFS='' read -r line || [[ -n "$line" ]]; do
          Prefix=$(echo $line | cut -d' ' -f 1)
          Value=$(echo $line | cut -d' ' -f 2)
          if [ "$Prefix" == "LogFileURL:" ]; then
            LogFile=$Value
          fi
          if [ "$Prefix" == "Status:" ]; then
            if [ "$Value" == "in" ]; then
              echo "In progress..."
            else
              RequestStatus=$Value
              echo "Status: $RequestStatus"
            fi
          fi
        done < "request_result.txt"
      done
      if [ "$RequestStatus" != "success" ]; then
        echo "Notarization problems, response:"
        cat request_result.txt
        if [ "$LogFile" != "" ]; then
          echo "Requesting log: $LogFile"
          curl $LogFile
        fi
        Error "Notarization FAILED."
      fi
      rm request_result.txt

      if [ "$LogFile" != "" ]; then
        echo "Requesting log: $LogFile"
        curl $LogFile > request_log.txt
      fi

      xcrun stapler staple "$ReleasePath/$BinaryName.app"

      if [ "$AlphaVersion" != "0" ]; then
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
    fi

    "./Packer" -path "$BinaryName.app" -target "$BuildTarget" -version $VersionForPacker $AlphaBetaParam
    echo "Packer done!"
  fi

  if [ ! -d "$ReleasePath/deploy" ]; then
    mkdir "$ReleasePath/deploy"
  fi

  if [ ! -d "$ReleasePath/deploy/$AppVersionStrMajor" ]; then
    mkdir "$ReleasePath/deploy/$AppVersionStrMajor"
  fi

  if [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "osx" ]; then
    echo "Copying $BinaryName.app and $UpdateFile to deploy/$AppVersionStrMajor/$AppVersionStr..";
    mkdir "$DeployPath"
    mkdir "$DeployPath/$BinaryName"
    cp -r "$ReleasePath/$BinaryName.app" "$DeployPath/$BinaryName/"
    if [ "$AlphaVersion" != "0" ]; then
      mv "$ReleasePath/$AlphaKeyFile" "$DeployPath/"
    fi
    mv "$ReleasePath/$BinaryName.app.dSYM" "$DeployPath/"
    rm "$ReleasePath/$BinaryName.app/Contents/MacOS/$BinaryName"
    rm "$ReleasePath/$BinaryName.app/Contents/Frameworks/Updater"
    mv "$ReleasePath/$UpdateFile" "$DeployPath/"
    mv "$ReleasePath/$SetupFile" "$DeployPath/"

    if [ "$BuildTarget" == "mac" ]; then
      mkdir -p "$BackupPath/tmac"
      cp "$DeployPath/$UpdateFile" "$BackupPath/tmac/"
      cp "$DeployPath/$SetupFile" "$BackupPath/tmac/"
      if [ "$AlphaVersion" != "0" ]; then
        cp -v "$DeployPath/$AlphaKeyFile" "$BackupPath/tmac/"
      fi
    fi
    if [ "$BuildTarget" == "osx" ]; then
      mkdir -p "$BackupPath/tosx"
      cp "$DeployPath/$UpdateFile" "$BackupPath/tosx/"
      cp "$DeployPath/$SetupFile" "$BackupPath/tosx/"
      if [ "$AlphaVersion" != "0" ]; then
        cp -v "$DeployPath/$AlphaKeyFile" "$BackupPath/tosx/"
      fi
    fi
  elif [ "$BuildTarget" == "macstore" ]; then
    echo "Copying $BinaryName.app to deploy/$AppVersionStrMajor/$AppVersionStr..";
    mkdir "$DeployPath"
    cp -r "$ReleasePath/$BinaryName.app" "$DeployPath/"
    mv "$ReleasePath/$BinaryName.pkg" "$DeployPath/"
    mv "$ReleasePath/$BinaryName.app.dSYM" "$DeployPath/"
    rm "$ReleasePath/$BinaryName.app/Contents/MacOS/$BinaryName"
  fi
fi

echo "Version $AppVersionStrFull is ready!";
echo -en "\007";
sleep 1;
echo -en "\007";
sleep 1;
echo -en "\007";

if [ "$BuildTarget" == "mac" ]; then
  if [ -f "$ReleasePath/request_log.txt" ]; then
    DisplayingLog=
    while IFS='' read -r line || [[ -n "$line" ]]; do
      if [ "$DisplayingLog" == "1" ]; then
        echo $line
      else
        Prefix=$(echo $line | cut -d' ' -f 1)
        Value=$(echo $line | cut -d' ' -f 2)
        if [ "$Prefix" == '"issues":' ]; then
          if [ "$Value" != "null" ]; then
            echo "NB! Notarization log issues:"
            echo $line
            DisplayingLog=1
          else
            DisplayingLog=0
          fi
        fi
      fi
    done < "$ReleasePath/request_log.txt"
    if [ "$DisplayingLog" != "0" ] && [ "$DisplayingLog" != "1" ]; then
      echo "NB! Notarization issues not found:"
      cat "$ReleasePath/request_log.txt"
    else
      rm "$ReleasePath/request_log.txt"
    fi
  else
    echo "NB! Notarization log not found :("
  fi
fi
