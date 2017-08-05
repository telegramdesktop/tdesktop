# Build instructions for Visual Studio 2015

- [Prepare folder](#prepare-folder)
- [Clone source code](#clone-source-code)
- [Prepare libraries](#prepare-libraries)
  * [OpenSSL](#openssl)
  * [LZMA SDK 9.20](#lzma-sdk-920)
    + [Building library](#building-library)
  * [zlib 1.2.8](#zlib-128)
    + [Building library](#building-library-1)
  * [libexif 0.6.20](#libexif-0620)
    + [Building library](#building-library-2)
  * [OpenAL Soft, slightly patched](#openal-soft-slightly-patched)
    + [Building library](#building-library-3)
  * [Opus codec](#opus-codec)
    + [Building libraries](#building-libraries)
  * [FFmpeg](#ffmpeg)
    + [Building libraries](#building-libraries-1)
  * [Qt 5.6.2, slightly patched](#qt-562-slightly-patched)
    + [Apply the patch](#apply-the-patch)
    + [Install Windows SDKs](#install-windows-sdks)
    + [Building library](#building-library-4)
  * [Qt5Package](#qt5package)
  * [Google Breakpad](#google-breakpad)
    + [Installation](#installation)
      - [depot_tools](#depot_tools)
      - [Breakpad](#breakpad)
    + [Build](#build)
- [Building Telegram Desktop](#building-telegram-desktop)
    + [Setup GYP/Ninja and generate VS solution](#setup-gypninja-and-generate-vs-solution)
    + [Configure VS](#configure-vs)
    + [Build the project](#build-the-project)

## Prepare folder

Choose a folder for the future build, for example **D:\\TBuild\\**. There you will have two folders, **Libraries** for third-party libs and **tdesktop** for the app.

All commands (if not stated otherwise) will be launched from **VS2015 x86 Native Tools Command Prompt.bat** (should be in **Start Menu > Programs > Visual Studio 2015** menu folder). Pay attention not to use another VS2015 Command Prompt.

#### A note on D:\\TBuild

***In case you don't have a *D* drive, or prefer to use another working directory.***
Since all of the examples and commands here are using the *D* drive, you might find it more comfortable to map the drive to a folder you of your choice.
For example, to map *D:\\* to *C:\\base_folder_for_telegram_dev*, open the cmd and execute: `subst D: C:\base_folder_for_telegram_dev`.

## Clone source code

Go to **D:\\TBuild**

    D:
    cd TBuild

and run

    git clone --recursive https://github.com/telegramdesktop/tdesktop.git
    mkdir Libraries

## Prepare libraries

### OpenSSL

Go to **D:\\TBuild\\Libraries** and run

    git clone https://github.com/openssl/openssl.git
    cd openssl
    git checkout OpenSSL_1_0_1-stable
    perl Configure VC-WIN32 --prefix=D:\TBuild\Libraries\openssl\Release
    ms\do_ms
    nmake -f ms\nt.mak
    nmake -f ms\nt.mak install
    cd ..
    git clone https://github.com/openssl/openssl.git openssl_debug
    cd openssl_debug
    git checkout OpenSSL_1_0_1-stable
    perl Configure debug-VC-WIN32 --prefix=D:\TBuild\Libraries\openssl_debug\Debug
    ms\do_ms
    nmake -f ms\nt.mak
    nmake -f ms\nt.mak install

### LZMA SDK 9.20

http://www.7-zip.org/sdk.html > Download [**LZMA SDK (C, C++, C#, Java)** 9.20](http://downloads.sourceforge.net/sevenzip/lzma920.tar.bz2)

Extract to **D:\\TBuild\\Libraries**

#### Building library

* Open in VS2015 **D:\\TBuild\\Libraries\\lzma\\C\\Util\\LzmaLib\\LzmaLib.dsw** > One-way upgrade – **OK**
* For **Debug** and **Release** configurations
  * LzmaLib Properties > General > Configuration Type = **Static library (.lib)** – **Apply**
  * LzmaLib Properties > Librarian > General > Target Machine = **MachineX86 (/MACHINE:X86)** – **OK**
  * If you can't find **Librarian**, you forgot to click **Apply** after changing the Configuration Type.
* For **Debug** configuration
  * LzmaLib Properties > C/C++ > General > Debug Information Format = **Program Database (/Zi)** - **OK**
* Build Debug configuration
* Build Release configuration

### zlib 1.2.8

http://www.zlib.net/fossils/ > Download [zlib-1.2.8.tar.gz](http://www.zlib.net/fossils/zlib-1.2.8.tar.gz)

Extract to **D:\\TBuild\\Libraries**

#### Building library

* Open in VS2015 **D:\\TBuild\\Libraries\\zlib-1.2.8\\contrib\\vstudio\\vc11\\zlibvc.sln** > One-way upgrade – **OK**
* We are interested only in **zlibstat** project, but it depends on some custom pre-build step, so build all
* For **Debug** configuration
  * zlibstat Properties > C/C++ > Code Generation > Runtime Library = **Multi-threaded Debug (/MTd)** – **OK**
* For **Release** configuration
  * zlibstat Properties > C/C++ > Code Generation > Runtime Library = **Multi-threaded (/MT)** – **OK**
* Build Solution for Debug configuration – only **zlibstat** project builds successfully
* Build Solution for Release configuration – only **zlibstat** project builds successfully

### libexif 0.6.20

Go to **D:\\TBuild\\Libraries** and run

    git clone https://github.com/telegramdesktop/libexif-0.6.20.git

#### Building library

* Open in VS2015 **D:\\TBuild\\Libraries\\libexif-0.6.20\\win32\\lib_exif.sln**
* Build Debug configuration
* Build Release configuration

### OpenAL Soft, slightly patched

Go to **D:\\TBuild\\Libraries** and run

    git clone git://repo.or.cz/openal-soft.git
    cd openal-soft
    git checkout 18bb46163af
    git apply ./../../tdesktop/Telegram/Patches/openal.diff

#### Building library

* Install [CMake](http://www.cmake.org/)
* Go to **D:\\TBuild\\Libraries\\openal-soft\\build** and run

<!-- -->

    cmake -G "Visual Studio 14 2015" -D LIBTYPE:STRING=STATIC -D FORCE_STATIC_VCRT:STRING=ON ..

* Open in VS2015 **D:\\TBuild\\Libraries\\openal-soft\\build\\OpenAL.sln** and build Debug and Release configurations

### Opus codec

Go to **D:\\TBuild\\Libraries** and run

    git clone https://github.com/telegramdesktop/opus.git
    cd opus
    git checkout ffmpeg_fix

#### Building libraries

* Open in VS2015 **D:\\TBuild\\Libraries\\opus\\win32\\VS2015\\opus.sln**
* Build Debug configuration
* Build Release configuration (it will be required in **FFmpeg** build!)

### FFmpeg

Go to **D:\\TBuild\\Libraries** and run

    git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg
    cd ffmpeg
    git checkout release/3.2

http://www.msys2.org/ > Download the latest version and install it to **D:\\msys64**

#### Building libraries

Download [yasm for Win64](http://www.tortall.net/projects/yasm/releases/yasm-1.3.0-win64.exe) from http://yasm.tortall.net/Download.html, rename **yasm-1.3.0-win64.exe** to **yasm.exe** and place it to your Visual C++ **bin** directory, like **\\Program Files (x86)\\Microsoft Visual Studio 14\\VC\\bin\\**

While still running the VS2015 x86 Native Tools Command Prompt, go to **D:\\msys64** and launch **msys2_shell.bat**. There run

    PATH="/c/Program Files (x86)/Microsoft Visual Studio 14.0/VC/BIN:$PATH"

    cd /d/TBuild/Libraries/ffmpeg
    pacman -Sy
    pacman -S msys/make
    pacman -S mingw64/mingw-w64-x86_64-opus
    pacman -S diffutils
    pacman -S pkg-config

    PKG_CONFIG_PATH="/mingw64/lib/pkgconfig:$PKG_CONFIG_PATH"

    ./configure --toolchain=msvc --disable-programs --disable-doc --disable-everything --enable-protocol=file --enable-libopus --enable-decoder=aac --enable-decoder=aac_latm --enable-decoder=aasc --enable-decoder=flac --enable-decoder=gif --enable-decoder=h264 --enable-decoder=mp1 --enable-decoder=mp1float --enable-decoder=mp2 --enable-decoder=mp2float --enable-decoder=mp3 --enable-decoder=mp3adu --enable-decoder=mp3adufloat --enable-decoder=mp3float --enable-decoder=mp3on4 --enable-decoder=mp3on4float --enable-decoder=mpeg4 --enable-decoder=msmpeg4v2 --enable-decoder=msmpeg4v3 --enable-decoder=wavpack --enable-decoder=opus --enable-decoder=pcm_alaw --enable-decoder=pcm_alaw_at --enable-decoder=pcm_f32be --enable-decoder=pcm_f32le --enable-decoder=pcm_f64be --enable-decoder=pcm_f64le --enable-decoder=pcm_lxf --enable-decoder=pcm_mulaw --enable-decoder=pcm_mulaw_at --enable-decoder=pcm_s16be --enable-decoder=pcm_s16be_planar --enable-decoder=pcm_s16le --enable-decoder=pcm_s16le_planar --enable-decoder=pcm_s24be --enable-decoder=pcm_s24daud --enable-decoder=pcm_s24le --enable-decoder=pcm_s24le_planar --enable-decoder=pcm_s32be --enable-decoder=pcm_s32le --enable-decoder=pcm_s32le_planar --enable-decoder=pcm_s64be --enable-decoder=pcm_s64le --enable-decoder=pcm_s8 --enable-decoder=pcm_s8_planar --enable-decoder=pcm_u16be --enable-decoder=pcm_u16le --enable-decoder=pcm_u24be --enable-decoder=pcm_u24le --enable-decoder=pcm_u32be --enable-decoder=pcm_u32le --enable-decoder=pcm_u8 --enable-decoder=pcm_zork --enable-decoder=vorbis --enable-decoder=wmalossless --enable-decoder=wmapro --enable-decoder=wmav1 --enable-decoder=wmav2 --enable-decoder=wmavoice --enable-encoder=libopus --enable-hwaccel=h264_d3d11va --enable-hwaccel=h264_dxva2 --enable-parser=aac --enable-parser=aac_latm --enable-parser=flac --enable-parser=h264 --enable-parser=mpeg4video --enable-parser=mpegaudio --enable-parser=opus --enable-parser=vorbis --enable-demuxer=aac --enable-demuxer=flac --enable-demuxer=gif --enable-demuxer=h264 --enable-demuxer=mov --enable-demuxer=mp3 --enable-demuxer=ogg --enable-demuxer=wav --enable-muxer=ogg --enable-muxer=opus --extra-ldflags="-libpath:/d/TBuild/Libraries/opus/win32/VS2015/Win32/Release"

    make
    make install

### Qt 5.6.2, slightly patched

* Install Python 3.3.2 from https://www.python.org/download/releases/3.3.2 > [**Windows x86 MSI Installer (3.3.2)**](https://www.python.org/ftp/python/3.3.2/python-3.3.2.msi)
* Go to **D:\\TBuild\\Libraries** and run

<!-- -->

    git clone git://code.qt.io/qt/qt5.git qt5_6_2
    cd qt5_6_2
    perl init-repository --module-subset=qtbase,qtimageformats
    git checkout v5.6.2
    cd qtimageformats && git checkout v5.6.2 && cd ..
    cd qtbase && git checkout v5.6.2 && cd ..

#### Apply the patch

    cd qtbase && git apply ../../../tdesktop/Telegram/Patches/qtbase_5_6_2.diff && cd ..

#### Install Windows SDKs

If you didn't install Windows SDKs before, you need to install them now. To install the SDKs just open Telegram solution at **D:\TBuild\tdesktop\Telegram.sln** ([jump here to generate Telegram.sln](#setup-gypninja-and-generate-vs-solution)) and on startup Visual Studio 2015 will popup dialog box and ask to download and install extra components (including Windows 7 SDK).

If you already have Windows SDKs then find the library folder and correct it at configure's command below (like **C:\Program Files (x86)\Windows Kits\8.0\Lib\win8\um\x86**).

#### Building library

Go to **D:\\TBuild\\Libraries\\qt5_6_2** and run

    configure -debug-and-release -force-debug-info -opensource -confirm-license -static -I "D:\TBuild\Libraries\openssl\Release\include" -L "C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Lib" -l Gdi32 -no-opengl -openssl-linked OPENSSL_LIBS_DEBUG="D:\TBuild\Libraries\openssl_debug\Debug\lib\ssleay32.lib D:\TBuild\Libraries\openssl_debug\Debug\lib\libeay32.lib" OPENSSL_LIBS_RELEASE="D:\TBuild\Libraries\openssl\Release\lib\ssleay32.lib D:\TBuild\Libraries\openssl\Release\lib\libeay32.lib" -mp -nomake examples -nomake tests -platform win32-msvc2015
    nmake
    nmake install

building (**nmake** command) will take really long time.

### Qt5Package

https://visualstudiogallery.msdn.microsoft.com/c89ff880-8509-47a4-a262-e4fa07168408

Download, close all VS2015 instances and install for VS2015

### Google Breakpad

Breakpad is a set of client and server components which implement a crash-reporting system.

#### Installation

##### depot_tools

Go to `D:\TBuild\Libraries` and run

    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
    cd depot_tools
    gclient

If you get errors like

    Cannot rebase: You have unstaged changes.
    Please commit or stash them.
    Failed to update depot_tools.

run `git reset --hard HEAD` and execute `gclient` again

##### Breakpad

    cd ..
    mkdir breakpad && cd breakpad
    ..\depot_tools\fetch breakpad
    ..\depot_tools\gclient sync
    xcopy src\src\* src /s /i
    rmdir src\src /s /q

#### Build

* Open `D:\TBuild\Libraries\breakpad\src\client\windows\breakpad_client.sln` in VS2015
* Change `Treat WChar_t As Built in Type` to `No` in all projects & configurations (should be in Project -> Properties -> C/C++ -> Language)
* Change `Treat Warnings As Errors` to `No` in all projects & configurations (should be in Project -> Properties -> C/C++ -> General)
* Build Debug configuration
* Build Release configuration

## Building Telegram Desktop

#### Setup GYP/Ninja and generate VS solution

* Download [Ninja binaries](https://github.com/ninja-build/ninja/releases/download/v1.7.1/ninja-win.zip) and unpack them to **D:\\TBuild\\Libraries\\ninja** to have **D:\\TBuild\\Libraries\\ninja\\ninja.exe**
* Go to **D:\\TBuild\\Libraries** and run

<!-- -->

    git clone https://chromium.googlesource.com/external/gyp
    cd gyp
    git checkout a478c1ab51
    SET PATH=%PATH%;D:\TBuild\Libraries\gyp;D:\TBuild\Libraries\ninja;
    cd ..\..\tdesktop\Telegram

Also, actually add **D:\\TBuild\\Libraries\\ninja\\** (not just for running the **gyp** command) to your path environment variable, since Telegram needs it for the build process.

If you want to pass a build define (like `TDESKTOP_DISABLE_AUTOUPDATE` or `TDESKTOP_DISABLE_NETWORK_PROXY`), call `set TDESKTOP_BUILD_DEFINES=TDESKTOP_DISABLE_AUTOUPDATE,TDESKTOP_DISABLE_NETWORK_PROXY,...` (comma seperated string)

After, call `gyp\refresh.bat` (python 2.7 needed)

#### Configure VS

* Launch VS2015 for configuring Qt5Package
* QT5 > Qt Options > Add
  * Version name: **Qt 5.6.22Win32**
  * Path: **D:\TBuild\Libraries\qt5_6_2\qtbase**
    * If you made a network map, here you should use the real path! (e.g *C:\base_folder_for_telegram_dev* or what you used at the beginning)
* Default Qt/Win version: **Qt 5.6.2 Win32** – **OK** - You may need to restart Visual Studio for this to take effect.

#### Build the project

* File > Open > Project/Solution > **D:\TBuild\tdesktop\Telegram\Telegram.sln**
* Select Telegram project and press Build > Build Telegram (Debug and Release configurations)
* The result Telegram.exe will be located in **D:\TBuild\tdesktop\out\Debug** (and **Release**)
