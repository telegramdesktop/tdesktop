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

####Qt 5.3.1, slightly patched

http://download.qt-project.org/official_releases/qt/5.3/5.3.1/single/qt-everywhere-opensource-src-5.3.1.zip

Extract to **D:\TBuild\Libraries\**, rename **qt-everywhere-opensource-src-5.3.1** to **QtStatic** to have **D:\TBuild\Libraries\QtStatic\qtbase\** folder

Apply patch – copy (with overwrite!) everything from **D:\TBuild\tdesktop\\\_qt\_5\_3\_1\_patch\** to **D:\TBuild\Libraries\QtStatic\**

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
  * Version name: **QtStatic.5.3.1**
  * Path: **D:\TBuild\Libraries\QtStatic\qtbase**
* Default Qt/Win version: **QtStatic.5.3.1** – **OK**
* File > Open > Project/Solution > **D:\TBuild\tdesktop\Telegram.sln**
* Build \ Build Solution (Debug and Release configurations)
