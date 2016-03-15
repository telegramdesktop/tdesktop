##Build instructions for Visual Studio 2015

###Prepare folder

Choose a folder for the future build, for example **D:\TBuild\**. There you will have two folders, **Libraries** for third-party libs and **tdesktop** (or **tdesktop-master**) for the app.

###Clone source code

By git – in [Git Bash](http://git-scm.com/downloads) go to **/d/tbuild** and run

     git clone https://github.com/telegramdesktop/tdesktop.git

or download in ZIP and extract to **D:\TBuild\**, rename **tdesktop-master** to **tdesktop** to have **D:\TBuild\tdesktop\Telegram.sln** solution

###Prepare libraries

####OpenSSL

Open **VS2015 x86 Native Tools Command Prompt.bat** (should be in **Start Menu > Programs > Visual Studio 2015** menu folder), go to **D:\\TBuild\\Libraries** and run

    git clone https://github.com/openssl/openssl.git
    cd openssl
    git checkout OpenSSL_1_0_1-stable
    git apply ./../../tdesktop/Telegram/_openssl_patch.diff
    perl Configure VC-WIN32 --prefix=D:\TBuild\Libraries\openssl\Release
    ms\do_ms
    nmake -f ms\nt.mak
    nmake -f ms\nt.mak install
    cd ..
    git clone https://github.com/openssl/openssl.git openssl_debug
    cd openssl_debug
    git checkout OpenSSL_1_0_1-stable
    git apply ./../../tdesktop/Telegram/_openssl_patch.diff
    perl Configure debug-VC-WIN32 --prefix=D:\TBuild\Libraries\openssl_debug\Debug
    ms\do_ms
    nmake -f ms\nt.mak
    nmake -f ms\nt.mak install


####LZMA SDK 9.20

http://www.7-zip.org/sdk.html > Download [**LZMA SDK (C, C++, C#, Java)** 9.20](http://downloads.sourceforge.net/sevenzip/lzma920.tar.bz2)

Extract to **D:\TBuild\Libraries**

#####Building library

* Open in VS2015 **D:\TBuild\Libraries\lzma\C\Util\LzmaLib\LzmaLib.dsw** > One-way upgrade – **OK**
* For **Debug** and **Release** configurations
  * LzmaLib Properties > General > Configuration Type = **Static library (.lib)** – **OK**
  * LzmaLib Properties > Librarian > General > Target Machine = **MachineX86 (/MACHINE:X86)** – **OK**
* Build Debug configuration
* Build Release configuration

####zlib 1.2.8

http://www.zlib.net/ > Download [**zlib source code, version 1.2.8, zipfile format**](http://zlib.net/zlib128.zip)

Extract to **D:\\TBuild\\Libraries\\**

#####Building library

* Open in VS2015 **D:\TBuild\Libraries\zlib-1.2.8\contrib\vstudio\vc11\zlibvc.sln** > One-way upgrade – **OK**
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

* Open in VS2015 **D:\TBuild\Libraries\libexif-0.6.20\win32\lib_exif.sln**
* Build Debug configuration
* Build Release configuration

####OpenAL Soft, slightly patched

Open **VS2015 x86 Native Tools Command Prompt.bat** (should be in **Start Menu > Programs > Visual Studio 2015** menu folder), go to **D:\\TBuild\\Libraries** and run

    git clone git://repo.or.cz/openal-soft.git
    git checkout 90349b38
    git apply ./../../tdesktop/Telegram/_openal_patch.diff

#####Building library

* Install [CMake](http://www.cmake.org/)
* Open **VS2015 x86 Native Tools Command Prompt.bat** (should be in **Start Menu > Programs > Visual Studio 2015** menu folder), go to **D:\TBuild\Libraries\openal-soft\build\** and run

    cmake -G "Visual Studio 14 2015" -D LIBTYPE:STRING=STATIC -D FORCE_STATIC_VCRT:STRING=ON ..

* Open in VS2015 **D:\TBuild\Libraries\openal-soft\build\OpenAL.sln** and build Debug and Release configurations

####Opus codec

Get sources by git – in [Git Bash](http://git-scm.com/downloads) go to **/d/tbuild/libraries** and run

    git clone https://github.com/telegramdesktop/opus.git

to have **D:\TBuild\Libraries\opus\win32**

#####Building libraries

* Open in VS2015 **D:\TBuild\Libraries\opus\win32\VS2010\opus.sln**
* Build Debug configuration
* Build Release configuration (it will be required in **FFmpeg** build!)

####FFmpeg

Open **VS2015 x86 Native Tools Command Prompt.bat** (should be in **Start Menu > Programs > Visual Studio 2015** menu folder) and run

    git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg
    cd ffmpeg
    git checkout release/2.8

http://msys2.github.io/ > Download [msys2-x86_64-20150512.exe](http://sourceforge.net/projects/msys2/files/Base/x86_64/msys2-x86_64-20150512.exe/download) and install to **D:\\msys64**

#####Building libraries

Download [yasm for Win64](http://www.tortall.net/projects/yasm/releases/yasm-1.3.0-win64.exe) from http://yasm.tortall.net/Download.html, rename **yasm-1.3.0-win64.exe** to **yasm.exe** and place it to your Visual C++ **bin** directory, like **\\Program Files (x86)\\Microsoft Visual Studio 14\\VC\\bin\\**

Open **VS2015 x86 Native Tools Command Prompt.bat** (should be in **Start Menu > Programs > Visual Studio 2015** menu folder), go to **D:\\msys64\\** and launch **msys2_shell.bat**, there run

    PATH="/c/Program Files (x86)/Microsoft Visual Studio 14.0/VC/BIN:$PATH"

    cd /d/TBuild/Libraries/ffmpeg
    pacman -Sy
    pacman -S msys/make
    pacman -S mingw64/mingw-w64-x86_64-opus
    pacman -S diffutils
    pacman -S pkg-config

    PKG_CONFIG_PATH="/mingw64/lib/pkgconfig:$PKG_CONFIG_PATH"

    ./configure --toolchain=msvc --disable-programs --disable-doc --disable-everything --disable-w32threads --enable-libopus --enable-decoder=aac --enable-decoder=aac_latm --enable-decoder=aasc --enable-decoder=flac --enable-decoder=gif --enable-decoder=h264 --enable-decoder=mp1 --enable-decoder=mp1float --enable-decoder=mp2 --enable-decoder=mp2float --enable-decoder=mp3 --enable-decoder=mp3adu --enable-decoder=mp3adufloat --enable-decoder=mp3float --enable-decoder=mp3on4 --enable-decoder=mp3on4float --enable-decoder=mpeg4 --enable-decoder=msmpeg4v2 --enable-decoder=msmpeg4v3 --enable-decoder=wavpack --enable-decoder=opus --enable-decoder=vorbis --enable-decoder=wmalossless --enable-decoder=wmapro --enable-decoder=wmav1 --enable-decoder=wmav2 --enable-decoder=wmavoice --enable-encoder=libopus --enable-hwaccel=h264_d3d11va --enable-hwaccel=h264_dxva2 --enable-parser=aac --enable-parser=aac_latm --enable-parser=flac --enable-parser=h264 --enable-parser=mpeg4video --enable-parser=mpegaudio --enable-parser=opus --enable-parser=vorbis --enable-demuxer=aac --enable-demuxer=flac --enable-demuxer=gif --enable-demuxer=h264 --enable-demuxer=mov --enable-demuxer=mp3 --enable-demuxer=ogg --enable-demuxer=wav --enable-muxer=ogg --enable-muxer=opus --extra-ldflags="-libpath:/d/TBuild/Libraries/opus/win32/VS2010/Win32/Release celt.lib silk_common.lib silk_float.lib"

    make
    make install

####Qt 5.5.1, slightly patched

* Install Python 3.3.2 from https://www.python.org/download/releases/3.3.2 > [**Windows x86 MSI Installer (3.3.2)**](https://www.python.org/ftp/python/3.3.2/python-3.3.2.msi)
* Open **VS2015 x86 Native Tools Command Prompt.bat** (should be in **Start Menu > Programs > Visual Studio 2015** menu folder)

There go to Libraries directory

    D:
    cd TBuild\Libraries

and run

    git clone git://code.qt.io/qt/qt5.git QtStatic
    cd QtStatic
    git checkout 5.5
    perl init-repository --module-subset=qtbase,qtimageformats
    git checkout v5.5.1
    cd qtimageformats && git checkout v5.5.1 && cd ..
    cd qtbase && git checkout v5.5.1 && cd ..

#####Apply the patch

    cd qtbase && git apply ../../../tdesktop/Telegram/_qtbase_5_5_1_patch.diff && cd ..

#####Install Windows SDKs

If you didn't install Windows SDKs before, you need to install them now. To install the SDKs just open Telegram solution at **D:\TBuild\tdesktop\Telegram.sln** and on startup Visual Studio 2015 will popup dialog box and ask to download and install extra components (including Windows 7 SDK).

If you already have Windows SDKs then find the library folder and correct it at configure's command below (like **C:\Program Files (x86)\Windows Kits\8.0\Lib\win8\um\x86**).

#####Building library

    configure -debug-and-release -opensource -confirm-license -static -I "D:\TBuild\Libraries\openssl\Release\include" -L "C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Lib" -l Gdi32 -no-opengl -openssl-linked OPENSSL_LIBS_DEBUG="D:\TBuild\Libraries\openssl_debug\Debug\lib\ssleay32.lib D:\TBuild\Libraries\openssl_debug\Debug\lib\libeay32.lib" OPENSSL_LIBS_RELEASE="D:\TBuild\Libraries\openssl\Release\lib\ssleay32.lib D:\TBuild\Libraries\openssl\Release\lib\libeay32.lib" -mp -nomake examples -nomake tests -platform win32-msvc2015
    nmake
    nmake install

building (**nmake** command) will take really long time.

####Qt5Package

https://visualstudiogallery.msdn.microsoft.com/c89ff880-8509-47a4-a262-e4fa07168408

Download, close all VS2015 instances and install for VS2015

####Google Breakpad

* Install Python 2.7.11 from https://www.python.org/downloads/release/python-2711/ > [**Windows x86 MSI installer**](https://www.python.org/ftp/python/2.7.11/python-2.7.11.msi)
* Open **VS2015 x86 Native Tools Command Prompt.bat** (should be in **Start Menu > Programs > Visual Studio 2015** menu folder)

There go to Libraries directory

    D:
    cd TBuild\Libraries

and run

    git clone https://chromium.googlesource.com/breakpad/breakpad
    git clone https://chromium.googlesource.com/external/gyp
    set PATH=C:\Python27;%PATH%
    cd breakpad/src/client/windows
    ..\..\..\..\gyp\gyp --no-circular-check

#####Building library

* Open in VS2015 **D:\TBuild\Libraries\breakpad\src\client\windows\breakpad_client.sln**
* Change "Treat WChar_t As Built in Type" to "No" in all projects & configurations
* Change "Treat Warnings As Errors" to "No" in all projects & configurations
* Build Debug configuration
* Build Release configuration

###Building Telegram Desktop

* Launch VS2015 for configuring Qt5Package
* QT5 > Qt Options > Add
  * Version name: **QtStatic.5.5.1**
  * Path: **D:\TBuild\Libraries\QtStatic\qtbase**
* Default Qt/Win version: **QtStatic.5.5.1** – **OK**
* File > Open > Project/Solution > **D:\TBuild\tdesktop\Telegram.sln**
* Build \ Build Solution (Debug and Release configurations)
