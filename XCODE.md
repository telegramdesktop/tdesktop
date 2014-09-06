##Build instructions for Xcode 5.1.1

###Prepare folder

Choose a folder for the future build, for example **/Users/user/TBuild** There you will have two folders, **Libraries** for third-party libs and **tdesktop** (or **tdesktop-master**) for the app.

###Clone source code

By git – in Terminal go to **/Users/user/TBuild** and run

     git clone https://github.com/telegramdesktop/tdesktop.git

or download in ZIP and extract to **/Users/user/TBuild** rename **tdesktop-master** to **tdesktop** to have **/Users/user/TBuild/tdesktop/Telegram/Telegram.xcodeproj** project

###Prepare libraries

####OpenSSL 1.0.1g

Get sources from https://github.com/telegramdesktop/openssl-xcode, by git – in Terminal go to **/Users/user/TBuild/Libraries** and run

    git clone https://github.com/telegramdesktop/openssl-xcode.git

or download in ZIP and extract to **/Users/user/TBuild/Libraries**, rename **openssl-xcode-master** to **openssl-xcode** to have **/Users/user/TBuild/Libraries/openssl-xcode/openssl.xcodeproj** project

http://www.openssl.org/source/ > Download [**openssl-1.0.1h.tar.gz**](http://www.openssl.org/source/openssl-1.0.1h.tar.gz) (4.3 Mb)

Extract openssl-1.0.1h.tar.gz and copy everything from **openssl-1.0.1h** to **/Users/user/TBuild/Libraries/openssl-xcode** to have **/Users/user/TBuild/Libraries/openssl-xcode/include**

#####Building library

* Open **/Users/user/TBuild/Libraries/openssl-xcode/openssl.xcodeproj** with Xcode
* Product > Build

####liblzma

http://tukaani.org/xz/ > Download [**xz-5.0.5.tar.gz**](http://tukaani.org/xz/xz-5.0.5.tar.gz)

Extract to **/Users/user/TBuild/Libraries**

#####Building library

In Terminal go to **/Users/user/TBuild/Libraries/xz-5.0.5** and there run

    ./configure
    make
    sudo make install

####zlib 1.2.8

Using se system lib

####libexif 0.6.20

Get sources from https://github.com/telegramdesktop/libexif-0.6.20, by git – in Terminal go to **/Users/user/TBuild/Libraries** and run

    git clone https://github.com/telegramdesktop/libexif-0.6.20.git

or download in ZIP and extract to **/Users/user/TBuild/Libraries**, rename **libexif-0.6.20-master** to **libexif-0.6.20** to have **/Users/user/TBuild/Libraries/libexif-0.6.20/configure** script

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

    cmake -D LIBTYPE:STRING=STATIC ..
    make
    sudo make install

####libogg 1.3.2

Get sources from http://xiph.org/downloads/ – in [ZIP](http://downloads.xiph.org/releases/ogg/libogg-1.3.2.zip) and extract to **/Users/user/TBuild/Libraries**

#####Building library

In Terminal go to **/Users/user/TBuild/Libraries/libogg-1.3.2** and there run

    ./configure
    make
    sudo make install

####Opus codec, opusfile

Download sources [opus-1.1.tar.gz](http://downloads.xiph.org/releases/opus/opus-1.1.tar.gz) and [opusfile-0.6.tar.gz](http://downloads.xiph.org/releases/opus/opusfile-0.6.tar.gz) from http://www.opus-codec.org/downloads/, extract to **/Users/user/TBuild/Libraries** and rename to have **/Users/user/TBuild/Libraries/opus/configure** and **/Users/user/TBuild/Libraries/opusfile/configure**

#####Building libraries

Download [pkg-config 0.28](http://pkgconfig.freedesktop.org/releases/pkg-config-0.28.tar.gz) from http://pkg-config.freedesktop.org, extract it to **/Users/user/TBuild/Libraries**

In Terminal go to **/Users/user/TBuild/Libraries/pkg-config-0.28** and run

    ./configure --with-internal-glib
    make
    sudo make install

then go to **/Users/user/TBuild/Libraries/opus** and there run

    ./configure
    make
    sudo make install

then go to **/Users/user/TBuild/Libraries/opusfile** and there run

    ./configure
    make
    sudo make install

####Qt 5.3.1, slightly patched

http://download.qt-project.org/official_releases/qt/5.3/5.3.1/single/qt-everywhere-opensource-src-5.3.1.tar.gz

Extract to **/Users/user/TBuild/Libraries**, rename **qt-everywhere-opensource-src-5.3.1** to **QtStatic** to have **/Users/user/TBuild/Libraries/QtStatic/qtbase** folder

Apply patch – copy (with overwrite!) everything from **/Users/user/TBuild/tdesktop/\_qt\_5\_3\_1\_patch/** to **/Users/user/TBuild/Libraries/QtStatic/**

#####Building library

In Terminal go to **/Users/user/TBuild/Libraries/QtStatic** and there run

    ./configure -debug-and-release -opensource -confirm-license -static -opengl desktop -nomake examples -platform macx-clang
    make
    sudo make install

building (**make** command) will take really long time.

###Building Telegram Desktop

* Launch Xcode, all projects will be taken from **/Users/user/TBuild/tdesktop/Telegram**
* Open MetaStyle.xcodeproj and build for Debug (Release optionally)
* Open MetaEmoji.xcodeproj and build for Debug (Release optionally)
* Open MetaLang.xcodeproj and build for Debug (Release optionally)
* Open Telegram.xcodeproj and build for Debug
* Build Updater target as well, it is required for Telegram relaunch
* Release Telegram build will require removing **CUSTOM_API_ID** definition in Telegram target settings (Apple LLVM 5.1 - Custom Compiler Flags > Other C / C++ Flags > Release)
