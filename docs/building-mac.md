## Build instructions for macOS

### Prepare folder

Choose a folder for the future build, for example **/Users/user/TBuild**. It will be named ***BuildPath*** in the rest of this document. All commands will be launched from Terminal.

### Obtain your API credentials

You will require **api_id** and **api_hash** to access the Telegram API servers. To learn how to obtain them [click here][api_credentials].

### Clone source code and prepare libraries

Go to ***BuildPath*** and run

    MAKE_THREADS_CNT=-j8
    MACOSX_DEPLOYMENT_TARGET=10.12
    UNGUARDED="-Werror=unguarded-availability-new"
    MIN_VER="-mmacosx-version-min=10.12"

    ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
    brew install automake cmake fdk-aac git lame libass libtool libvorbis libvpx ninja opus sdl shtool texi2html theora wget x264 xvid yasm pkg-config gnu-tar

    sudo xcode-select -s /Applications/Xcode.app/Contents/Developer

    git clone --recursive https://github.com/telegramdesktop/tdesktop.git

    mkdir ThirdParty
    cd ThirdParty

    git clone https://github.com/desktop-app/patches.git
    cd patches
    git checkout 87a2e9ee07
    cd ../
    git clone https://chromium.googlesource.com/external/gyp
    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
    export PATH="$PWD/depot_tools:$PATH"
    cd gyp
    git checkout 9f2a7bb1
    git apply ../patches/gyp.diff
    ./setup.py build
    sudo ./setup.py install
    cd ..

    git clone -b macos_padding https://github.com/desktop-app/yasm.git
    cd yasm
    ./autogen.sh
    make $MAKE_THREADS_CNT
    cd ..

    git clone https://github.com/desktop-app/macho_edit.git
    cd macho_edit
    xcodebuild build -configuration Release -project macho_edit.xcodeproj -target macho_edit
    cd ..

    cd ..
    mkdir -p Libraries/macos
    cd Libraries/macos

    git clone https://github.com/desktop-app/patches.git
    cd patches
    git checkout 87a2e9ee07
    cd ..

    git clone https://git.tukaani.org/xz.git
    cd xz
    git checkout v5.2.5
    mkdir build
    cd build
    CFLAGS="$UNGUARDED" CPPFLAGS="$UNGUARDED" cmake -D CMAKE_OSX_DEPLOYMENT_TARGET:STRING=10.12 -D CMAKE_INSTALL_PREFIX:STRING=/usr/local/macos ..
    make $MAKE_THREADS_CNT
    sudo make install
    cd ../..

    git clone https://github.com/desktop-app/zlib.git
    cd zlib
    CFLAGS="$MIN_VER $UNGUARDED" LDFLAGS="$MIN_VER" ./configure --prefix=/usr/local/macos
    make $MAKE_THREADS_CNT
    sudo make install
    cd ..

    git clone -b v4.0.1-rc2 https://github.com/mozilla/mozjpeg.git
    cd mozjpeg
    cmake -B build . \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local/macos \
        -DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=10.12 \
        -DWITH_JPEG8=ON \
        -DPNG_SUPPORTED=OFF
    cmake --build build $MAKE_THREADS_CNT
    sudo cmake --install build
    cd ..

    git clone https://github.com/openssl/openssl openssl_1_1_1
    cd openssl_1_1_1
    git checkout OpenSSL_1_1_1-stable
    ./Configure --prefix=/usr/local/macos no-shared no-tests darwin64-x86_64-cc $MIN_VER
    make build_libs $MAKE_THREADS_CNT
    cd ..

    git clone https://github.com/xiph/opus.git
    cd opus
    git checkout v1.3
    ./autogen.sh
    CFLAGS="$MIN_VER $UNGUARDED" CPPFLAGS="$MIN_VER $UNGUARDED" LDFLAGS="$MIN_VER" ./configure --prefix=/usr/local/macos
    make $MAKE_THREADS_CNT
    sudo make install
    cd ..

    git clone https://github.com/desktop-app/rnnoise.git
    cd rnnoise
    mkdir out
    cd out
    mkdir Debug
    cd Debug
    cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug ../..
    ninja
    cd ..
    mkdir Release
    cd Release
    cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ../..
    ninja
    cd ../../..

    libiconv_ver=1.16
    wget https://ftp.gnu.org/pub/gnu/libiconv/libiconv-$libiconv_ver.tar.gz
    tar -xvzf libiconv-$libiconv_ver.tar.gz
    rm libiconv-$libiconv_ver.tar.gz
    cd libiconv-$libiconv_ver
    CFLAGS="$MIN_VER $UNGUARDED" CPPFLAGS="$MIN_VER $UNGUARDED" LDFLAGS="$MIN_VER" ./configure --enable-static --prefix=/usr/local/macos
    make $MAKE_THREADS_CNT
    sudo make install
    cd ..

    git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg
    cd ffmpeg
    git checkout release/4.4
    CFLAGS=`freetype-config --cflags`
    LDFLAGS=`freetype-config --libs`
    PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/usr/local/lib/pkgconfig:/usr/lib/pkgconfig:/usr/X11/lib/pkgconfig
    cp ../patches/macos_yasm_wrap.sh ./

    ./configure --prefix=/usr/local/macos \
    --extra-cflags="$MIN_VER $UNGUARDED -DCONFIG_SAFE_BITSTREAM_READER=1" \
    --extra-cxxflags="$MIN_VER $UNGUARDED -DCONFIG_SAFE_BITSTREAM_READER=1" \
    --extra-ldflags="$MIN_VER" \
    --x86asmexe=`pwd`/macos_yasm_wrap.sh \
    --enable-protocol=file \
    --enable-libopus \
    --disable-programs \
    --disable-doc \
    --disable-network \
    --disable-everything \
    --enable-hwaccel=h264_videotoolbox \
    --enable-hwaccel=hevc_videotoolbox \
    --enable-hwaccel=mpeg1_videotoolbox \
    --enable-hwaccel=mpeg2_videotoolbox \
    --enable-hwaccel=mpeg4_videotoolbox \
    --enable-decoder=aac \
    --enable-decoder=aac_at \
    --enable-decoder=aac_fixed \
    --enable-decoder=aac_latm \
    --enable-decoder=aasc \
    --enable-decoder=alac \
    --enable-decoder=alac_at \
    --enable-decoder=flac \
    --enable-decoder=gif \
    --enable-decoder=h264 \
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
    --enable-decoder=msmpeg4v2 \
    --enable-decoder=msmpeg4v3 \
    --enable-decoder=opus \
    --enable-decoder=pcm_alaw \
    --enable-decoder=pcm_alaw_at \
    --enable-decoder=pcm_f32be \
    --enable-decoder=pcm_f32le \
    --enable-decoder=pcm_f64be \
    --enable-decoder=pcm_f64le \
    --enable-decoder=pcm_lxf \
    --enable-decoder=pcm_mulaw \
    --enable-decoder=pcm_mulaw_at \
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

    git clone --branch capture_with_webrtc https://github.com/telegramdesktop/openal-soft.git
    cd openal-soft/build
    CFLAGS=$UNGUARDED CPPFLAGS=$UNGUARDED cmake -D CMAKE_INSTALL_PREFIX:PATH=/usr/local/macos -D ALSOFT_EXAMPLES=OFF -D LIBTYPE:STRING=STATIC -D CMAKE_OSX_DEPLOYMENT_TARGET:STRING=10.12 ..
    make $MAKE_THREADS_CNT
    sudo make install
    cd ../..

    git clone https://chromium.googlesource.com/crashpad/crashpad.git
    cd crashpad
    git checkout feb3aa3923
    git apply ../patches/crashpad.diff
    cd third_party/mini_chromium
    git clone https://chromium.googlesource.com/chromium/mini_chromium
    cd mini_chromium
    git checkout 7c5b0c1ab4
    git apply ../../../../patches/mini_chromium.diff
    cd ../../gtest
    git clone https://chromium.googlesource.com/external/github.com/google/googletest gtest
    cd gtest
    git checkout d62d6c6556
    cd ../../..

    build/gyp_crashpad.py -Dmac_deployment_target=10.10
    ninja -C out/Debug base crashpad_util crashpad_client crashpad_handler
    ninja -C out/Release base crashpad_util crashpad_client crashpad_handler
    cd ..

    git clone git://code.qt.io/qt/qt5.git qt_5_15_2
    cd qt_5_15_2
    perl init-repository --module-subset=qtbase,qtimageformats
    git checkout v5.15.2
    git submodule update qtbase qtimageformats
    cd qtbase
    find ../../patches/qtbase_5_15_2 -type f -print0 | sort -z | xargs -0 git apply
    cd ..

    ./configure -prefix "/usr/local/desktop-app/Qt-5.15.2" \
        -debug-and-release \
        -force-debug-info \
        -opensource \
        -confirm-license \
        -static \
        -opengl desktop \
        -no-openssl \
        -securetransport \
        -I "/usr/local/macos/include" \
        LIBJPEG_LIBS="/usr/local/macos/lib/libjpeg.a" \
        ZLIB_LIBS="/usr/local/macos/lib/libz.a" \
        -nomake examples \
        -nomake tests \
        -platform macx-clang

    make $MAKE_THREADS_CNT
    sudo make install
    cd ..

    git clone https://github.com/desktop-app/tg_owt.git
    cd tg_owt
    git checkout 91d836dc84
    git submodule init
    git submodule update
    mkdir out
    cd out
    mkdir Debug
    cd Debug
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DTG_OWT_BUILD_AUDIO_BACKENDS=OFF \
        -DTG_OWT_SPECIAL_TARGET=mac \
        -DTG_OWT_LIBJPEG_INCLUDE_PATH=/usr/local/macos/include \
        -DTG_OWT_OPENSSL_INCLUDE_PATH=`pwd`/../../../openssl_1_1_1/include \
        -DTG_OWT_OPUS_INCLUDE_PATH=/usr/local/macos/include/opus \
        -DTG_OWT_FFMPEG_INCLUDE_PATH=/usr/local/macos/include ../..
    ninja
    cd ..
    mkdir Release
    cd Release
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DTG_OWT_BUILD_AUDIO_BACKENDS=OFF \
        -DTG_OWT_SPECIAL_TARGET=mac \
        -DTG_OWT_LIBJPEG_INCLUDE_PATH=/usr/local/macos/include \
        -DTG_OWT_OPENSSL_INCLUDE_PATH=`pwd`/../../../openssl_1_1_1/include \
        -DTG_OWT_OPUS_INCLUDE_PATH=/usr/local/macos/include/opus \
        -DTG_OWT_FFMPEG_INCLUDE_PATH=/usr/local/macos/include ../..
    ninja
    cd ../../..

### Building the project

Go to ***BuildPath*/tdesktop/Telegram** and run (using [your **api_id** and **api_hash**](#obtain-your-api-credentials))

    ./configure.sh -D TDESKTOP_API_ID=YOUR_API_ID -D TDESKTOP_API_HASH=YOUR_API_HASH -D DESKTOP_APP_USE_PACKAGED=OFF

Then launch Xcode, open ***BuildPath*/tdesktop/out/Telegram.xcodeproj** and build for Debug / Release.

[api_credentials]: api_credentials.md
