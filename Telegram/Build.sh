while IFS='' read -r line || [[ -n "$line" ]]; do
  set $line
  eval $1="$2"
done < Version

AppVersionStrFull="$AppVersionStr"
DevParam=''
if [ "$DevChannel" != "0" ]; then
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
if [ "$BuildTarget" == "linux" ]; then
  echo "Building version $AppVersionStrFull for Linux 64bit.."
  UpdateFile="tlinuxupd$AppVersion"
  SetupFile="tsetup.$AppVersionStrFull.tar.xz"
  ReleasePath="./../Linux/Release"
elif [ "$BuildTarget" == "linux32" ]; then
  echo "Building version $AppVersionStrFull for Linux 32bit.."
  UpdateFile="tlinux32upd$AppVersion"
  SetupFile="tsetup32.$AppVersionStrFull.tar.xz"
  ReleasePath="./../Linux/Release"
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
  if [ -d "$ReleasePath/deploy/$AppVersionStrMajor/$AppVersionStr.dev" ]; then
    echo "Deploy folder for version $AppVersionStr.dev already exists!"
    exit 1
  fi

  if [ -d "$ReleasePath/deploy/$AppVersionStrMajor/$AppVersionStr" ]; then
    echo "Deploy folder for version $AppVersionStr already exists!"
    exit 1
  fi

  if [ -f "$ReleasePath/$UpdateFile" ]; then
    echo "Update file for version $AppVersion already exists!"
    exit 1
  fi

  DeployPath="$ReleasePath/deploy/$AppVersionStrMajor/$AppVersionStrFull"
#fi

if [ "$BuildTarget" == "linux" ] || [ "$BuildTarget" == "linux32" ]; then
  if [ ! -f "$ReleasePath/Telegram" ]; then
    echo "Telegram not found!"
    exit 1
  fi

  if [ ! -f "$ReleasePath/Updater" ]; then
    echo "Updater not found!"
    exit 1
  fi

  echo "Preparing version $AppVersionStrFull, executing Packer.."
  cd $ReleasePath && ./Packer -path Telegram -path Updater -version $AppVersion $DevParam && cd ./../../Telegram
  echo "Packer done!"

  if [ ! -d "$ReleasePath/deploy" ]; then
    mkdir "$ReleasePath/deploy"
  fi

  if [ ! -d "$ReleasePath/deploy/$AppVersionStrMajor" ]; then
    mkdir "$ReleasePath/deploy/$AppVersionStrMajor"
  fi

  echo "Copying Telegram, Updater and $UpdateFile to deploy/$AppVersionStrMajor/$AppVersionStrFull..";
  mkdir "$DeployPath"
  mkdir "$DeployPath/Telegram"
  mv $ReleasePath/Telegram $DeployPath/Telegram/
  mv $ReleasePath/Updater $DeployPath/Telegram/
  mv $ReleasePath/$UpdateFile $DeployPath/
  cd $DeployPath && tar -cJvf $SetupFile Telegram/ && cd ./../../../../../Telegram
fi

if [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "mac32" ] || [ "$BuildTarget" == "macstore" ]; then

  touch ./SourceFiles/telegram.qrc or exit 1
  xcodebuild -project Telegram.xcodeproj -alltargets -configuration Release build or exit 1

  if [ ! -d "$ReleasePath/$BinaryName.app" ]; then
    echo "$BinaryName.app not found!"
    exit 1
  fi

  if [ ! -d "$ReleasePath/$BinaryName.app.dSYM" ]; then
    echo "$BinaryName.app.dSYM not found!"
    exit 1
  fi

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

  if [ "$BuildTarget" == "mac" ] || [ "$BuildTarget" == "mac32" ]; then
    cd $ReleasePath
    temppath=`hdiutil attach -readwrite tsetup.dmg | awk -F "\t" 'END {print $3}'`
    cp -R "./$BinaryName.app" "$temppath/"
    bless --folder "$temppath/" --openfolder "$temppath/"
    hdiutil detach "$temppath"
    hdiutil convert tsetup.dmg -format UDZO -imagekey zlib-level=9 -ov -o $SetupFile
    cd ./../../Telegram
    cd $ReleasePath && ./Packer.app/Contents/MacOS/Packer -path "$BinaryName.app" -version $AppVersion $DevParam && cd ./../../Telegram
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
    cp -r "$ReleasePath/$BinaryName.app" $DeployPath/Telegram/
    mv "$ReleasePath/$BinaryName.app.dSYM" $DeployPath/
    rm "$ReleasePath/$BinaryName.app/Contents/MacOS/$BinaryName"
    rm "$ReleasePath/$BinaryName.app/Contents/Frameworks/Updater"
    rm -rf "$ReleasePath/$BinaryName.app/Contents/_CodeSignature"
    mv $ReleasePath/$UpdateFile $DeployPath/
    mv $ReleasePath/$SetupFile $DeployPath/

    if [ "$BuildTarget" == "mac32" ]; then
      ReleaseToPath="./../../../TBuild/tother/tmac32"
      DeployToPath="$ReleaseToPath/$AppVersionStrMajor/$AppVersionStrFull"
      if [ ! -d "$ReleaseToPath/$AppVersionStrMajor" ]; then
        mkdir "$ReleaseToPath/$AppVersionStrMajor"
      fi

      if [ ! -d "$DeployToPath" ]; then
        mkdir "$DeployToPath"
      fi

      cp -v $DeployPath/$UpdateFile $DeployToPath/
      cp -v $DeployPath/$SetupFile $DeployToPath/
      cp -rv $DeployPath/$BinaryName.app.dSYM $DeployToPath/
    fi
  elif [ "$BuildTarget" == "macstore" ]; then
    echo "Copying $BinaryName.app to deploy/$AppVersionStrMajor/$AppVersionStr..";
    mkdir "$DeployPath"
    cp -r "$ReleasePath/$BinaryName.app" $DeployPath/
    mv "$ReleasePath/$BinaryName.pkg" $DeployPath/
    mv "$ReleasePath/$BinaryName.app.dSYM" $DeployPath/
    rm "$ReleasePath/$BinaryName.app/Contents/MacOS/$BinaryName"
    rm -rf "$ReleasePath/$BinaryName.app/Contents/_CodeSignature"

    cp -v "$DeployPath/$BinaryName.app" "$DropboxDeployPath/"
    cp -rv "$DeployPath/$BinaryName.app.dSYM" "$DropboxDeployPath/"
  fi
fi

echo "Version $AppVersionStrFull is ready!";
