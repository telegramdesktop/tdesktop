##Build instructions for Qt Creator 3.1.2 under Ubuntu 14.04

###Prepare

* Install git by command **sudo apt-get install git** in Terminal
* Install g++ by command **sudo apt-get install g++** in Terminal
* Install Qt Creator from [**Downloads page**](https://qt-project.org/downloads)

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

Install audio libraries

####libogg-1.3.2

[Download libogg-1.3.2 sources](http://downloads.xiph.org/releases/ogg/libogg-1.3.2.tar.xz) from http://xiph.org/downloads, extract to **/home/user/TBuild/Libraries**, go to **/home/user/TBuild/Libraries/libogg-1.3.2** and run

    ./configure
    make
    sudo make install

####Opus codec 1.1

[Download opus-1.1 sources](http://downloads.xiph.org/releases/opus/opus-1.1.tar.gz) from http://www.opus-codec.org/downloads, extract to **/home/user/TBuild/Libraries**, go to **/home/user/TBuild/Libraries/opus-1.1** and run

    ./configure
    make
    sudo make install

####opusfile-0.6

[Download opusfile-0.6 sources](http://downloads.xiph.org/releases/opus/opusfile-0.6.tar.gz) from http://www.opus-codec.org/downloads, extract to **/home/user/TBuild/Libraries**, go to **/home/user/TBuild/Libraries/opusfile-0.6** and run

    ./configure
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

####Qt 5.4.0, slightly patched

http://download.qt-project.org/official_releases/qt/5.4/5.4.0/single/qt-everywhere-opensource-src-5.4.0.tar.gz

Extract to **/home/user/TBuild/Libraries**, rename **qt-everywhere-opensource-src-5.4.0** to **QtStatic** to have **/home/user/TBuild/Libraries/QtStatic/qtbase** folder

Apply patch:

* OR copy (with overwrite!) everything from **/home/user/TBuild/tdesktop/\_qt\_5\_4\_0\_patch/** to **/home/user/TBuild/Libraries/QtStatic/**
* OR copy **/home/user/TBuild/tdesktop/\_qt\_5\_4\_0\_patch.diff** to **/home/user/TBuild/Libraries/QtStatic/**, go there in Terminal and run

    git apply _qt_5_4_0_patch.diff

#####Building library

Install some packages for Qt (see **/home/user/TBuild/Libraries/QtStatic/qtbase/src/plugins/platforms/xcb/README**)

    sudo apt-get install libxcb1-dev libxcb-image0-dev libxcb-keysyms1-dev libxcb-icccm4-dev libxcb-render-util0-dev libxcb-util0-dev libxrender-dev libasound-dev libpulse-dev libxcb-sync0-dev libxcb-xfixes0-dev libxcb-randr0-dev libx11-xcb-dev

In Terminal go to **/home/user/TBuild/Libraries/QtStatic** and there run

    ./configure -release -opensource -confirm-license -qt-xcb -no-opengl -static -nomake examples -nomake tests -skip qtquick1 -skip qtdeclarative
    make module-qtbase module-qtimageformats
    sudo make module-qtbase-install_subtargets module-qtimageformats-install_subtargets

building (**make** command) will take really long time.

###Building Telegram Desktop

* Launch Qt Creator, all projects will be taken from **/home/user/TBuild/tdesktop/Telegram**
* Tools > Options > Build & Run > Qt Versions tab > Add > File System /usr/local/Qt-5.4.0/bin/qmake > **Qt 5.4.0 (Qt-5.4.0)** > Apply
* Tools > Options > Build & Run > Kits tab > Desktop (default) > change **Qt version** to **Qt 5.4.0 (Qt-5.4.0)** > Apply
* Open MetaStyle.pro, configure project with paths **/home/user/TBuild/tdesktop/Linux/DebugIntermediateStyle** and **/home/user/TBuild/tdesktop/Linux/ReleaseIntermediateStyle** and build for Debug
* Open MetaEmoji.pro, configure project with paths **/home/user/TBuild/tdesktop/Linux/DebugIntermediateEmoji** and **/home/user/TBuild/tdesktop/Linux/ReleaseIntermediateEmoji** and build for Debug
* Open MetaLang.pro, configure project with paths **/home/user/TBuild/tdesktop/Linux/DebugIntermediateLang** and **/home/user/TBuild/tdesktop/Linux/ReleaseIntermediateLang** and build for Debug
* Open Telegram.pro, configure project with paths **/home/user/TBuild/tdesktop/Linux/DebugIntermediate** and **/home/user/TBuild/tdesktop/Linux/ReleaseIntermediate** and build for Debug, if GeneratedFiles are not found click **Run qmake** from **Build** menu and try again
* Open Updater.pro, configure project with paths **/home/user/TBuild/tdesktop/Linux/DebugIntermediateUpdater** and **/home/user/TBuild/tdesktop/Linux/ReleaseIntermediateUpdater** and build for Debug
* Release Telegram build will require removing **CUSTOM_API_ID** definition in Telegram.pro project and may require changing paths in **/home/user/TBuild/tdesktop/Telegram/FixMake.sh** or **/home/user/TBuild/tdesktop/Telegram/FixMake32.sh** for static library linking fix, static linking applies only on second Release build (first uses old Makefile)
