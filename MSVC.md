##Build instructions for Visual Studio 2013

###Prepare folder

Choose a folder for the future build, for example **D:\TBuild\**. There you will have two folders, **Libraries** for third-party libs and **tdesktop** (or **tdesktop-master**) for the app.

###Clone source code

By git – in [Git Bash](http://git-scm.com/downloads) go to **/d/tbuild** and run

     git clone https://github.com/telegramdesktop/tdesktop.git

or download in ZIP and extract to **D:\TBuild\**, rename **tdesktop-master** to **tdesktop** to have **D:\TBuild\tdesktop\Telegram.sln** solution

###Prepare libraries

####OpenSSL 1.0.1p

http://slproweb.com/products/Win32OpenSSL.html > Download [**Win32 OpenSSL v1.0.1p** (19.8 Mb)](http://slproweb.com/download/Win32OpenSSL-1_0_1p.exe)

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

Extract to **D:\\TBuild\\Libraries\\**

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

####OpenAL Soft, slightly patched

Get sources by git – in [Git Bash](http://git-scm.com/downloads) go to **/d/tbuild/libraries** and run

    git clone git://repo.or.cz/openal-soft.git

to have **D:\TBuild\Libraries\openal-soft\CMakeLists.txt**, then in [Git Bash](http://git-scm.com/downloads) go to **/d/tbuild/libraries/openal-soft** and run

    git checkout 9479ea656b

Apply patch

* OR copy (with overwrite!) everything from **D:\\TBuild\\tdesktop\\\_openal\_patch\\** to **D:\\TBuild\\Libraries\\openal-soft\\**
* OR in Git Bash go to **/d/tbuild/libraries/openal-soft/** and run

    git apply ./../../tdesktop/Telegram/_openal_patch.diff


#####Building library

* Install [CMake](http://www.cmake.org/)
* Go in **cmd** to **D:\TBuild\Libraries\openal-soft\build\**
* Run **cmake -G "Visual Studio 12 2013" -D LIBTYPE:STRING=STATIC ..**
* Open in VS2013 **D:\TBuild\Libraries\openal-soft\build\OpenAL.sln**
* For **Debug** configuration
  * OpenAL32 Properties > C/C++ > Code Generation > Runtime Library = **Multi-threaded Debug (/MTd)** – **OK**
  * common Properties > C/C++ > Code Generation > Runtime Library = **Multi-threaded Debug (/MTd)** – **OK**
* For **Release** configuration
  * OpenAL32 Properties > C/C++ > Code Generation > Runtime Library = **Multi-threaded (/MT)** – **OK**
  * common Properties > C/C++ > Code Generation > Runtime Library = **Multi-threaded (/MT)** – **OK**

####Opus codec

Get sources by git – in [Git Bash](http://git-scm.com/downloads) go to **/d/tbuild/libraries** and run

    git clone https://github.com/telegramdesktop/opus.git

to have **D:\TBuild\Libraries\opus\win32**

#####Building libraries

* Open in VS2013 **D:\TBuild\Libraries\opus\win32\VS2010\opus.sln**
* Build Debug configuration
* Build Release configuration (it will be required in **FFmpeg** build!)

####FFmpeg

https://www.ffmpeg.org/download.html > Download [ffmpeg-2.6.3.tar.bz2](http://ffmpeg.org/releases/ffmpeg-2.6.3.tar.bz2)

Extract to **D:\\TBuild\\Libraries**

http://msys2.github.io/ > Download [msys2-x86_64-20150512.exe](http://sourceforge.net/projects/msys2/files/Base/x86_64/msys2-x86_64-20150512.exe/download) and install to **D:\\msys64**

#####Building libraries

Download [yasm for Win64](http://www.tortall.net/projects/yasm/releases/yasm-1.3.0-win64.exe) from http://yasm.tortall.net/Download.html, rename **yasm-1.3.0-win64.exe** to **yasm.exe** and place it to your Visual C++ **bin** directory, like **\\Program Files (x86)\\Microsoft Visual Studio 12\\VC\\bin\\**

Open **VS2013 x86 Native Tools Command Prompt.bat** (should be in **\\Program Files (x86)\\Microsoft Visual Studio 12.0\\Common7\\Tools\\Shortcuts\\** folder), go to **D:\\msys64\\** and launch **msys2_shell.bat**, there run

    PATH="/c/Program Files (x86)/Microsoft Visual Studio 12.0/VC/BIN:$PATH"

    cd /d/TBuild/Libraries/ffmpeg-2.6.3
    pacman -Sy
    pacman -S msys/make
    pacman -S mingw64/mingw-w64-x86_64-opus
    pacman -S diffutils
    pacman -S pkg-config

    PKG_CONFIG_PATH="/mingw64/lib/pkgconfig:$PKG_CONFIG_PATH"

    ./configure --toolchain=msvc --disable-programs --disable-everything --enable-libopus --enable-decoder=aac --enable-decoder=aac_latm --enable-decoder=aasc --enable-decoder=mp1 --enable-decoder=mp1float --enable-decoder=mp2 --enable-decoder=mp2float --enable-decoder=mp3 --enable-decoder=mp3adu --enable-decoder=mp3adufloat --enable-decoder=mp3float --enable-decoder=mp3on4 --enable-decoder=mp3on4float --enable-decoder=wavpack --enable-decoder=opus --enable-decoder=vorbis --enable-decoder=wmalossless --enable-decoder=wmapro --enable-decoder=wmav1 --enable-decoder=wmav2 --enable-decoder=wmavoice --enable-decoder=flac --enable-encoder=libopus --enable-parser=aac --enable-parser=aac_latm --enable-parser=mpegaudio --enable-parser=opus --enable-parser=vorbis --enable-parser=flac --enable-demuxer=aac --enable-demuxer=wav --enable-demuxer=mp3 --enable-demuxer=ogg --enable-demuxer=mov --enable-demuxer=flac --enable-muxer=ogg --enable-muxer=opus --extra-ldflags="-libpath:/d/TBuild/Libraries/opus/win32/VS2010/Win32/Release celt.lib silk_common.lib silk_float.lib"

    make
    make install

####Qt 5.5.0, slightly patched

http://download.qt-project.org/official_releases/qt/5.5/5.5.0/single/qt-everywhere-opensource-src-5.5.0.zip

Extract to **D:\TBuild\Libraries\**, rename **qt-everywhere-opensource-src-5.5.0** to **QtStatic** to have **D:\TBuild\Libraries\QtStatic\qtbase\** folder

Apply patch

* OR copy (with overwrite!) everything from **D:\TBuild\tdesktop\\\_qt\_5\_5\_0\_patch\** to **D:\TBuild\Libraries\QtStatic\**
* OR in Git Bash go to **/d/TBuild/Libraries/QtStatic/** and run

    git apply ./../../tdesktop/Telegram/_qt_5_5_0_patch.diff

#####Building library

* Install Python 3.3.2 from https://www.python.org/download/releases/3.3.2 > [**Windows x86 MSI Installer (3.3.2)**](https://www.python.org/ftp/python/3.3.2/python-3.3.2.msi)
* Open **VS2013 x86 Native Tools Command Prompt.bat** (should be in **\Program Files (x86)\Microsoft Visual Studio 12.0\Common7\Tools\Shortcuts\** folder)

There go to Qt directory

    D:
    cd TBuild\Libraries\QtStatic

and after that run configure

    configure -debug-and-release -opensource -confirm-license -static -I "D:\TBuild\Libraries\OpenSSL-Win32\include" -L "C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Lib" -l Gdi32 -no-opengl -openssl-linked OPENSSL_LIBS_DEBUG="D:\TBuild\Libraries\OpenSSL-Win32\lib\VC\static\ssleay32MTd.lib D:\TBuild\Libraries\OpenSSL-Win32\lib\VC\static\libeay32MTd.lib" OPENSSL_LIBS_RELEASE="D:\TBuild\Libraries\OpenSSL-Win32\lib\VC\static\ssleay32MT.lib D:\TBuild\Libraries\OpenSSL-Win32\lib\VC\static\libeay32MT.lib" -mp -nomake examples -nomake tests -platform win32-msvc2013

to configure Qt build. After configuration is complete run

    nmake module-qtbase module-qtimageformats
    nmake module-qtbase-install_subtargets module-qtimageformats-install_subtargets

building (**nmake** command) will take really long time.

####Qt Visual Studio Addin 1.2.3

http://download.qt-project.org/official_releases/vsaddin/qt-vs-addin-1.2.3-opensource.exe

Close all VS2013 instances and install to default location

###Building Telegram Desktop

* Launch VS2013 for configuring Qt Addin
* QT5 > Qt Options > Add
  * Version name: **QtStatic.5.5.0**
  * Path: **D:\TBuild\Libraries\QtStatic\qtbase**
* Default Qt/Win version: **QtStatic.5.5.0** – **OK**
* File > Open > Project/Solution > **D:\TBuild\tdesktop\Telegram.sln**
* Build \ Build Solution (Debug and Release configurations)
