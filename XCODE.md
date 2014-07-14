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

####Qt 5.3.1, slightly patched

http://download.qt-project.org/official_releases/qt/5.3/5.3.1/single/qt-everywhere-opensource-src-5.3.1.tar.gz

Extract to **/Users/user/TBuild/Libraries**, rename **qt-everywhere-opensource-src-5.3.1** to **QtStatic** to have **/Users/user/TBuild/Libraries/QtStatic/qtbase** folder

Apply patch – copy (with overwrite!) everything from **/Users/user/TBuild/tdesktop/\_qt\_5\_3\_1\_patch/** to **/Users/user/TBuild/Libraries/QtStatic/**

#####Building library

In Terminal go to **/Users/user/TBuild/Libraries/QtStatic** and there run

    ./configure -debug-and-release -opensource -static -opengl desktop -nomake examples -platform macx-clang
    y
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
