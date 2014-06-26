## [Telegram D](https://tdesktop.com) – Unofficial Telegram Desktop App

This is complete source code and build instructions for alpha version of unofficial desktop client for [Telegram](https://telegram.org) messenger, based on [Telegram API](https://core.telegram.org/) and [MTProto](https://core.telegram.org/mtproto) secure protocol.

Source code is published under GPL v3, license is available [here](https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE).

###Supported systems

Only Windows systems are supported at this moment, OS X and Linux builds are on their way.

* Windows XP
* Windows Vista
* Windows 7
* Windows 8 (**not** RT)
* Windows 8.1 (**not** RT)

###Third-party

* Qt 5.3.0, slightly patched ([GPL](http://qt-project.org/doc/qt-5/gpl.html))
* OpenSSL 1.0.1g ([OpenSSL License](https://www.openssl.org/source/license.html))
* zlib 1.2.8 ([zlib License](http://www.zlib.net/zlib_license.html))
* libexif 0.6.20 ([LGPL](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.en.html))
* LZMA SDK 9.20 ([public domain](http://www.7-zip.org/sdk.html))
* Open Sans font ([Apache License](http://www.apache.org/licenses/LICENSE-2.0.html))

##Build instructions for Visual Studio 2013

###Prepare folder

Choose a folder for the future build, for example **D:\TBuild\**. There you will have two folders, **Libraries** for third-party libs and **tdesktop** (or **tdesktop-master**) for the app.

###Clone source code

By git – in [Git Bash](http://git-scm.com/downloads) go to **/d/tbuild** and run

     git clone https://github.com/telegramdesktop/tdesktop.git

or download in ZIP and extract to **D:\TBuild\**, rename **tdesktop-master** to **tdesktop** to have **D:\TBuild\tdesktop\Telegram.sln** solution

###Prepare libraries

####OpenSSL 1.0.1g

https://www.openssl.org/related/binaries.html > **OpenSSL for Windows** > Download [**Win32 OpenSSL v1.0.1g** (16 Mb)](http://slproweb.com/download/Win32OpenSSL-1_0_1g.exe)

Install to **D:\TBuild\Libraries\OpenSSL-Win32**, while installing **Copy OpenSSL DLLs to** choose **The OpenSSL binaries (/bin) directory**

####LZMA SDK 9.20

http://www.7-zip.org/sdk.html > Download [**LZMA SDK (C, C++, C#, Java)** 9.20](http://downloads.sourceforge.net/sevenzip/lzma920.tar.bz2)

Extract to **D:\TBuild\Libraries**

#####Building library

* Open in VS2013 **D:\TBuild\Libraries\lzma\C\Util\LzmaLib\LzmaLib.dsw** > One-way upgrade – **OK**
* For **Debug** and **Release** configurations
  * LzmaLib Properties > General > Configuration Type = **Static library (.lib)** – **OK**
  * LzmaLib Properties > Librarian > General > Target Machine = **MachineX86 (/MACHINE:X86)** – **OK**
* Build Debug configuration
* Build Release configuration

####zlib 1.2.8

http://www.zlib.net/ > Download [**zlib source code, version 1.2.8, zipfile format**](http://zlib.net/zlib128.zip)

Extract to **D:\TBuild\Libraries\**

#####Building library

* Open in VS2013 **D:\TBuild\Libraries\zlib-1.2.8\contrib\vstudio\vc11\zlibvc.sln** > One-way upgrade – **OK**
* We are interested only in **zlibstat** project, but it depends on some custom pre-build step, so build all
* For **Debug** configuration
  * zlibstat Properties > C/C++ > Code Generation > Runtime Library = **Multi-threaded Debug (/MTd)** – **OK**
* For **Release** configuration
  * zlibstat Properties > C/C++ > Code Generation > Runtime Library = **Multi-threaded (/MT)** – **OK**
* Build Solution for Debug configuration – only **zlibstat** project builds successfully
* Build Solution for Release configuration – only **zlibstat** project builds successfully

####libexif 0.6.20

Get sources from https://github.com/telegramdesktop/libexif-0.6.20, by git – in [Git Bash](http://git-scm.com/downloads) go to **/d/tbuild/libraries** and run

    git clone https://github.com/telegramdesktop/libexif-0.6.20.git

or download in ZIP and extract to **D:\TBuild\Libraries\**, rename **libexif-0.6.20-master** to **libexif-0.6.20** to have **D:\TBuild\Libraries\libexif-0.6.20\win32\lib_exif.sln** solution

#####Building library

* Open in VS2013 **D:\TBuild\Libraries\libexif-0.6.20\win32\lib_exif.sln**
* Build Debug configuration
* Build Release configuration

####Qt 5.3.0, slightly patched

http://download.qt-project.org/official_releases/qt/5.3/5.3.0/single/qt-everywhere-opensource-src-5.3.0.zip

Extract to **D:\TBuild\Libraries\**, rename **qt-everywhere-opensource-src-5.3.0** to **QtStatic** to have **D:\TBuild\Libraries\QtStatic\qtbase\** folder

Apply patch – copy (with overwrite!) everything from **D:\TBuild\tdesktop\\\_qt\_5\_3\_0\_patch\** to **D:\TBuild\Libraries\QtStatic\**

#####Building library

* Install Python 3.3.2 from https://www.python.org/download/releases/3.3.2 > [**Windows x86 MSI Installer (3.3.2)**](https://www.python.org/ftp/python/3.3.2/python-3.3.2.msi)
* Open **VS2013 x86 Native Tools Command Prompt.bat** (should be in **\Program Files (x86)\Microsoft Visual Studio 12.0\Common7\Tools\Shortcuts\** folder)

There go to Qt directory

    D:
    cd TBuild\Libraries\QtStatic

and after that run configure

    configure -debug-and-release -opensource -static -opengl desktop -mp -nomake examples -platform win32-msvc2013
    y

to configure Qt build. After configuration is complete run

    nmake
    nmake install

building (**nmake** command) will take really long time.

####Qt Visual Studio Addin 1.2.3

http://download.qt-project.org/official_releases/vsaddin/qt-vs-addin-1.2.3-opensource.exe

Close all VS2013 instances and install to default location

###Building Telegram Desktop

* Launch VS2013 for configuring Qt Addin
* QT5 > Qt Options > Add
  * Version name: **QtStatic.5.3.0**
  * Path: **D:\TBuild\Libraries\QtStatic\qtbase**
* Default Qt/Win version: **QtStatic.5.3.0** – **OK**
* File > Open > Project/Solution > **D:\TBuild\tdesktop\Telegram.sln**
* Build \ Build Solution (Debug and Release configurations)

##Projects in Telegram solution

####Telegram

tdesktop messenger

####Updater

little app, that is launched by Telegram when update is ready, replaces all files and launches it back

####Packer

compiles given files to single update file, compresses it with lzma and signs with a private key, it is not built in **Debug** and **Release** configurations of Telegram solution, because private key is inaccessible

####Prepare

prepares a release for deployment, puts all files to deploy/{version} folder
* current tsetup{version}exe installer
* current Telegram.exe
* current Telegram.pdb (debug info for crash minidumps view)
* current tupdate{updversion} binary lzma update archive

####MetaEmoji

from two folders
* SourceFiles/art/Emoji
* SourceFiles/art/Emoji_200x

and some inner config creates four sprites and text2emoji replace code
* SourceFiles/art/emoji.png
* SourceFiles/art/emoji_125x.png
* SourceFiles/art/emoji_150x.png
* SourceFiles/art/emoji_200x.png
* SourceFiles/gui/emoji_config.cpp

####MetaStyle

from two files and two sprites
* Resources/style_classes.txt
* Resources/style.txt
* SourceFiles/art/sprite.png
* SourceFiles/art/sprite_200x.png

creates two other sprites, four sprite grids and style constants code
* SourceFiles/art/sprite_125x.png
* SourceFiles/art/sprite_150x.png
* SourceFiles/art/grid.png
* SourceFiles/art/grid_125x.png
* SourceFiles/art/grid_150x.png
* SourceFiles/art/grid_200x.png
* GeneratedFiles/style_classes.h
* GeneratedFiles/style_auto.h
* GeneratedFiles/style_auto.cpp

####MetaLang

from langpack file
* Resources/lang.txt

creates lang constants code and lang file parse code
* GeneratedFiles/lang.h
* GeneratedFiles/lang.cpp
