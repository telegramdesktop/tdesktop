## Build instructions for CMake under Ubuntu 14.04

### Prepare folder

Choose an empty folder for the future build, for example **/home/user/TBuild**. It will be named ***BuildPath*** in the rest of this document.

### Obtain your API credentials

You will require **api_id** and **api_hash** to access the Telegram API servers. To learn how to obtain them [click here][api_credentials].

### Install software and required packages

You will need GCC 8 installed. To install them and all the required dependencies run

    sudo apt-get install software-properties-common -y && \
    sudo apt-get install git libexif-dev liblzma-dev libz-dev libssl-dev \
    libgtk2.0-dev libice-dev libsm-dev libicu-dev libdrm-dev dh-autoreconf \
    autoconf automake build-essential libxml2-dev libass-dev libfreetype6-dev \
    libgpac-dev libsdl1.2-dev libtheora-dev libtool libva-dev libvdpau-dev \
    libvorbis-dev libxcb1-dev libxcb-image0-dev libxcb-shm0-dev \
    libxcb-screensaver0-dev ninja-build libegl1-mesa-dev \
    libxcb-xfixes0-dev libxcb-keysyms1-dev libxcb-icccm4-dev libatspi2.0-dev \
    libxcb-render-util0-dev libxcb-util0-dev libxcb-xkb-dev libxrender-dev \
    libasound-dev libpulse-dev libxcb-sync0-dev libxcb-randr0-dev \
    libx11-xcb-dev libffi-dev libncurses5-dev pkg-config texi2html bison yasm \
    zlib1g-dev xutils-dev python-xcbgen chrpath gperf -y --force-yes && \
    sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y && \
    sudo apt-get update && \
    sudo apt-get install gcc-8 g++-8 -y && \
    sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-8 60 && \
    sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-8 60 && \
    sudo update-alternatives --config gcc && \
    sudo add-apt-repository --remove ppa:ubuntu-toolchain-r/test -y

You can set the multithreaded make parameter by running

    MAKE_THREADS_CNT=-j8

### Clone source code and prepare libraries

Go to ***BuildPath*** and run

    git clone --recursive https://github.com/telegramdesktop/tdesktop.git

    mkdir Libraries
    cd Libraries

    git clone https://github.com/Kitware/CMake cmake
    cd cmake
    git checkout v3.16.0
    ./bootstrap
    make $MAKE_THREADS_CNT
    sudo make install
    cd ..

    git clone https://github.com/desktop-app/patches.git
    cd patches
    git checkout ddd4084
    cd ../

    git clone -b v4.0.1-rc2 https://github.com/mozilla/mozjpeg.git
    cd mozjpeg
    cmake -B build . \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DWITH_JPEG8=ON \
    -DPNG_SUPPORTED=OFF
    cmake --build build $MAKE_THREADS_CNT
    sudo cmake --install build
    cd ..

    git clone https://github.com/xiph/opus
    cd opus
    git checkout v1.3
    ./autogen.sh
    ./configure
    make $MAKE_THREADS_CNT
    sudo make install
    cd ..

    git clone https://github.com/intel/libva.git
    cd libva
    CFLAGS=-fPIC CPPFLAGS=-fPIC LDFLAGS=-fPIC ./autogen.sh --enable-static
    make $MAKE_THREADS_CNT
    sudo make install
    cd ..

    git clone https://gitlab.freedesktop.org/vdpau/libvdpau.git --depth=1 -b libvdpau-1.2
    cd libvdpau
    CFLAGS=-fPIC CPPFLAGS=-fPIC LDFLAGS=-fPIC ./autogen.sh --enable-static
    make $MAKE_THREADS_CNT
    sudo make install
    cd ..

    git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg
    cd ffmpeg
    git checkout release/3.4

    ./configure \
    --extra-cflags="-fPIC" \
    --extra-cxxflags="-fPIC" \
    --extra-ldflags="-fPIC" \
    --disable-programs \
    --disable-doc \
    --disable-network \
    --disable-autodetect \
    --disable-everything \
    --disable-alsa \
    --disable-iconv \
    --enable-libopus \
    --enable-vaapi \
    --enable-vdpau \
    --enable-protocol=file \
    --enable-hwaccel=h264_vaapi \
    --enable-hwaccel=h264_vdpau \
    --enable-hwaccel=mpeg4_vaapi \
    --enable-hwaccel=mpeg4_vdpau \
    --enable-decoder=aac \
    --enable-decoder=aac_fixed \
    --enable-decoder=aac_latm \
    --enable-decoder=aasc \
    --enable-decoder=alac \
    --enable-decoder=flac \
    --enable-decoder=gif \
    --enable-decoder=h264 \
    --enable-decoder=h264_vdpau \
    --enable-decoder=hevc \
    --enable-decoder=mp1 \
    --enable-decoder=mp1float \
    --enable-decoder=mp2 \
    --enable-decoder=mp2float \
    --enable-decoder=mp3 \
    --enable-decoder=mp3adu \
    --enable-decoder=mp3adufloat \
    --enable-decoder=mp3float \
    --enable-decoder=mp3on4 \
    --enable-decoder=mp3on4float \
    --enable-decoder=mpeg4 \
    --enable-decoder=mpeg4_vdpau \
    --enable-decoder=msmpeg4v2 \
    --enable-decoder=msmpeg4v3 \
    --enable-decoder=opus \
    --enable-decoder=pcm_alaw \
    --enable-decoder=pcm_f32be \
    --enable-decoder=pcm_f32le \
    --enable-decoder=pcm_f64be \
    --enable-decoder=pcm_f64le \
    --enable-decoder=pcm_lxf \
    --enable-decoder=pcm_mulaw \
    --enable-decoder=pcm_s16be \
    --enable-decoder=pcm_s16be_planar \
    --enable-decoder=pcm_s16le \
    --enable-decoder=pcm_s16le_planar \
    --enable-decoder=pcm_s24be \
    --enable-decoder=pcm_s24daud \
    --enable-decoder=pcm_s24le \
    --enable-decoder=pcm_s24le_planar \
    --enable-decoder=pcm_s32be \
    --enable-decoder=pcm_s32le \
    --enable-decoder=pcm_s32le_planar \
    --enable-decoder=pcm_s64be \
    --enable-decoder=pcm_s64le \
    --enable-decoder=pcm_s8 \
    --enable-decoder=pcm_s8_planar \
    --enable-decoder=pcm_u16be \
    --enable-decoder=pcm_u16le \
    --enable-decoder=pcm_u24be \
    --enable-decoder=pcm_u24le \
    --enable-decoder=pcm_u32be \
    --enable-decoder=pcm_u32le \
    --enable-decoder=pcm_u8 \
    --enable-decoder=pcm_zork \
    --enable-decoder=vorbis \
    --enable-decoder=wavpack \
    --enable-decoder=wmalossless \
    --enable-decoder=wmapro \
    --enable-decoder=wmav1 \
    --enable-decoder=wmav2 \
    --enable-decoder=wmavoice \
    --enable-encoder=libopus \
    --enable-parser=aac \
    --enable-parser=aac_latm \
    --enable-parser=flac \
    --enable-parser=h264 \
    --enable-parser=hevc \
    --enable-parser=mpeg4video \
    --enable-parser=mpegaudio \
    --enable-parser=opus \
    --enable-parser=vorbis \
    --enable-demuxer=aac \
    --enable-demuxer=flac \
    --enable-demuxer=gif \
    --enable-demuxer=h264 \
    --enable-demuxer=hevc \
    --enable-demuxer=m4v \
    --enable-demuxer=mov \
    --enable-demuxer=mp3 \
    --enable-demuxer=ogg \
    --enable-demuxer=wav \
    --enable-muxer=ogg \
    --enable-muxer=opus

    make $MAKE_THREADS_CNT
    sudo make install
    cd ..

    git clone git://repo.or.cz/openal-soft.git
    cd openal-soft
    git checkout openal-soft-1.19.1
    cd build
    cmake .. \
    -DLIBTYPE:STRING=STATIC \
    -DALSOFT_EXAMPLES=OFF \
    -DALSOFT_TESTS=OFF \
    -DALSOFT_UTILS=OFF \
    -DALSOFT_CONFIG=OFF
    make $MAKE_THREADS_CNT
    sudo make install
    cd ../..

    git clone https://github.com/openssl/openssl openssl_1_1_1
    cd openssl_1_1_1
    git checkout OpenSSL_1_1_1-stable
    ./config --prefix=/usr/local/desktop-app/openssl-1.1.1 no-tests
    make $MAKE_THREADS_CNT
    sudo make install_sw
    cd ..

    git clone -b 1.18.0 https://gitlab.freedesktop.org/wayland/wayland
    cd wayland
    ./autogen.sh \
    --enable-static \
    --disable-documentation \
    --disable-dtd-validation
    make $MAKE_THREADS_CNT
    sudo make install
    cd ..

    git clone https://github.com/xkbcommon/libxkbcommon.git
    cd libxkbcommon
    git checkout xkbcommon-0.8.4
    ./autogen.sh \
    --disable-docs \
    --disable-wayland \
    --with-xkb-config-root=/usr/share/X11/xkb \
    --with-x-locale-root=/usr/share/X11/locale
    make $MAKE_THREADS_CNT
    sudo make install
    cd ..

    git clone git://code.qt.io/qt/qt5.git qt_5_12_8
    cd qt_5_12_8
    perl init-repository --module-subset=qtbase,qtwayland,qtimageformats,qtsvg
    git checkout v5.12.8
    git submodule update qtbase qtwayland qtimageformats qtsvg
    cd qtbase
    find ../../patches/qtbase_5_12_8 -type f -print0 | sort -z | xargs -r0 git apply
    cd ..
    cd qtwayland
    find ../../patches/qtwayland_5_12_8 -type f -print0 | sort -z | xargs -r0 git apply
    cd ..

    OPENSSL_DIR=/usr/local/desktop-app/openssl-1.1.1
    ./configure -prefix "/usr/local/desktop-app/Qt-5.12.8" \
        -release \
        -force-debug-info \
        -opensource \
        -confirm-license \
        -qt-zlib \
        -qt-libpng \
        -qt-harfbuzz \
        -qt-pcre \
        -qt-xcb \
        -no-gtk \
        -no-icu \
        -static \
        -dbus-runtime \
        -openssl-linked \
        -I "$OPENSSL_DIR/include" \
        OPENSSL_LIBS="$OPENSSL_DIR/lib/libssl.a $OPENSSL_DIR/lib/libcrypto.a -ldl -lpthread" \
        -I "/usr/local/include" \
        LIBJPEG_LIBS="/usr/local/lib/libjpeg.a" \
        -nomake examples \
        -nomake tests

    make $MAKE_THREADS_CNT
    sudo make install
    cd ..

    git clone https://github.com/desktop-app/tg_owt.git
    cd tg_owt
    mkdir out
    cd out
    mkdir Debug
    cd Debug
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DTG_OWT_SPECIAL_TARGET=linux \
        -DTG_OWT_LIBJPEG_INCLUDE_PATH=/usr/local/include \
        -DTG_OWT_OPENSSL_INCLUDE_PATH=/usr/local/desktop-app/openssl-1.1.1/include \
        -DTG_OWT_OPUS_INCLUDE_PATH=/usr/local/include/opus \
        -DTG_OWT_FFMPEG_INCLUDE_PATH=/usr/local/include ../..
    ninja
    cd ..
    mkdir Release
    cd Release
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DTG_OWT_SPECIAL_TARGET=linux \
        -DTG_OWT_LIBJPEG_INCLUDE_PATH=`pwd`/../../../qt_5_12_8/qtbase/src/3rdparty/libjpeg \
        -DTG_OWT_OPENSSL_INCLUDE_PATH=/usr/local/desktop-app/openssl-1.1.1/include \
        -DTG_OWT_OPUS_INCLUDE_PATH=/usr/local/include/opus \
        -DTG_OWT_FFMPEG_INCLUDE_PATH=/usr/local/include ../..
    ninja
    cd ../../..

    git clone https://chromium.googlesource.com/external/gyp
    cd gyp
    git checkout 9f2a7bb1
    git apply ../patches/gyp.diff
    cd ..

    git clone https://chromium.googlesource.com/breakpad/breakpad
    cd breakpad
    git checkout bc8fb886
    git clone https://chromium.googlesource.com/linux-syscall-support src/third_party/lss
    cd src/third_party/lss
    git checkout a91633d1
    cd ../../..
    ./configure
    make $MAKE_THREADS_CNT
    sudo make install
    cd src
    rm -r testing
    git clone https://github.com/google/googletest testing
    cd tools
    sed -i 's/minidump_upload.m/minidump_upload.cc/' linux/tools_linux.gypi
    ../../../gyp/gyp  --depth=. --generator-output=.. -Goutput_dir=../out tools.gyp --format=cmake
    cd ../../out/Default
    cmake .
    make $MAKE_THREADS_CNT dump_syms
    cd ../../..

### Building the project

Go to ***BuildPath*/tdesktop/Telegram** and run (using [your **api_id** and **api_hash**](#obtain-your-api-credentials))

    ./configure.sh -D TDESKTOP_API_ID=YOUR_API_ID -D TDESKTOP_API_HASH=YOUR_API_HASH -D DESKTOP_APP_USE_PACKAGED=OFF -D DESKTOP_APP_DISABLE_CRASH_REPORTS=OFF

To make Debug version go to ***BuildPath*/tdesktop/out/Debug** and run

    make $MAKE_THREADS_CNT

To make Release version go to ***BuildPath*/tdesktop/out/Release** and run

    make $MAKE_THREADS_CNT

You can debug your builds from Qt Creator, just open ***BuildPath*/tdesktop/CMakeLists.txt**, configure to a separate directory with correct options and launch with debug.

[api_credentials]: api_credentials.md
