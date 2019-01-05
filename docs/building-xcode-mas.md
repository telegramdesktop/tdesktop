## Build instructions for Xcode 10.1

**NB** These are used for Mac App Store build, after the [Building using Xcode][xcode] instructions.

### Prepare breakpad

Go to ***BuildPath*** and run

    MACOSX_DEPLOYMENT_TARGET=10.8

    cd Libraries

    git clone https://chromium.googlesource.com/breakpad/breakpad
    cd breakpad
    git checkout bc8fb886
    git clone https://chromium.googlesource.com/linux-syscall-support src/third_party/lss
    cd src/third_party/lss
    git checkout a91633d1
    cd ../../..
    git apply ../../tdesktop/Telegram/Patches/breakpad.diff
    cd src/client/mac
    xcodebuild -project Breakpad.xcodeproj -target Breakpad -configuration Debug build
    xcodebuild -project Breakpad.xcodeproj -target Breakpad -configuration Release build
    cd ../../tools/mac/dump_syms
    xcodebuild -project dump_syms.xcodeproj -target dump_syms -configuration Release build
    cd ../../../../..

[xcode]: building-xcode.md
