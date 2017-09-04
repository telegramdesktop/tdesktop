## Build instructions for Xcode 8.0

### Prepare folder

Choose a folder for the future build, for example **/Users/user/TBuild**

There you will have two folders, **Libraries** for third-party libs and **tdesktop** (or **tdesktop-master**) for the app.

**You will need this hierarchy to be able to follow this README !**

### Clone source code

By git – in Terminal go to **/Users/user/TBuild** and run:

    git clone --recursive https://github.com/telegramdesktop/tdesktop.git

### Prepare libraries

In your build Terminal run:

    MACOSX_DEPLOYMENT_TARGET=10.8

to set minimal supported OS version to 10.8 for future console builds.

#### zlib 1.2.8

http://www.zlib.net/ > Download [**zlib source code, version 1.2.8**](http://www.zlib.net/fossils/zlib-1.2.8.tar.gz)

Extract to **/Users/user/TBuild/Libraries**

##### Building library

In Terminal go to **/Users/user/TBuild/Libraries/zlib-1.2.8** and run:

    CFLAGS="-mmacosx-version-min=10.8" LDFLAGS="-mmacosx-version-min=10.8" ./configure
    make
    sudo make install

#### OpenSSL 1.0.1g

##### Get openssl-xcode project file

From https://github.com/telegramdesktop/openssl-xcode with git in Terminal:

* go to **/Users/user/TBuild/Libraries
* run:

    git clone https://github.com/telegramdesktop/openssl-xcode.git

The path to openssl.xcodeproj should now be: **/Users/user/TBuild/Libraries/openssl-xcode/openssl.xcodeproj**

##### Get the source code:

Download [**openssl-1.0.1h.tar.gz**](http://www.openssl.org/source/openssl-1.0.1h.tar.gz) (4.3 Mb)

* Extract openssl-1.0.1h.tar.gz
* Copy everything from **openssl-1.0.1h** to **/Users/user/TBuild/Libraries/openssl-xcode**

The folder include of openssl should be:
**/Users/user/TBuild/Libraries/openssl-xcode/include**

##### Building library

* Open **/Users/user/TBuild/Libraries/openssl-xcode/openssl.xcodeproj** with Xcode
* Product > Build

#### liblzma
##### Get the source code

Download [**xz-5.0.5.tar.gz**](http://tukaani.org/xz/xz-5.0.5.tar.gz)

Extract to **/Users/user/TBuild/Libraries**

##### Building library

In Terminal go to **/Users/user/TBuild/Libraries/xz-5.0.5** and there run:

    ./configure
    make
    sudo make install

#### libexif 0.6.20
##### Get the source code

From https://github.com/telegramdesktop/libexif-0.6.20 with git in Terminal:

*  go to **/Users/user/TBuild/Libraries**
*  run:

    git clone https://github.com/telegramdesktop/libexif-0.6.20.git

The folder configure should have this path:
**/Users/user/TBuild/Libraries/libexif-0.6.20/configure**

##### Building library

In Terminal go to **/Users/user/TBuild/Libraries/libexif-0.6.20** and there run

    ./configure
    make
    sudo make install

#### OpenAL Soft

Get sources by git – in Terminal go to **/Users/user/TBuild/Libraries** and run

    git clone git://repo.or.cz/openal-soft.git
    cd openal-soft/build
    cmake -D LIBTYPE:STRING=STATIC -D CMAKE_OSX_DEPLOYMENT_TARGET:STRING=10.8 ..
    make
    sudo make install

#### Opus codec

In Terminal go to **/Users/user/TBuild/Libraries** and there run

    git clone https://github.com/xiph/opus
    cd opus
    git checkout v1.2-alpha2
    ./autogen.sh
    CFLAGS="-mmacosx-version-min=10.8" CPPFLAGS="-mmacosx-version-min=10.8" LDFLAGS="-mmacosx-version-min=10.8" ./configure
    make
    sudo make install

#### FFmpeg and Libiconv
##### Get the source code

In Terminal go to **/Users/user/TBuild/Libraries** and run:

    git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg
    cd ffmpeg
    git checkout release/3.2

* Download [libiconv-1.14](http://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.14.tar.gz) from http://www.gnu.org/software/libiconv/#downloading
* Extract to **/Users/user/TBuild/Libraries** to have **/Users/user/TBuild/Libraries/ibiconv-1.14**

##### Building library

In Terminal go to **/Users/user/TBuild/Libraries/libiconv-1.14** and run:

    CFLAGS="-mmacosx-version-min=10.8" CPPFLAGS="-mmacosx-version-min=10.8" LDFLAGS="-mmacosx-version-min=10.8" ./configure --enable-static
    make
    sudo make install

Then in Terminal go to **/Users/user/TBuild/Libraries/ffmpeg** and run:

    ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"

    brew install automake fdk-aac git lame libass libtool libvorbis libvpx opus sdl shtool texi2html theora wget x264 xvid yasm

    CFLAGS=`freetype-config --cflags`
    LDFLAGS=`freetype-config --libs`
    PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/usr/local/lib/pkgconfig:/usr/lib/pkgconfig:/usr/X11/lib/pkgconfig

    ./configure --prefix=/usr/local --disable-programs --disable-doc --disable-everything --enable-protocol=file --enable-libopus --enable-decoder=aac --enable-decoder=aac_latm --enable-decoder=aasc --enable-decoder=flac --enable-decoder=gif --enable-decoder=h264 --enable-decoder=h264_vda --enable-decoder=mp1 --enable-decoder=mp1float --enable-decoder=mp2 --enable-decoder=mp2float --enable-decoder=mp3 --enable-decoder=mp3adu --enable-decoder=mp3adufloat --enable-decoder=mp3float --enable-decoder=mp3on4 --enable-decoder=mp3on4float --enable-decoder=mpeg4 --enable-decoder=msmpeg4v2 --enable-decoder=msmpeg4v3 --enable-decoder=opus --enable-decoder=pcm_alaw --enable-decoder=pcm_alaw_at --enable-decoder=pcm_f32be --enable-decoder=pcm_f32le --enable-decoder=pcm_f64be --enable-decoder=pcm_f64le --enable-decoder=pcm_lxf --enable-decoder=pcm_mulaw --enable-decoder=pcm_mulaw_at --enable-decoder=pcm_s16be --enable-decoder=pcm_s16be_planar --enable-decoder=pcm_s16le --enable-decoder=pcm_s16le_planar --enable-decoder=pcm_s24be --enable-decoder=pcm_s24daud --enable-decoder=pcm_s24le --enable-decoder=pcm_s24le_planar --enable-decoder=pcm_s32be --enable-decoder=pcm_s32le --enable-decoder=pcm_s32le_planar --enable-decoder=pcm_s64be --enable-decoder=pcm_s64le --enable-decoder=pcm_s8 --enable-decoder=pcm_s8_planar --enable-decoder=pcm_u16be --enable-decoder=pcm_u16le --enable-decoder=pcm_u24be --enable-decoder=pcm_u24le --enable-decoder=pcm_u32be --enable-decoder=pcm_u32le --enable-decoder=pcm_u8 --enable-decoder=pcm_zork --enable-decoder=vorbis --enable-decoder=wavpack --enable-decoder=wmalossless --enable-decoder=wmapro --enable-decoder=wmav1 --enable-decoder=wmav2 --enable-decoder=wmavoice --enable-encoder=libopus --enable-hwaccel=mpeg4_videotoolbox --enable-hwaccel=h264_vda --enable-hwaccel=h264_videotoolbox --enable-parser=aac --enable-parser=aac_latm --enable-parser=flac --enable-parser=h264 --enable-parser=mpeg4video --enable-parser=mpegaudio --enable-parser=opus --enable-parser=vorbis --enable-demuxer=aac --enable-demuxer=flac --enable-demuxer=gif --enable-demuxer=h264 --enable-demuxer=mov --enable-demuxer=mp3 --enable-demuxer=ogg --enable-demuxer=wav --enable-muxer=ogg --enable-muxer=opus --extra-cflags="-mmacosx-version-min=10.8" --extra-cxxflags="-mmacosx-version-min=10.8" --extra-ldflags="-mmacosx-version-min=10.8"

    make
    sudo make install

#### Qt 5.6.2, slightly patched
##### Get the source code

In Terminal go to **/Users/user/TBuild/Libraries** and run:

    git clone git://code.qt.io/qt/qt5.git qt5_6_2
    cd qt5_6_2
    perl init-repository --module-subset=qtbase,qtimageformats
    git checkout v5.6.2
    cd qtimageformats && git checkout v5.6.2 && cd ..
    cd qtbase && git checkout v5.6.2 && cd ..

##### Apply the patch

From **/Users/user/TBuild/Libraries/qt5_6_2/qtbase**, run:

    git apply ../../../tdesktop/Telegram/Patches/qtbase_5_6_2.diff

##### Building library

Go to **/Users/user/TBuild/Libraries/qt5_6_2** and run:

    ./configure -prefix "/usr/local/tdesktop/Qt-5.6.2" -debug-and-release -force-debug-info -opensource -confirm-license -static -opengl desktop -no-openssl -securetransport -nomake examples -nomake tests -platform macx-clang
    make -j4
    sudo make install

Building (**make** command) will take a really long time.

#### Google Crashpad

##### Install gyp

In Terminal go to **/Users/user/TBuild/Libraries** and run:

    git clone https://chromium.googlesource.com/external/gyp
    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
    cd gyp
    git checkout 702ac58e47
    git apply ../../tdesktop/Telegram/Patches/gyp.diff
    ./setup.py build
    sudo ./setup.py install
    cd ..

##### Build crashpad

In Terminal go to **/Users/user/TBuild/Libraries** and run:

    git clone https://chromium.googlesource.com/crashpad/crashpad.git
    cd crashpad
    git checkout feb3aa3923
    cd third_party/mini_chromium
    git clone https://chromium.googlesource.com/chromium/mini_chromium
    cd mini_chromium
    git checkout 7c5b0c1ab4
    git apply ../../../../../tdesktop/Telegram/Patches/mini_chromium.diff
    cd ../../gtest
    git clone https://chromium.googlesource.com/external/github.com/google/googletest gtest
    cd gtest
    git checkout d62d6c6556
    cd ../../../

    build/gyp_crashpad.py -Dmac_deployment_target=10.8
    ninja -C out/Debug
    ninja -C out/Release

#### Prepare GYP

In Terminal go to **/Users/user/TBuild/Libraries** and run

    cd gyp
    git apply ../../tdesktop/Telegram/Patches/gyp.diff

### Building Telegram Desktop

In Terminal go to **/home/user/TBuild/tdesktop/Telegram** and run

    gyp/refresh.sh

Then launch Xcode, open **/Users/user/TBuild/tdesktop/Telegram/Telegram.xcodeproj** and build for Debug / Release.
