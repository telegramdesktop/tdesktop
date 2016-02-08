##Build instructions for Qt Creator 3.5.1 under Ubuntu 12.04

###Prepare

* Install git by command **sudo apt-get install git** in Terminal
* Install g++ by command **sudo apt-get install g++** in Terminal
* Install Qt Creator from [**Downloads page**](https://www.qt.io/download/)

For 32 bit Ubuntu you need to install g++ version 4.8 manually by such commands

* sudo add-apt-repository ppa:ubuntu-toolchain-r/test
* sudo apt-get update
* sudo apt-get install gcc-4.8 g++-4.8
* sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.8 20
* sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-4.8 20

###Prepare folder

Choose a folder for the future build, for example **/home/user/TBuild** There you will have two folders, **Libraries** for third-party libs and **tdesktop** (or **tdesktop-master**) for the app.

###Clone source code

By git – in Terminal go to **/home/user/TBuild** and run

    git clone https://github.com/telegramdesktop/tdesktop.git

or download in ZIP and extract to **/home/user/TBuild** rename **tdesktop-master** to **tdesktop** to have **/home/user/TBuild/tdesktop/Telegram/Telegram.pro** project

###Prepare libraries

Install dev libraries

    sudo apt-get install libexif-dev liblzma-dev libz-dev libssl-dev libappindicator-dev libunity-dev

####zlib 1.2.8

http://www.zlib.net/ > Download [**zlib source code, version 1.2.8, zipfile format**](http://zlib.net/zlib128.zip)

Extract to **/home/user/TBuild/Libraries**

#####Building library

In Terminal go to **/home/user/TBuild/Libraries/zlib-1.2.8** and run:

        ./configure
        make
        sudo make install

Install audio libraries

####Opus codec 1.1

Download [opus-1.1 sources](http://downloads.xiph.org/releases/opus/opus-1.1.tar.gz) from http://www.opus-codec.org/downloads, extract to **/home/user/TBuild/Libraries**, go to **/home/user/TBuild/Libraries/opus-1.1** and run

    ./configure
    make
    sudo make install

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
    git checkout release/2.8

    sudo apt-get update
    sudo apt-get -y --force-yes install autoconf automake build-essential libass-dev libfreetype6-dev libgpac-dev libsdl1.2-dev libtheora-dev libtool libva-dev libvdpau-dev libvorbis-dev libxcb1-dev libxcb-shm0-dev libxcb-xfixes0-dev pkg-config texi2html zlib1g-dev
    sudo apt-get install yasm

    ./configure --prefix=/usr/local --disable-programs --disable-doc --disable-pthreads --disable-mmx --disable-everything --enable-libopus --enable-decoder=aac --enable-decoder=aac_latm --enable-decoder=aasc --enable-decoder=flac --enable-decoder=gif --enable-decoder=h264 --enable-decoder=h264_vdpau --enable-decoder=mp1 --enable-decoder=mp1float --enable-decoder=mp2 --enable-decoder=mp2float --enable-decoder=mp3 --enable-decoder=mp3adu --enable-decoder=mp3adufloat --enable-decoder=mp3float --enable-decoder=mp3on4 --enable-decoder=mp3on4float --enable-decoder=mpeg4 --enable-decoder=mpeg4_vdpau --enable-decoder=msmpeg4v2 --enable-decoder=msmpeg4v3 --enable-decoder=opus --enable-decoder=vorbis --enable-decoder=wavpack --enable-decoder=wmalossless --enable-decoder=wmapro --enable-decoder=wmav1 --enable-decoder=wmav2 --enable-decoder=wmavoice --enable-encoder=libopus --enable-hwaccel=h264_vaapi --enable-hwaccel=h264_vdpau --enable-hwaccel=mpeg4_vaapi --enable-hwaccel=mpeg4_vdpau --enable-parser=aac --enable-parser=aac_latm --enable-parser=flac --enable-parser=h264 --enable-parser=mpeg4video --enable-parser=mpegaudio --enable-parser=opus --enable-parser=vorbis --enable-demuxer=aac --enable-demuxer=flac --enable-demuxer=gif --enable-demuxer=h264 --enable-demuxer=mov --enable-demuxer=mp3 --enable-demuxer=ogg --enable-demuxer=wav --enable-muxer=ogg --enable-muxer=opus

    make
    sudo make install

####PortAudio 19

[Download portaudio sources](http://www.portaudio.com/archives/pa_stable_v19_20140130.tgz) from **http://www.portaudio.com/download.html**, extract to **/home/user/TBuild/Libraries**, go to **/home/user/TBuild/Libraries/portaudio** and run

    ./configure
    make
    sudo make install

####OpenAL Soft

In Terminal go to **/home/user/TBuild/Libraries** and run

    git clone git://repo.or.cz/openal-soft.git

then go to **/home/user/TBuild/Libraries/openal-soft/build** and run

    sudo apt-get install cmake
    cmake -D LIBTYPE:STRING=STATIC ..
    make
    sudo make install

####libxkbcommon (required for Fcitx Qt plugin)

In Terminal go to **/home/user/TBuild/Libraries** and run

    sudo apt-get install xutils-dev bison python-xcbgen
    git clone https://github.com/xkbcommon/libxkbcommon.git
    cd libxkbcommon
    ./autogen.sh --disable-x11
    make
    sudo make install

####Qt 5.5.1, slightly patched

In Terminal go to **/home/user/TBuild/Libraries** and run

    git clone git://code.qt.io/qt/qt5.git QtStatic
    cd QtStatic
    git checkout 5.5
    perl init-repository --module-subset=qtbase,qtimageformats
    git checkout v5.5.1
    cd qtimageformats && git checkout v5.5.1 && cd ..
    cd qtbase && git checkout v5.5.1 && cd ..

#####Apply the patch

    cd qtbase && git apply ../../../tdesktop/Telegram/_qtbase_5_5_1_patch.diff && cd ..

#####Building library

Install some packages for Qt (see **/home/user/TBuild/Libraries/QtStatic/qtbase/src/plugins/platforms/xcb/README**)

    sudo apt-get install libxcb1-dev libxcb-image0-dev libxcb-keysyms1-dev libxcb-icccm4-dev libxcb-render-util0-dev libxcb-util0-dev libxrender-dev libasound-dev libpulse-dev libxcb-sync0-dev libxcb-xfixes0-dev libxcb-randr0-dev libx11-xcb-dev libffi-dev

In Terminal go to **/home/user/TBuild/Libraries/QtStatic** and there run

    ./configure -release -opensource -confirm-license -qt-zlib -qt-libpng -qt-libjpeg -qt-freetype -qt-harfbuzz -qt-pcre -qt-xcb -qt-xkbcommon-x11 -no-opengl -static -nomake examples -nomake tests
    make -j4
    sudo make -j4 install

building (**make** command) will take really long time.

####Google Breakpad

In Terminal go to **/home/user/TBuild/Libraries** and run

    git clone https://chromium.googlesource.com/breakpad/breakpad
    git clone https://chromium.googlesource.com/linux-syscall-support breakpad/src/third_party/lss
    cd breakpad
    ./configure
    make
    sudo make install

###Building Telegram Desktop

* Launch Qt Creator, all projects will be taken from **/home/user/TBuild/tdesktop/Telegram**
* Tools > Options > Build & Run > Qt Versions tab > Add > File System /usr/local/Qt-5.5.1/bin/qmake > **Qt 5.5.1 (Qt-5.5.1)** > Apply
* Tools > Options > Build & Run > Kits tab > Desktop (default) > change **Qt version** to **Qt 5.5.1 (Qt-5.5.1)** > Apply
* Open MetaStyle.pro, configure project with paths **/home/user/TBuild/tdesktop/Linux/DebugIntermediateStyle** and **/home/user/TBuild/tdesktop/Linux/ReleaseIntermediateStyle** and build for Debug
* Open MetaLang.pro, configure project with paths **/home/user/TBuild/tdesktop/Linux/DebugIntermediateLang** and **/home/user/TBuild/tdesktop/Linux/ReleaseIntermediateLang** and build for Debug
* Open Telegram.pro, configure project with paths **/home/user/TBuild/tdesktop/Linux/DebugIntermediate** and **/home/user/TBuild/tdesktop/Linux/ReleaseIntermediate** and build for Debug, if GeneratedFiles are not found click **Run qmake** from **Build** menu and try again
* Open Updater.pro, configure project with paths **/home/user/TBuild/tdesktop/Linux/DebugIntermediateUpdater** and **/home/user/TBuild/tdesktop/Linux/ReleaseIntermediateUpdater** and build for Debug
* Release Telegram build will require removing **CUSTOM_API_ID** definition in Telegram.pro project and may require changing paths in **/home/user/TBuild/tdesktop/Telegram/FixMake.sh** or **/home/user/TBuild/tdesktop/Telegram/FixMake32.sh** for static library linking fix, static linking applies only on second Release build (first uses old Makefile)
