##Build instructions for Qt Creator 2.7 under Ubuntu 14.04

###Prepare

* Install git by command **sudo apt-get install git** in Terminal
* Install g++ by command **sudo apt-get install g++** in Terminal
* Install Qt Creator from Ubuntu Software Center

###Prepare folder

Choose a folder for the future build, for example **/home/user/TBuild** There you will have two folders, **Libraries** for third-party libs and **tdesktop** (or **tdesktop-master**) for the app.

###Clone source code

By git – in Terminal go to **/home/user/TBuild** and run

     git clone https://github.com/telegramdesktop/tdesktop.git

or download in ZIP and extract to **/home/user/TBuild** rename **tdesktop-master** to **tdesktop** to have **/home/user/TBuild/tdesktop/Telegram/Telegram.pro** project

###Prepare libraries

####OpenSSL 1.0.1g

http://www.openssl.org/source/ > Download [**openssl-1.0.1h.tar.gz**](http://www.openssl.org/source/openssl-1.0.1h.tar.gz) (4.3 Mb)

Extract openssl-1.0.1h.tar.gz to **/home/user/TBuild/Libraries**

#####Building library

In Terminal go to **/home/user/TBuild/Libraries/openssl-1.0.1h** and there run

    ./config
    make
    sudo make install

####liblzma

http://tukaani.org/xz/ > Download [**xz-5.0.5.tar.gz**](http://tukaani.org/xz/xz-5.0.5.tar.gz)

Extract to **/home/user/TBuild/Libraries**

#####Building library

In Terminal go to **/home/user/TBuild/Libraries/xz-5.0.5** and there run

    ./configure
    make
    sudo make install

####zlib 1.2.8

http://zlib.net/ > Download [**zlib source code, version 1.2.8, tar.gz format**](http://zlib.net/zlib-1.2.8.tar.gz)

Extract to **/home/user/TBuild/Libraries**

#####Building library

In Terminal go to **/home/user/TBuild/Libraries/zlib-1.2.8** and there run

    ./configure
    make
    sudo make install

####libexif 0.6.20

Get sources from https://github.com/telegramdesktop/libexif-0.6.20, by git – in Terminal go to **/home/user/TBuild/Libraries** and run

    git clone https://github.com/telegramdesktop/libexif-0.6.20.git

or download in ZIP and extract to **/home/user/TBuild/Libraries**, rename **libexif-0.6.20-master** to **libexif-0.6.20** to have **/home/user/TBuild/Libraries/libexif-0.6.20/configure** script

#####Building library

In Terminal go to **/home/user/TBuild/Libraries/libexif-0.6.20** and there run

    ./configure
    make
    sudo make install

####Qt 5.3.1, slightly patched

http://download.qt-project.org/official_releases/qt/5.3/5.3.1/single/qt-everywhere-opensource-src-5.3.1.tar.gz

Extract to **/home/user/TBuild/Libraries**, rename **qt-everywhere-opensource-src-5.3.1** to **QtStatic** to have **/home/user/TBuild/Libraries/QtStatic/qtbase** folder

Apply patch – copy (with overwrite!) everything from **/home/user/TBuild/tdesktop/\_qt\_5\_3\_1\_patch/** to **/home/user/TBuild/Libraries/QtStatic/**

#####Building library

Install some packages for Qt (see **/home/user/TBuild/Libraries/QtStatic/qtbase/src/plugins/platforms/xcb/README**)

    sudo apt-get install libxcb-image0-dev
    sudo apt-get install libxcb-keysyms1-dev
    sudo apt-get install libxcb-icccm4-dev
    sudo apt-get install libxcb-render-util0-dev
    sudo apt-get install libxrender-dev
    sudo apt-get install libpulse-dev

In Terminal go to **/home/user/TBuild/Libraries/QtStatic** and there run

    ./configure -release -opensource -no-opengl -static -nomake examples -skip qtquick1 -skip qtdeclarative
    y
    make
    sudo make install

building (**make** command) will take really long time.

In Ubuntu under Parallels Desktop there was a problem with libGL, bad symlink **/usr/lib/x86_64-linux-gnu/mesa/libGL.so** to unexisting file, had to fix it manually like this

    sudo ln -sf libGL.so.1 /usr/lib/x86_64-linux-gnu/mesa/libGL.so
    
because only **/usr/lib/x86_64-linux-gnu/mesa/libGL.so.1** existed.

#####Building pulseaudio plugin

In Terminal go to **/home/user/TBuild/Libraries/QtStatic/qtmultimedia/src/plugins/pulseaudio** and run

    qmake pulseaudio.pro
    make

###Building Telegram Desktop

* Launch Qt Creator, all projects will be taken from **/home/user/TBuild/tdesktop/Telegram**
* Tools > Options > Build & Run > Qt Versions tab > Add > File System /usr/local/Qt-5.3.1/bin/qmake > **Qt 5.3.1 (Qt-5.3.1)** > Apply
* Tools > Options > Build & Run > Kits tab > Desktop (default) > change **Qt version** to **Qt 5.3.1 (Qt-5.3.1)** > Apply
* Open MetaStyle.pro, configure project with paths **/home/user/TBuild/tdesktop/Linux/DebugIntermediateStyle** and **/home/user/TBuild/tdesktop/Linux/ReleaseIntermediateStyle** and build for Debug
* Open MetaEmoji.pro, configure project with paths **/home/user/TBuild/tdesktop/Linux/DebugIntermediateEmoji** and **/home/user/TBuild/tdesktop/Linux/ReleaseIntermediateEmoji** and build for Debug
* Open MetaLang.pro, configure project with paths **/home/user/TBuild/tdesktop/Linux/DebugIntermediateLang** and **/home/user/TBuild/tdesktop/Linux/ReleaseIntermediateLang** and build for Debug
* Open Telegram.pro, configure project with paths **/home/user/TBuild/tdesktop/Linux/DebugIntermediate** and **/home/user/TBuild/tdesktop/Linux/ReleaseIntermediate** and build for Debug
* Open Updater.pro, configure project with paths **/home/user/TBuild/tdesktop/Linux/DebugIntermediateUpdater** and **/home/user/TBuild/tdesktop/Linux/ReleaseIntermediateUpdater** and build for Debug
* Release Telegram build will require removing **CUSTOM_API_ID** definition in Telegram.pro project and may require changing paths in **/home/user/TBuild/tdesktop/Telegram/FixMake.sh** for static library linking fix
