## Build instructions for Xcode 9.0

### Prepare folder

Choose a folder for the future build, for example **/Users/user/TBuild**. It will be named ***BuildPath*** in the rest of this document. All commands will be launched from Terminal.

### Obtain your API credentials

You will require **api_id** and **api_hash** to access the Telegram API servers. To learn how to obtain them [click here][api_credentials].

### Download libraries

Download [**xz-5.0.5**](http://tukaani.org/xz/xz-5.0.5.tar.gz) and unpack to ***BuildPath*/Libraries/xz-5.0.5**

Download [**libiconv-1.15**](http://www.gnu.org/software/libiconv/#downloading) and unpack to ***BuildPath*/Libraries/libiconv-1.15**

### Clone source code and prepare libraries

Go to ***BuildPath*** and run

    MACOSX_DEPLOYMENT_TARGET=10.8

    ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
    brew install automake fdk-aac git lame libass libtool libvorbis libvpx opus sdl shtool texi2html theora wget x264 xvid yasm

    sudo xcode-select -s /Applications/Xcode.app/Contents/Developer

    git clone --recursive https://github.com/telegramdesktop/tdesktop.git

    cd Libraries

    git clone https://github.com/ericniebler/range-v3

    cd xz-5.0.5
    CFLAGS="-mmacosx-version-min=10.8" LDFLAGS="-mmacosx-version-min=10.8" ./configure
    make
    sudo make install
    cd ..

    git clone https://github.com/Kitware/CMake
    cd CMake
    ./bootstrap
    make -j4
    sudo make install
    cd ..

    git clone https://github.com/telegramdesktop/zlib.git
    cd zlib
    CFLAGS="-mmacosx-version-min=10.8" LDFLAGS="-mmacosx-version-min=10.8" ./configure
    make -j4
    sudo make install
    cd ..

    git clone https://github.com/openssl/openssl
    cd openssl
    git checkout OpenSSL_1_0_1-stable
    ./Configure darwin64-x86_64-cc -static -mmacosx-version-min=10.8
    make build_libs -j4
    cd ..

    git clone https://github.com/xiph/opus
    cd opus
    git checkout v1.2.1
    ./autogen.sh
    CFLAGS="-mmacosx-version-min=10.8" CPPFLAGS="-mmacosx-version-min=10.8" LDFLAGS="-mmacosx-version-min=10.8" ./configure
    make -j4
    sudo make install
    cd ..

    cd libiconv-1.15
    CFLAGS="-mmacosx-version-min=10.8" CPPFLAGS="-mmacosx-version-min=10.8" LDFLAGS="-mmacosx-version-min=10.8" ./configure --enable-static
    make -j4
    sudo make install
    cd ..

    git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg
    cd ffmpeg
    git checkout release/3.4
    CFLAGS=`freetype-config --cflags`
    LDFLAGS=`freetype-config --libs`
    PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/usr/local/lib/pkgconfig:/usr/lib/pkgconfig:/usr/X11/lib/pkgconfig

    ./configure --prefix=/usr/local --disable-programs --disable-doc --disable-everything --enable-protocol=file --enable-libopus --enable-decoder=aac --enable-decoder=aac_latm --enable-decoder=aasc --enable-decoder=flac --enable-decoder=gif --enable-decoder=h264 --enable-decoder=h264_vda --enable-decoder=mp1 --enable-decoder=mp1float --enable-decoder=mp2 --enable-decoder=mp2float --enable-decoder=mp3 --enable-decoder=mp3adu --enable-decoder=mp3adufloat --enable-decoder=mp3float --enable-decoder=mp3on4 --enable-decoder=mp3on4float --enable-decoder=mpeg4 --enable-decoder=msmpeg4v2 --enable-decoder=msmpeg4v3 --enable-decoder=opus --enable-decoder=pcm_alaw --enable-decoder=pcm_alaw_at --enable-decoder=pcm_f32be --enable-decoder=pcm_f32le --enable-decoder=pcm_f64be --enable-decoder=pcm_f64le --enable-decoder=pcm_lxf --enable-decoder=pcm_mulaw --enable-decoder=pcm_mulaw_at --enable-decoder=pcm_s16be --enable-decoder=pcm_s16be_planar --enable-decoder=pcm_s16le --enable-decoder=pcm_s16le_planar --enable-decoder=pcm_s24be --enable-decoder=pcm_s24daud --enable-decoder=pcm_s24le --enable-decoder=pcm_s24le_planar --enable-decoder=pcm_s32be --enable-decoder=pcm_s32le --enable-decoder=pcm_s32le_planar --enable-decoder=pcm_s64be --enable-decoder=pcm_s64le --enable-decoder=pcm_s8 --enable-decoder=pcm_s8_planar --enable-decoder=pcm_u16be --enable-decoder=pcm_u16le --enable-decoder=pcm_u24be --enable-decoder=pcm_u24le --enable-decoder=pcm_u32be --enable-decoder=pcm_u32le --enable-decoder=pcm_u8 --enable-decoder=pcm_zork --enable-decoder=vorbis --enable-decoder=wavpack --enable-decoder=wmalossless --enable-decoder=wmapro --enable-decoder=wmav1 --enable-decoder=wmav2 --enable-decoder=wmavoice --enable-encoder=libopus --enable-hwaccel=mpeg4_videotoolbox --enable-hwaccel=h264_vda --enable-hwaccel=h264_videotoolbox --enable-parser=aac --enable-parser=aac_latm --enable-parser=flac --enable-parser=h264 --enable-parser=mpeg4video --enable-parser=mpegaudio --enable-parser=opus --enable-parser=vorbis --enable-demuxer=aac --enable-demuxer=flac --enable-demuxer=gif --enable-demuxer=h264 --enable-demuxer=mov --enable-demuxer=mp3 --enable-demuxer=ogg --enable-demuxer=wav --enable-muxer=ogg --enable-muxer=opus --extra-cflags="-mmacosx-version-min=10.8" --extra-cxxflags="-mmacosx-version-min=10.8" --extra-ldflags="-mmacosx-version-min=10.8"

    make -j4
    sudo make install
    cd ..

    git clone git://repo.or.cz/openal-soft.git
    cd openal-soft/build
    cmake -D LIBTYPE:STRING=STATIC -D CMAKE_OSX_DEPLOYMENT_TARGET:STRING=10.8 ..
    make -j4
    sudo make install
    cd ../..

    git clone https://chromium.googlesource.com/external/gyp
    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
    export PATH="$PWD/depot_tools:$PATH"
    cd gyp
    git checkout 702ac58e47
    git apply ../../tdesktop/Telegram/Patches/gyp.diff
    ./setup.py build
    sudo ./setup.py install
    cd ..

    git clone https://chromium.googlesource.com/crashpad/crashpad.git
    cd crashpad
    git checkout feb3aa3923
    git apply ../../tdesktop/Telegram/Patches/crashpad.diff
    cd third_party/mini_chromium
    git clone https://chromium.googlesource.com/chromium/mini_chromium
    cd mini_chromium
    git checkout 7c5b0c1ab4
    git apply ../../../../../tdesktop/Telegram/Patches/mini_chromium.diff
    cd ../../gtest
    git clone https://chromium.googlesource.com/external/github.com/google/googletest gtest
    cd gtest
    git checkout d62d6c6556
    cd ../../..

    build/gyp_crashpad.py -Dmac_deployment_target=10.8
    ninja -C out/Debug
    ninja -C out/Release
    cd ..

    git clone git://code.qt.io/qt/qt5.git qt5_6_2
    cd qt5_6_2
    perl init-repository --module-subset=qtbase,qtimageformats
    git checkout v5.6.2
    cd qtimageformats && git checkout v5.6.2 && cd ..
    cd qtbase && git checkout v5.6.2 && git apply ../../../tdesktop/Telegram/Patches/qtbase_5_6_2.diff && cd ..

    ./configure -prefix "/usr/local/tdesktop/Qt-5.6.2" -debug-and-release -force-debug-info -opensource -confirm-license -static -opengl desktop -no-openssl -securetransport -nomake examples -nomake tests -platform macx-clang
    make -j4
    sudo make install
    cd ..

### Building the project

Go to ***BuildPath*/tdesktop/Telegram** and run

    gyp/refresh.sh

Then launch Xcode, open ***BuildPath*/tdesktop/Telegram/Telegram.xcodeproj** and build for Debug / Release.

[api_credentials]: api_credentials.md
