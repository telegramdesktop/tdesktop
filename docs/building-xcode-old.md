## Build instructions for Xcode 7.2.1

**NB** These are outdated, please refer to [Building using Xcode][xcode] instructions.

### Prepare folder

Choose a folder for the future build, for example **/Users/user/TBuild** There you will have two folders, **Libraries** for third-party libs and **tdesktop** (or **tdesktop-master**) for the app.

### Clone source code

By git – in Terminal go to **/Users/user/TBuild** and run

    git clone --recursive https://github.com/telegramdesktop/tdesktop.git

then go to **/Users/user/TBuild/tdesktop** and run

    git checkout dev

#### Prepare latest cmake

Download the [latest sources](https://cmake.org/download/) and unpack to **/Users/user/TBuild/Libraries/macold**

    ./bootstrap
    make -j4
    sudo make install

### Prepare libraries

In your build Terminal run

    MACOSX_DEPLOYMENT_TARGET=10.6

to set minimal supported OS version to 10.6 for future console builds.

#### custom build of libc++

From **/Users/user/TBuild/Libraries/macold** run

    svn co http://llvm.org/svn/llvm-project/llvm/trunk llvm
    cd llvm/projects
    svn co http://llvm.org/svn/llvm-project/libcxx/trunk libcxx
    svn co http://llvm.org/svn/llvm-project/libcxxabi/trunk libcxxabi

    cd ../../
    mkdir libcxxabi
    cd libcxxabi

    cmake -G "Unix Makefiles" -DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=10.6 -DCMAKE_BUILD_TYPE:STRING=Release -DLIBCXX_ENABLE_SHARED:BOOL=NO -DCMAKE_INSTALL_PREFIX:PATH=/usr/local/macold -DLLVM_PATH=../llvm -DLIBCXXABI_LIBCXX_PATH=../llvm/projects/libcxx ../llvm/projects/libcxxabi/
    make -j4
    sudo make install

    cd ../
    mkdir libcxx
    cd libcxx

    cmake -G "Unix Makefiles" -DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=10.6 -DCMAKE_BUILD_TYPE:STRING=Release -DCMAKE_INSTALL_PREFIX:PATH=/usr/local/macold -DLIBCXX_ENABLE_SHARED:BOOL=NO -DLIBCXX_CXX_ABI:STRING=libstdc++ -DLIBCXX_CXX_ABI_INCLUDE_PATHS="/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/c++/4.2.1/" -DLLVM_PATH=../llvm/ ../llvm/projects/libcxx/
    make -j4
    sudo make install

#### zlib

In Terminal go to **/Users/user/TBuild/Libraries** and run:

    git clone https://github.com/telegramdesktop/zlib.git
    cd zlib
    prefix=/usr/local/macold CFLAGS="-mmacosx-version-min=10.6" LDFLAGS="-mmacosx-version-min=10.6" ./configure
    make
    sudo make install

#### OpenSSL 1.0.1g

http://www.openssl.org/source/ > Download [**openssl-1.0.1h.tar.gz**](http://www.openssl.org/source/openssl-1.0.1h.tar.gz) (4.3 Mb)

Extract openssl-1.0.1h.tar.gz to **/Users/user/TBuild/Libraries/macold/openssl-1.0.1h**

    ./Configure --install_prefix=/usr/local/macold darwin64-x86_64-cc -static -mmacosx-version-min=10.6
    make build_crypto build_ssl -j4

#### liblzma

http://tukaani.org/xz/ > Download [**xz-5.0.5.tar.gz**](http://tukaani.org/xz/xz-5.0.5.tar.gz)

Extract to **/Users/user/TBuild/Libraries**

##### Building library

In Terminal go to **/Users/user/TBuild/Libraries/xz-5.0.5** and there run

    ./configure
    make
    sudo make install

#### libexif 0.6.20

Get sources from https://github.com/telegramdesktop/libexif-0.6.20, by git – in Terminal go to **/Users/user/TBuild/Libraries/macold** and run

    git clone https://github.com/telegramdesktop/libexif-0.6.20.git

##### Building library

In Terminal go to **/Users/user/TBuild/Libraries/macold/libexif-0.6.20** and there run

    CFLAGS="-mmacosx-version-min=10.6" CPPFLAGS="-mmacosx-version-min=10.6 -nostdinc++" LDFLAGS="-mmacosx-version-min=10.6" ./configure --prefix=/usr/local/macold
    make -j4
    sudo make install

#### OpenAL Soft

Get sources by git – in Terminal go to **/Users/user/TBuild/Libraries/macold** and run

    git clone git://repo.or.cz/openal-soft.git

to have **/Users/user/TBuild/Libraries/macold/openal-soft/CMakeLists.txt**

##### Building library

In Terminal go to **/Users/user/TBuild/Libraries/openal-soft/build** and there run

    cmake -DLIBTYPE:STRING=STATIC -DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=10.6 -DCMAKE_INSTALL_PREFIX:STRING=/usr/local/macold ..
    make
    sudo make install

#### Opus codec

In Terminal go to **/Users/user/TBuild/Libraries/macold** and there run

    git clone https://github.com/xiph/opus
    cd opus
    git checkout v1.2-alpha2
    ./autogen.sh
    CFLAGS="-mmacosx-version-min=10.6" CPPFLAGS="-mmacosx-version-min=10.6" LDFLAGS="-mmacosx-version-min=10.6" ./configure --prefix=/usr/local/macold
    make
    sudo make install

#### FFmpeg

In Terminal go to **/Users/user/TBuild/Libraries/macold** and run:

    git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg
    cd ffmpeg
    git checkout release/3.2

##### Building libraries

Download [libiconv-1.14](http://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.14.tar.gz) from http://www.gnu.org/software/libiconv/#downloading, extract it to **/Users/user/TBuild/Libraries/macold**

In Termianl go to **/Users/user/TBuild/Libraries/macold/libiconv-1.14** and run

    CFLAGS="-mmacosx-version-min=10.6" CPPFLAGS="-mmacosx-version-min=10.6 -nostdinc++" LDFLAGS="-mmacosx-version-min=10.6" ./configure --enable-static --prefix=/usr/local/macold
    make -j4
    sudo make install

Then in Terminal go to **/Users/user/TBuild/Libraries/macold/ffmpeg** and run

    ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"

    brew install automake fdk-aac git lame libass libtool libvorbis libvpx opus sdl shtool texi2html theora wget x264 xvid yasm

    CFLAGS=`freetype-config --cflags`
    LDFLAGS=`freetype-config --libs`
    PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/usr/local/lib/pkgconfig:/usr/lib/pkgconfig:/usr/X11/lib/pkgconfig

    ./configure --prefix=/usr/local/macold --disable-programs --disable-doc --disable-everything --enable-protocol=file --enable-libopus --enable-decoder=aac --enable-decoder=aac_latm --enable-decoder=aasc --enable-decoder=flac --enable-decoder=gif --enable-decoder=h264 --enable-decoder=mp1 --enable-decoder=mp1float --enable-decoder=mp2 --enable-decoder=mp2float --enable-decoder=mp3 --enable-decoder=mp3adu --enable-decoder=mp3adufloat --enable-decoder=mp3float --enable-decoder=mp3on4 --enable-decoder=mp3on4float --enable-decoder=mpeg4 --enable-decoder=msmpeg4v2 --enable-decoder=msmpeg4v3 --enable-decoder=opus --enable-decoder=pcm_alaw --enable-decoder=pcm_alaw_at --enable-decoder=pcm_f32be --enable-decoder=pcm_f32le --enable-decoder=pcm_f64be --enable-decoder=pcm_f64le --enable-decoder=pcm_lxf --enable-decoder=pcm_mulaw --enable-decoder=pcm_mulaw_at --enable-decoder=pcm_s16be --enable-decoder=pcm_s16be_planar --enable-decoder=pcm_s16le --enable-decoder=pcm_s16le_planar --enable-decoder=pcm_s24be --enable-decoder=pcm_s24daud --enable-decoder=pcm_s24le --enable-decoder=pcm_s24le_planar --enable-decoder=pcm_s32be --enable-decoder=pcm_s32le --enable-decoder=pcm_s32le_planar --enable-decoder=pcm_s64be --enable-decoder=pcm_s64le --enable-decoder=pcm_s8 --enable-decoder=pcm_s8_planar --enable-decoder=pcm_u16be --enable-decoder=pcm_u16le --enable-decoder=pcm_u24be --enable-decoder=pcm_u24le --enable-decoder=pcm_u32be --enable-decoder=pcm_u32le --enable-decoder=pcm_u8 --enable-decoder=pcm_zork --enable-decoder=vorbis --enable-decoder=wavpack --enable-decoder=wmalossless --enable-decoder=wmapro --enable-decoder=wmav1 --enable-decoder=wmav2 --enable-decoder=wmavoice --enable-encoder=libopus --enable-parser=aac --enable-parser=aac_latm --enable-parser=flac --enable-parser=h264 --enable-parser=mpeg4video --enable-parser=mpegaudio --enable-parser=opus --enable-parser=vorbis --enable-demuxer=aac --enable-demuxer=flac --enable-demuxer=gif --enable-demuxer=h264 --enable-demuxer=mov --enable-demuxer=mp3 --enable-demuxer=ogg --enable-demuxer=wav --enable-muxer=ogg --enable-muxer=opus --extra-cflags="-mmacosx-version-min=10.6" --extra-cxxflags="-mmacosx-version-min=10.6 -nostdinc++" --extra-ldflags="-mmacosx-version-min=10.6"

    make
    sudo make install

#### Qt 5.3.2, slightly patched
##### Get the source code

In Terminal go to **/Users/user/TBuild/Libraries** and run:

    git clone git://code.qt.io/qt/qt5.git qt5_3_2
    cd qt5_3_2
    perl init-repository --module-subset=qtbase,qtimageformats
    git checkout v5.3.2
    cd qtimageformats && git checkout v5.3.2 && cd ..
    cd qtbase && git checkout v5.3.2 && cd ..

##### Apply the patch

From **/Users/user/TBuild/Libraries/macold/qt5_3_2/qtbase**, run:

    git apply ../../../../tdesktop/Telegram/Patches/macold/qtbase_5_3_2.diff

From **/Users/user/TBuild/Libraries/macold/qt5_3_2/qtimageformats**, run:

    git apply ../../../../tdesktop/Telegram/Patches/macold/qtimageformats_5_3_2.diff

##### Building library

Go to **/Users/user/TBuild/Libraries/macold/qt5_3_2** and run:

    OPENSSL_LIBS="/Users/user/TBuild/Libraries/macold/openssl-1.0.1h/libssl.a /Users/user/TBuild/Libraries/macold/openssl-1.0.1h/libcrypto.a" ./configure -prefix "/usr/local/macold/Qt-5.3.2" -debug-and-release -force-debug-info -opensource -confirm-license -static -opengl desktop -openssl-linked -I "/Users/user/TBuild/Libraries/macold/openssl-1.0.1h/include" -nomake examples -nomake tests -platform macx-g++
    make -j4
    sudo make -j4 install

building (**make** command) will take really long time.

#### Google Crashpad

##### Install gyp

.. the same as modern ..

##### Build crashpad

In Terminal go to **/Users/user/TBuild/Libraries/macold** and run:

    git clone https://chromium.googlesource.com/crashpad/crashpad.git
    cd crashpad
    git checkout feb3aa3923
    git apply ../../../tdesktop/Telegram/Patches/macold/crashpad.diff
    cd third_party/mini_chromium
    git clone https://chromium.googlesource.com/chromium/mini_chromium
    cd mini_chromium
    git checkout 7c5b0c1ab4
    git apply ../../../../../../tdesktop/Telegram/Patches/macold/mini_chromium.diff
    cd ../../gtest
    git clone https://chromium.googlesource.com/external/github.com/google/googletest gtest
    cd gtest
    git checkout d62d6c6556
    cd ../../../

    build/gyp_crashpad.py -Dmac_deployment_target=10.6
    ninja -C out/Debug
    ninja -C out/Release

#### Prepare GYP

.. the same as modern ..

### Building Telegram Desktop

* Launch Xcode, all projects will be taken from **/Users/user/TBuild/tdesktop/Telegram**
* Open MetaEmoji.xcodeproj and build for Debug (Release optionally)
* Open MetaLang.xcodeproj and build for Debug (Release optionally)
* Open Telegram.xcodeproj and build for Debug
* Build Updater target as well, it is required for Telegram relaunch
* Release Telegram build will require removing **CUSTOM_API_ID** definition in Telegram target settings (Apple LLVM 6.1 - Custom Compiler Flags > Other C / C++ Flags > Release)

[xcode]: building-xcode.md
