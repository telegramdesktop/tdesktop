Building via qmake
==================

The following commands assume the following environment variables are set:

 * `$srcdir`: The directory into which the source has been downloaded and
   unpacked.
 * `_qtver`: The Qt version being used (eg: `5.5.1`).
 * `$pkgdir`: The directory into which installable files are places. This is
   `/` for local installations, or can be different directory when preparing a
   redistributable package.

Either set them accordingly, or replace them in the below commands as desired.

The following sources should be downloaded and unpacked into `$srcdir`:

  * This repository (either `master` or a specific tag).
  * The Qt sources: `http://download.qt-project.org/official_releases/qt/${_qtver%.*}/$_qtver/single/qt-everywhere-opensource-src-$_qtver.tar.xz`
  * `git clone git+https://chromium.googlesource.com/breakpad/breakpad breakpad`
  * `git clone git+https://chromium.googlesource.com/linux-syscall-support breakpad-lss`
  * telegramdesktop.desktop (The intention is to include this file inside the
    source package at some point):
    `https://aur.archlinux.org/cgit/aur.git/plain/telegramdesktop.desktop?h=telegram-desktop`
  * tg.protocol: `https://aur.archlinux.org/cgit/aur.git/plain/tg.protocol?h=telegram-desktop`

Preparation
-----------

    cd "$srcdir/tdesktop"
    
    mkdir -p "$srcdir/Libraries"
    
    local qt_patch_file="$srcdir/tdesktop/Telegram/_qtbase_${_qtver//./_}_patch.diff"
    if [ "$qt_patch_file" -nt "$srcdir/Libraries/QtStatic" ]; then
      rm -rf "$srcdir/Libraries/QtStatic"
      mv "$srcdir/qt-everywhere-opensource-src-$_qtver" "$srcdir/Libraries/QtStatic"
      cd "$srcdir/Libraries/QtStatic/qtbase"
      patch -p1 -i "$qt_patch_file"
    fi
    
    if [ ! -h "$srcdir/Libraries/breakpad" ]; then
      ln -s "$srcdir/breakpad" "$srcdir/Libraries/breakpad"
      ln -s "$srcdir/breakpad-lss" "$srcdir/Libraries/breakpad/src/third_party/lss"
    fi
    
    sed -i 's/CUSTOM_API_ID//g' "$srcdir/tdesktop/Telegram/Telegram.pro"
    sed -i 's,LIBS += /usr/local/lib/libxkbcommon.a,,g' "$srcdir/tdesktop/Telegram/Telegram.pro"
    sed -i 's,LIBS += /usr/local/lib/libz.a,LIBS += -lz,g' "$srcdir/tdesktop/Telegram/Telegram.pro"
    
    (
      echo "DEFINES += TDESKTOP_DISABLE_AUTOUPDATE"
      echo "DEFINES += TDESKTOP_DISABLE_REGISTER_CUSTOM_SCHEME"
      echo 'INCLUDEPATH += "/usr/lib/glib-2.0/include"'
      echo 'INCLUDEPATH += "/usr/lib/gtk-2.0/include"'
      echo 'INCLUDEPATH += "/usr/include/opus"'
    ) >> "$srcdir/tdesktop/Telegram/Telegram.pro"

Building
--------


    # Build patched Qt
    cd "$srcdir/Libraries/QtStatic"
    ./configure -prefix "$srcdir/qt" -release -opensource -confirm-license -qt-zlib \
                -qt-libpng -qt-libjpeg -qt-freetype -qt-harfbuzz -qt-pcre -qt-xcb \
                -qt-xkbcommon-x11 -no-opengl -static -nomake examples -nomake tests
    make module-qtbase module-qtimageformats
    make module-qtbase-install_subtargets module-qtimageformats-install_subtargets
    
    export PATH="$srcdir/qt/bin:$PATH"
    
    # Build breakpad
    cd "$srcdir/Libraries/breakpad"
    ./configure
    make
    
    # Build MetaStyle
    mkdir -p "$srcdir/tdesktop/Linux/DebugIntermediateStyle"
    cd "$srcdir/tdesktop/Linux/DebugIntermediateStyle"
    qmake CONFIG+=debug "../../Telegram/MetaStyle.pro"
    make
    
    # Build MetaLang
    mkdir -p "$srcdir/tdesktop/Linux/DebugIntermediateLang"
    cd "$srcdir/tdesktop/Linux/DebugIntermediateLang"
    qmake CONFIG+=debug "../../Telegram/MetaLang.pro"
    make
    
    # Build Telegram Desktop
    mkdir -p "$srcdir/tdesktop/Linux/ReleaseIntermediate"
    cd "$srcdir/tdesktop/Linux/ReleaseIntermediate"
    
    qmake CONFIG+=release "../../Telegram/Telegram.pro"
    local pattern="^PRE_TARGETDEPS +="
    grep "$pattern" "$srcdir/tdesktop/Telegram/Telegram.pro" | sed "s/$pattern//g" | xargs make
    
    qmake CONFIG+=release "../../Telegram/Telegram.pro"
    make

Installation
------------


    install -dm755 "$pkgdir/usr/bin"
    install -m755 "$srcdir/tdesktop/Linux/Release/Telegram" "$pkgdir/usr/bin/telegram-desktop"
    
    install -d "$pkgdir/usr/share/applications"
    install -m644 "$srcdir/telegramdesktop.desktop" "$pkgdir/usr/share/applications/telegramdesktop.desktop"
    
    install -d "$pkgdir/usr/share/kde4/services"
    install -m644 "$srcdir/tg.protocol" "$pkgdir/usr/share/kde4/services/tg.protocol"
    
    local icon_size icon_dir
    for icon_size in 16 32 48 64 128 256 512; do
      icon_dir="$pkgdir/usr/share/icons/hicolor/${icon_size}x${icon_size}/apps"
      
      install -d "$icon_dir"
      install -m644 "$srcdir/tdesktop/Telegram/SourceFiles/art/icon${icon_size}.png" "$icon_dir/telegram-desktop.png"
    done

Notes
-----

These instructions are based on the [ArchLinux package][arch-package] for
telegram-desktop.

In case these instructions are at some point out of date, the above may serve
as an update reference.

[arch-package]: https://aur.archlinux.org/packages/telegram-desktop/
