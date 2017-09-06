## Build instructions for GYP/CMake under Ubuntu 12.04

### Prepare

* Install git by command **sudo apt-get install git** in Terminal
* Install g++ by command **sudo apt-get install g++** in Terminal
* Install libtool and automake by command **sudo apt-get install libtool automake** in Terminal

You need to install g++ version 6 manually by such commands

* sudo add-apt-repository ppa:ubuntu-toolchain-r/test
* sudo apt-get update
* sudo apt-get install gcc-6 g++-6
* sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-6 60
* sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-6 60

### Prepare folder

Choose a folder for the future build, for example **/home/user/TBuild** There you will have two folders, **Libraries** for third-party libs and **tdesktop** (or **tdesktop-master**) for the app.

### Clone source code

By git – in Terminal go to **/home/user/TBuild** and run

    git clone --recursive https://github.com/telegramdesktop/tdesktop.git

### Prepare libraries

Install dev libraries

    sudo apt-get install libexif-dev liblzma-dev libz-dev libssl-dev libappindicator-dev libunity-dev libicu-dev libdee-dev

#### zlib

In Terminal go to **/home/user/TBuild/Libraries** and run

    git clone https://github.com/telegramdesktop/zlib.git
    cd zlib
    ./configure
    make
    sudo make install

Install audio libraries

#### Opus codec

In Terminal go to **/home/user/TBuild/Libraries** and run

    git clone https://github.com/xiph/opus
    cd opus
    git checkout v1.2-alpha2
    ./autogen.sh
    ./configure
    make
    sudo make install

#### FFmpeg

In Terminal go to **/home/user/TBuild/Libraries** and run

    git clone https://github.com/01org/libva.git
    cd libva
    ./autogen.sh --enable-static
    make
    sudo make install
    cd ..

    git clone git://anongit.freedesktop.org/vdpau/libvdpau
    cd libvdpau
    ./autogen.sh --enable-static
    make
    sudo make install
    cd ..

    git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg
    cd ffmpeg
    git checkout release/3.2

    sudo apt-get update
    sudo apt-get -y --force-yes install autoconf automake build-essential libass-dev libfreetype6-dev libgpac-dev libsdl1.2-dev libtheora-dev libtool libva-dev libvdpau-dev libvorbis-dev libxcb1-dev libxcb-shm0-dev libxcb-xfixes0-dev pkg-config texi2html zlib1g-dev
    sudo apt-get install yasm

    ./configure --prefix=/usr/local --disable-programs --disable-doc --disable-everything --enable-protocol=file --enable-libopus --enable-decoder=aac --enable-decoder=aac_latm --enable-decoder=aasc --enable-decoder=flac --enable-decoder=gif --enable-decoder=h264 --enable-decoder=h264_vdpau --enable-decoder=mp1 --enable-decoder=mp1float --enable-decoder=mp2 --enable-decoder=mp2float --enable-decoder=mp3 --enable-decoder=mp3adu --enable-decoder=mp3adufloat --enable-decoder=mp3float --enable-decoder=mp3on4 --enable-decoder=mp3on4float --enable-decoder=mpeg4 --enable-decoder=mpeg4_vdpau --enable-decoder=msmpeg4v2 --enable-decoder=msmpeg4v3 --enable-decoder=opus --enable-decoder=pcm_alaw --enable-decoder=pcm_alaw_at --enable-decoder=pcm_f32be --enable-decoder=pcm_f32le --enable-decoder=pcm_f64be --enable-decoder=pcm_f64le --enable-decoder=pcm_lxf --enable-decoder=pcm_mulaw --enable-decoder=pcm_mulaw_at --enable-decoder=pcm_s16be --enable-decoder=pcm_s16be_planar --enable-decoder=pcm_s16le --enable-decoder=pcm_s16le_planar --enable-decoder=pcm_s24be --enable-decoder=pcm_s24daud --enable-decoder=pcm_s24le --enable-decoder=pcm_s24le_planar --enable-decoder=pcm_s32be --enable-decoder=pcm_s32le --enable-decoder=pcm_s32le_planar --enable-decoder=pcm_s64be --enable-decoder=pcm_s64le --enable-decoder=pcm_s8 --enable-decoder=pcm_s8_planar --enable-decoder=pcm_u16be --enable-decoder=pcm_u16le --enable-decoder=pcm_u24be --enable-decoder=pcm_u24le --enable-decoder=pcm_u32be --enable-decoder=pcm_u32le --enable-decoder=pcm_u8 --enable-decoder=pcm_zork --enable-decoder=vorbis --enable-decoder=wavpack --enable-decoder=wmalossless --enable-decoder=wmapro --enable-decoder=wmav1 --enable-decoder=wmav2 --enable-decoder=wmavoice --enable-encoder=libopus --enable-hwaccel=h264_vaapi --enable-hwaccel=h264_vdpau --enable-hwaccel=mpeg4_vaapi --enable-hwaccel=mpeg4_vdpau --enable-parser=aac --enable-parser=aac_latm --enable-parser=flac --enable-parser=h264 --enable-parser=mpeg4video --enable-parser=mpegaudio --enable-parser=opus --enable-parser=vorbis --enable-demuxer=aac --enable-demuxer=flac --enable-demuxer=gif --enable-demuxer=h264 --enable-demuxer=mov --enable-demuxer=mp3 --enable-demuxer=ogg --enable-demuxer=wav --enable-muxer=ogg --enable-muxer=opus

    make
    sudo make install

#### PortAudio 19

[Download portaudio sources](http://www.portaudio.com/archives/pa_stable_v19_20140130.tgz) from **http://www.portaudio.com/download.html**, extract to **/home/user/TBuild/Libraries**, go to **/home/user/TBuild/Libraries/portaudio** and run

    ./configure
    make
    sudo make install

#### OpenAL Soft

In Terminal go to **/home/user/TBuild/Libraries** and run

    git clone git://repo.or.cz/openal-soft.git

then go to **/home/user/TBuild/Libraries/openal-soft/build** and run

    sudo apt-get install cmake
    cmake -D LIBTYPE:STRING=STATIC ..
    make
    sudo make install

#### OpenSSL

In Terminal go to **/home/user/TBuild/Libraries** and run

    git clone https://github.com/openssl/openssl
    cd openssl
    git checkout OpenSSL_1_0_1-stable
    ./config
    make
    sudo make install

#### libxkbcommon (required for Fcitx Qt plugin)

In Terminal go to **/home/user/TBuild/Libraries** and run

    sudo apt-get install xutils-dev bison python-xcbgen
    git clone https://github.com/xkbcommon/libxkbcommon.git
    cd libxkbcommon
    ./autogen.sh --disable-x11
    make
    sudo make install

#### Qt 5.6.2, slightly patched

In Terminal go to **/home/user/TBuild/Libraries** and run

    git clone git://code.qt.io/qt/qt5.git qt5_6_2
    cd qt5_6_2
    perl init-repository --module-subset=qtbase,qtimageformats
    git checkout v5.6.2
    cd qtimageformats && git checkout v5.6.2 && cd ..
    cd qtbase && git checkout v5.6.2 && cd ..

##### Apply the patch

    cd qtbase && git apply ../../../tdesktop/Telegram/Patches/qtbase_5_6_2.diff && cd ..

##### Add additional input method plugins

    cd qtbase/src/plugins/platforminputcontexts
    git clone https://github.com/telegramdesktop/fcitx.git
    git clone https://github.com/telegramdesktop/hime.git
    cd ../../../..

##### Building library

Install some packages for Qt (see **/home/user/TBuild/Libraries/qt5_6_2/qtbase/src/plugins/platforms/xcb/README**)

    sudo apt-get install libxcb1-dev libxcb-image0-dev libxcb-keysyms1-dev libxcb-icccm4-dev libxcb-render-util0-dev libxcb-util0-dev libxrender-dev libasound-dev libpulse-dev libxcb-sync0-dev libxcb-xfixes0-dev libxcb-randr0-dev libx11-xcb-dev libffi-dev

In Terminal go to **/home/user/TBuild/Libraries/qt5_6_2** and there run

    ./configure -prefix "/usr/local/tdesktop/Qt-5.6.2" -release -force-debug-info -opensource -confirm-license -qt-zlib -qt-libpng -qt-libjpeg -qt-freetype -qt-harfbuzz -qt-pcre -qt-xcb -qt-xkbcommon-x11 -no-opengl -no-gtkstyle -static -openssl-linked -nomake examples -nomake tests
    make -j4
    sudo make install

building (**make** command) will take really long time.

#### Google Breakpad

In Terminal go to **/home/user/TBuild/Libraries** and run

    git clone https://chromium.googlesource.com/breakpad/breakpad
    git clone https://chromium.googlesource.com/linux-syscall-support breakpad/src/third_party/lss
    cd breakpad
    ./configure --prefix=$PWD
    make
    make install

#### GYP and CMake

In Terminal go to **/home/user/TBuild/Libraries** and run

    git clone https://chromium.googlesource.com/external/gyp
    wget https://cmake.org/files/v3.6/cmake-3.6.2.tar.gz
    tar -xf cmake-3.6.2.tar.gz
    cd gyp
    git checkout 702ac58e47
    git apply ../../tdesktop/Telegram/Patches/gyp.diff
    cd ../cmake-3.6.2
    ./configure
    make

### Building Telegram Desktop

In Terminal go to **/home/user/TBuild/tdesktop/Telegram** and run

    gyp/refresh.sh

To make Debug version go to **/home/user/TBuild/tdesktop/out/Debug** and run

    make

To make Release version go to **/home/user/TBuild/tdesktop/out/Release** and run

    make

You can debug your builds from Qt Creator, just open **CMakeLists.txt** from **/home/user/TBuild/tdesktop/out/Debug** and start debug.
