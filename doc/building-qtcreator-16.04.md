##Build instructions for Qt Creator 4.0.3 under Ubuntu 16.04

###Prepare

* Read [Build instructions for Qt Creator 3.5.1 under Ubuntu 12.04](https://github.com/telegramdesktop/tdesktop/blob/master/doc/building-qtcreator.md) first, and the following instructions is modified by it
* Install git by command **sudo apt-get install git** in Terminal
* Install g++ by command **sudo apt-get install g++** in Terminal
* Install Qt Creator from [**Downloads page**](https://www.qt.io/download/)

###Prepare folder

Choose a folder for the future build, for example **/home/user/TBuild** There you will have two folders, **Libraries** for third-party libs and **tdesktop** (or **tdesktop-master**) for the app.

###Clone source code

By git – in Terminal go to **/home/user/TBuild** and run

    git clone https://github.com/telegramdesktop/tdesktop.git

or download in ZIP and extract to **/home/user/TBuild** rename **tdesktop-master** to **tdesktop** to have **/home/user/TBuild/tdesktop/Telegram/Telegram.pro** project

###Prepare libraries

####Install dev libraries

    sudo apt-get install libexif-dev liblzma-dev libz-dev libssl-dev libappindicator-dev libunity-dev zlib1g-dev install libopus-dev

####FFmpeg

In Terminal go to **/home/user/TBuild/Libraries** and run

    git clone git://anongit.freedesktop.org/git/libva
    cd libva
    ./autogen.sh --enable-static
    make
    sudo make install
    cd ..

    git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg
    cd ffmpeg
    git checkout release/3.1

    sudo apt-get update
    sudo apt-get -y --force-yes install autoconf automake build-essential libass-dev libfreetype6-dev libgpac-dev libsdl1.2-dev libtheora-dev libtool libva-dev libvdpau-dev libvorbis-dev libxcb1-dev libxcb-shm0-dev libxcb-xfixes0-dev pkg-config texi2html zlib1g-dev
    sudo apt-get install yasm

    ./configure --prefix=/usr/local --disable-programs --disable-doc --disable-everything --enable-protocol=file --enable-libopus --enable-decoder=aac --enable-decoder=aac_latm --enable-decoder=aasc --enable-decoder=flac --enable-decoder=gif --enable-decoder=h264 --enable-decoder=h264_vdpau --enable-decoder=mp1 --enable-decoder=mp1float --enable-decoder=mp2 --enable-decoder=mp2float --enable-decoder=mp3 --enable-decoder=mp3adu --enable-decoder=mp3adufloat --enable-decoder=mp3float --enable-decoder=mp3on4 --enable-decoder=mp3on4float --enable-decoder=mpeg4 --enable-decoder=mpeg4_vdpau --enable-decoder=msmpeg4v2 --enable-decoder=msmpeg4v3 --enable-decoder=opus --enable-decoder=vorbis --enable-decoder=wavpack --enable-decoder=wmalossless --enable-decoder=wmapro --enable-decoder=wmav1 --enable-decoder=wmav2 --enable-decoder=wmavoice --enable-encoder=libopus --enable-hwaccel=h264_vaapi --enable-hwaccel=h264_vdpau --enable-hwaccel=mpeg4_vaapi --enable-hwaccel=mpeg4_vdpau --enable-parser=aac --enable-parser=aac_latm --enable-parser=flac --enable-parser=h264 --enable-parser=mpeg4video --enable-parser=mpegaudio --enable-parser=opus --enable-parser=vorbis --enable-demuxer=aac --enable-demuxer=flac --enable-demuxer=gif --enable-demuxer=h264 --enable-demuxer=mov --enable-demuxer=mp3 --enable-demuxer=ogg --enable-demuxer=wav --enable-muxer=ogg --enable-muxer=opus

    make
    sudo make install

####Install dev libraries (continue)
  
    sudo apt-get install portaudio19-dev libopenal-dev libxkbcommon-dev

####Qt 5.6.0, slightly patched

In Terminal go to **/home/user/TBuild/Libraries** and run

    git clone git://code.qt.io/qt/qt5.git qt5_6_0
    cd qt5_6_0
    git checkout 5.6
    perl init-repository --module-subset=qtbase,qtimageformats
    git checkout v5.6.0
    cd qtimageformats && git checkout v5.6.0 && cd ..
    cd qtbase && git checkout v5.6.0 && cd ..

#####Apply the patch

    cd qtbase && git apply ../../../tdesktop/Telegram/Patches/qtbase_5_6_0.diff && cd ..

#####Building library

Install some packages for Qt (see **/home/user/TBuild/Libraries/qt5_6_0/qtbase/src/plugins/platforms/xcb/README**)

    sudo apt-get install libxcb1-dev libxcb-image0-dev libxcb-keysyms1-dev libxcb-icccm4-dev libxcb-render-util0-dev libxcb-util0-dev libxrender-dev libasound-dev libpulse-dev libxcb-sync0-dev libxcb-xfixes0-dev libxcb-randr0-dev libx11-xcb-dev libffi-dev

In Terminal go to **/home/user/TBuild/Libraries/qt5_6_0** and there run

    OPENSSL_LIBS='-L-L/usr/lib/x86_64-linux-gnu -lssl -lcrypto' ./configure -prefix "/usr/local/tdesktop/Qt-5.6.0" -release -force-debug-info -opensource -confirm-license -qt-zlib -qt-libpng -qt-libjpeg -qt-freetype -qt-harfbuzz -qt-pcre -qt-xcb -qt-xkbcommon-x11 -no-opengl -no-gtkstyle -static -openssl-linked -nomake examples -nomake tests
    make -j4
    sudo make install

building (**make** command) will take really long time.

####Google Breakpad

In Terminal go to **/home/user/TBuild/Libraries** and run

    git clone https://chromium.googlesource.com/breakpad/breakpad
    git clone https://chromium.googlesource.com/linux-syscall-support breakpad/src/third_party/lss
    cd breakpad
    ./configure
    make
    sudo make install

###Building Telegram codegen utilities

In Terminal go to **/home/user/TBuild/tdesktop** and run

    mkdir -p Linux/obj/codegen_style/Debug
    cd Linux/obj/codegen_style/Debug
    /usr/local/tdesktop/Qt-5.6.0/bin/qmake CONFIG+=debug ../../../../Telegram/build/qmake/codegen_style/codegen_style.pro
    make
    mkdir -p ../../codegen_numbers/Debug
    cd ../../codegen_numbers/Debug
    /usr/local/tdesktop/Qt-5.6.0/bin/qmake CONFIG+=debug ../../../../Telegram/build/qmake/codegen_numbers/codegen_numbers.pro
    make

###Building Telegram Desktop

* Launch Qt Creator, all projects will be taken from **/home/user/TBuild/tdesktop/Telegram**
* Tools > Options > Build & Run > Qt Versions tab > Add > File System /usr/local/tdesktop/Qt-5.6.0/bin/qmake > **Qt 5.6.0 (Qt-5.6.0)** > Apply
* Tools > Options > Build & Run > Kits tab > Desktop (default) > change **Qt version** to **Qt 5.6.0 (Qt-5.6.0)** > Apply
* Open MetaLang.pro, configure project with paths **/home/user/TBuild/tdesktop/Linux/DebugIntermediateLang** and **/home/user/TBuild/tdesktop/Linux/ReleaseIntermediateLang** and build for Debug
* Open Telegram.pro, configure project with paths **/home/user/TBuild/tdesktop/Linux/DebugIntermediate** and **/home/user/TBuild/tdesktop/Linux/ReleaseIntermediate**
* Modify Telegram.pro
   * find `#xkbcommon` and uncomment it
   * find `LIBS += /usr/local/lib/libxkbcommon.a` and comment (#) it
* Build Telegram.pro for Debug, if GeneratedFiles are not found click **Run qmake** from **Build** menu and try again
* Open Updater.pro, configure project with paths **/home/user/TBuild/tdesktop/Linux/DebugIntermediateUpdater** and **/home/user/TBuild/tdesktop/Linux/ReleaseIntermediateUpdater** and build for Debug
* Release Telegram build will require removing **CUSTOM_API_ID** definition in Telegram.pro project and may require changing paths in **/home/user/TBuild/tdesktop/Telegram/FixMake.sh** or **/home/user/TBuild/tdesktop/Telegram/FixMake32.sh** for static library linking fix, static linking applies only on second Release build (first uses old Makefile)

###Modify Font Setting

Because there are more [issues](https://github.com/telegramdesktop/tdesktop/issues?utf8=%E2%9C%93&q=is%3Aissue%20is%3Aopen%20font) for changing fonts, and there is no improvement for this, so the following is the dirty methods for changing fonts settings.

####Change Fonts

* Prepare 3 ttf fonts which you want to change for, one for reqular (let it as `a-regular.ttf`), another for semibold (let it as `a-semibold.ttf`), and the last for bold (let it as `a-bold.ttf`)
* Find the ttf\`s font family name (let them as `a`, `a semibold`, and `a bold`)
* Open Telegram.pro and modify it
   * Delete resources for origional fonts, at Resource > Resources > telegram.qrc > /gui > art > OpenSans-Regular.ttf, OpenSans-Bold.ttf, OpenSans-Semibold.ttf
   * Modify Source Code > SourceFiles > ui > twidget.cpp, it will make Telegram load the fonts which are the same path as Telegram app.
       
       ```qt
          //QFontDatabase::addApplicationFont(qsl(":/gui/art/fonts/OpenSans-Regular.ttf"));
          //QFontDatabase::addApplicationFont(qsl(":/gui/art/fonts/OpenSans-Bold.ttf"));
          //QFontDatabase::addApplicationFont(qsl(":/gui/art/fonts/OpenSans-Semibold.ttf"));
            QFontDatabase::addApplicationFont("./a-regular.ttf");
            QFontDatabase::addApplicationFont("./a-bold.ttf");
            QFontDatabase::addApplicationFont("./a-semibold.ttf");
       ```  
   * Replace all project files for `Open Sans SemiBold` to `a semibold`
   * Replace all project files for `Open Sans` to `a`
* Rebuild Telegram.pro 
