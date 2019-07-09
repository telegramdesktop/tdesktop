Building via qmake
==================

**NB** These are outdated, please refer to [Building using GYP/CMake][cmake] instructions.

The following commands assume the following environment variables are set:

 * `$srcdir`: The directory into which the source has been downloaded and
   unpacked.
 * `_qtver`: The Qt version being used (eg: `5.6.2`).
 * `$pkgdir`: The directory into which installable files are places. This is
   `/` for local installations, or can be different directory when preparing a
   redistributable package.

Either set them accordingly, or replace them in the below commands as desired.

The following sources should be downloaded and unpacked into `$srcdir`:

  * This repository (either `master` or a specific tag).
  * `git clone git://code.qt.io/qt/qt5.git`
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

    local qt_patch_file="$srcdir/tdesktop/Telegram/Patches/qtbase_${_qtver//./_}.diff"
    local qt_dir="$srcdir/Libraries/qt${_qtver//./_}"
    if [ "$qt_patch_file" -nt "$qt_dir" ]; then
      rm -rf "$qt_dir"
      git clone git://code.qt.io/qt/qt5.git
      cd "$qt_dir"
      perl init-repository --module-subset=qtbase,qtimageformats
      git checkout v$_qtver
      cd qtimageformats
      git checkout v$_qtver
      cd ../qtbase
      git checkout v$_qtver
      git apply "$qt_patch_file"
    fi

    if [ ! -h "$srcdir/Libraries/breakpad" ]; then
      ln -s "$srcdir/breakpad" "$srcdir/Libraries/breakpad"
      ln -s "$srcdir/breakpad-lss" "$srcdir/Libraries/breakpad/src/third_party/lss"
    fi

    sed -i 's/CUSTOM_API_ID//g' "$srcdir/tdesktop/Telegram/Telegram.pro"

    (
      echo "DEFINES += TDESKTOP_DISABLE_AUTOUPDATE"
      echo "DEFINES += TDESKTOP_DISABLE_REGISTER_CUSTOM_SCHEME"
    ) >> "$srcdir/tdesktop/Telegram/Telegram.pro"

Building
--------


    # Build patched Qt
    cd "$qtdir"
    ./configure -prefix "$srcdir/qt" -release -opensource -confirm-license -qt-zlib \
                -qt-libpng -qt-libjpeg -qt-freetype -qt-harfbuzz -qt-pcre -qt-xcb \
                -qt-xkbcommon-x11 -no-opengl -no-gtkstyle -static -nomake examples -nomake tests
    make module-qtbase module-qtimageformats
    make module-qtbase-install_subtargets module-qtimageformats-install_subtargets

    export PATH="$srcdir/qt/bin:$PATH"

    # Build breakpad
    cd "$srcdir/Libraries/breakpad"
    ./configure
    make

    # Build codegen_style
    mkdir -p "$srcdir/tdesktop/Linux/obj/codegen_style/Debug"
    cd "$srcdir/tdesktop/Linux/obj/codegen_style/Debug"
    qmake CONFIG+=debug ../../../../Telegram/build/qmake/codegen_style/codegen_style.pro
    make

    # Build codegen_numbers
    mkdir -p "$srcdir/tdesktop/Linux/obj/codegen_numbers/Debug"
    cd "$srcdir/tdesktop/Linux/obj/codegen_numbers/Debug"
    qmake CONFIG+=debug ../../../../Telegram/build/qmake/codegen_numbers/codegen_numbers.pro
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
[cmake]: building-cmake.md
