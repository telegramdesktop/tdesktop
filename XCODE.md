##Build instructions for Xcode 6.4

###Prepare folder

Choose a folder for the future build, for example **/Users/user/TBuild**   
  
There you will have two folders, **Libraries** for third-party libs and **tdesktop** (or **tdesktop-master**) for the app.

**You will need this hierarchy to be able to follow this README !** 

###Clone source code

By git – in Terminal go to **/Users/user/TBuild** and run:

    git clone https://github.com/telegramdesktop/tdesktop.git

or:
* download in ZIP and extract to **/Users/user/TBuild**   
* rename **tdesktop-master** to **tdesktop**.     

The path to Telegram.xcodeproj should now be: **/Users/user/TBuild/tdesktop/Telegram/Telegram.xcodeproj**

###Prepare libraries

In your build Terminal run:

	MACOSX_DEPLOYMENT_TARGET=10.8

to set minimal supported OS version to 10.8 for future console builds.

####OpenSSL 1.0.1g

#####Get openssl-xcode project file 

From https://github.com/telegramdesktop/openssl-xcode with git in Terminal:

* go to **/Users/user/TBuild/Libraries
* run:
 
		git clone https://github.com/telegramdesktop/openssl-xcode.git

or:

* download in ZIP and extract to **/Users/user/TBuild/Libraries**, 
* rename **openssl-xcode-master** to **openssl-xcode** 

The path to openssl.xcodeproj should now be: **/Users/user/TBuild/Libraries/openssl-xcode/openssl.xcodeproj**

#####Get the source code:

Download [**openssl-1.0.1h.tar.gz**](http://www.openssl.org/source/openssl-1.0.1h.tar.gz) (4.3 Mb)

* Extract openssl-1.0.1h.tar.gz 
* Copy everything from **openssl-1.0.1h** to **/Users/user/TBuild/Libraries/openssl-xcode** 

The folder include of openssl should be:
**/Users/user/TBuild/Libraries/openssl-xcode/include**

#####Building library

* Open **/Users/user/TBuild/Libraries/openssl-xcode/openssl.xcodeproj** with Xcode
* Product > Build

####liblzma
#####Get the source code

Download [**xz-5.0.5.tar.gz**](http://tukaani.org/xz/xz-5.0.5.tar.gz)

Extract to **/Users/user/TBuild/Libraries**

#####Building library

In Terminal go to **/Users/user/TBuild/Libraries/xz-5.0.5** and there run:

    ./configure
    make
    sudo make install

####zlib 1.2.8

Using the system lib

####libexif 0.6.20
#####Get the source code

From https://github.com/telegramdesktop/libexif-0.6.20 with git in Terminal:

*  go to **/Users/user/TBuild/Libraries**
*  run:

    	git clone https://github.com/telegramdesktop/libexif-0.6.20.git

or:

* download in ZIP 
* extract to **/Users/user/TBuild/Libraries**
* rename **libexif-0.6.20-master** to **libexif-0.6.20** 

The folder configure should have this path:
**/Users/user/TBuild/Libraries/libexif-0.6.20/configure**

#####Building library

In Terminal go to **/Users/user/TBuild/Libraries/libexif-0.6.20** and there run

    ./configure
    make
    sudo make install

####OpenAL Soft

Get sources by git – in Terminal go to **/Users/user/TBuild/Libraries** and run

    git clone git://repo.or.cz/openal-soft.git

to have **/Users/user/TBuild/Libraries/openal-soft/CMakeLists.txt**

#####Building library

In Terminal go to **/Users/user/TBuild/Libraries/openal-soft/build** and there run

    cmake -D LIBTYPE:STRING=STATIC -D CMAKE_OSX_DEPLOYMENT_TARGET:STRING=10.8 ..
    make
    sudo make install

####Opus codec
#####Get the source code

* Download sources [opus-1.1.tar.gz](http://downloads.xiph.org/releases/opus/opus-1.1.tar.gz) from http://www.opus-codec.org/downloads/ 
* Extract them to **/Users/user/TBuild/Libraries** 
* Rename opus-1.1 to opus to have **/Users/user/TBuild/Libraries/opus/configure**

#####Building library

* Download [pkg-config 0.28](http://pkgconfig.freedesktop.org/releases/pkg-config-0.28.tar.gz) from http://pkg-config.freedesktop.org
* Extract it to **/Users/user/TBuild/Libraries**

In Terminal go to **/Users/user/TBuild/Libraries/pkg-config-0.28** and run:

    ./configure --with-internal-glib
    make
    sudo make install

then go to **/Users/user/TBuild/Libraries/opus** and run:

    ./configure
    make
    sudo make install

####FFmpeg and Libiconv
#####Get the source code

In Terminal go to **/Users/user/TBuild/Libraries** and run:

    git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg
    cd ffmpeg
    git checkout release/2.8

* Download [libiconv-1.14](http://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.14.tar.gz) from http://www.gnu.org/software/libiconv/#downloading
* Extract to **/Users/user/TBuild/Libraries** to have **/Users/user/TBuild/Libraries/ibiconv-1.14**

#####Building library

In Terminal go to **/Users/user/TBuild/Libraries/libiconv-1.14** and run:

    ./configure --enable-static
    make
    sudo make install

Then in Terminal go to **/Users/user/TBuild/Libraries/ffmpeg** and run:

    ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"

    brew install automake fdk-aac git lame libass libtool libvorbis libvpx opus sdl shtool texi2html theora wget x264 xvid yasm

    CFLAGS=`freetype-config --cflags`
    LDFLAGS=`freetype-config --libs`
    PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/usr/local/lib/pkgconfig:/usr/lib/pkgconfig:/usr/X11/lib/pkgconfig

    ./configure --prefix=/usr/local --disable-programs --disable-doc --disable-everything --disable-pthreads --enable-libopus --enable-decoder=aac --enable-decoder=aac_latm --enable-decoder=aasc --enable-decoder=flac --enable-decoder=gif --enable-decoder=h264 --enable-decoder=h264_vda --enable-decoder=mp1 --enable-decoder=mp1float --enable-decoder=mp2 --enable-decoder=mp2float --enable-decoder=mp3 --enable-decoder=mp3adu --enable-decoder=mp3adufloat --enable-decoder=mp3float --enable-decoder=mp3on4 --enable-decoder=mp3on4float --enable-decoder=mpeg4 --enable-decoder=msmpeg4v2 --enable-decoder=msmpeg4v3 --enable-decoder=opus --enable-decoder=vorbis --enable-decoder=wavpack --enable-decoder=wmalossless --enable-decoder=wmapro --enable-decoder=wmav1 --enable-decoder=wmav2 --enable-decoder=wmavoice --enable-encoder=libopus --enable-hwaccel=mpeg4_videotoolbox --enable-hwaccel=h264_vda --enable-hwaccel=h264_videotoolbox --enable-parser=aac --enable-parser=aac_latm --enable-parser=flac --enable-parser=h264 --enable-parser=mpeg4video --enable-parser=mpegaudio --enable-parser=opus --enable-parser=vorbis --enable-demuxer=aac --enable-demuxer=flac --enable-demuxer=gif --enable-demuxer=h264 --enable-demuxer=mov --enable-demuxer=mp3 --enable-demuxer=ogg --enable-demuxer=wav --enable-muxer=ogg --enable-muxer=opus --extra-cflags="-mmacosx-version-min=10.8" --extra-cxxflags="-mmacosx-version-min=10.8" --extra-ldflags="-mmacosx-version-min=10.8"

    make
    sudo make install

####Qt 5.5.1, slightly patched
#####Get the source code

In Terminal go to **/Users/user/TBuild/Libraries** and run:

    git clone git://code.qt.io/qt/qt5.git QtStatic
    cd QtStatic
    git checkout 5.5
    perl init-repository --module-subset=qtbase,qtimageformats
    git checkout v5.5.1
    cd qtimageformats && git checkout v5.5.1 && cd ..
    cd qtbase && git checkout v5.5.1 && cd ..

#####Apply the patch
From **/Users/user/TBuild/Libraries/QtStatic/qtbase**, run:  

    git apply ../../../tdesktop/Telegram/_qtbase_5_5_1_patch.diff

#####Building library
Go to **/Users/user/TBuild/Libraries/QtStatic** and run:

    ./configure -debug-and-release -opensource -confirm-license -static -opengl desktop -no-openssl -securetransport -nomake examples -nomake tests -platform macx-clang
    make -j4
    sudo make -j4 install

Building (**make** command) will take a really long time.

###Building Telegram Desktop

* Launch Xcode, all projects will be taken from **/Users/user/TBuild/tdesktop/Telegram**
* Open MetaStyle.xcodeproj and build for Debug (Release optionally)
* Open MetaEmoji.xcodeproj and build for Debug (Release optionally)
* Open MetaLang.xcodeproj and build for Debug (Release optionally)
* Open Telegram.xcodeproj and build for Debug
* Build Updater target as well, it is required for Telegram relaunch
* Release Telegram build will require removing **CUSTOM_API_ID** definition in Telegram target settings (Apple LLVM 6.1 - Custom Compiler Flags > Other C / C++ Flags > Release)
