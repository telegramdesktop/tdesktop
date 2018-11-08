# Build instructions for Visual Studio 2017

- [Prepare folder](#prepare-folder)
- [Install third party software](#install-third-party-software)
- [Clone source code and prepare libraries](#clone-source-code-and-prepare-libraries)
- [Build the project](#build-the-project)
- [Qt Visual Studio Tools](#qt-visual-studio-tools)

## Prepare folder

Choose an empty folder for the future build, for example **D:\\TBuild**. It will be named ***BuildPath*** in the rest of this document. Create two folders there, ***BuildPath*\\ThirdParty** and ***BuildPath*\\Libraries**.

All commands (if not stated otherwise) will be launched from **x86 Native Tools Command Prompt for VS 2017.bat** (should be in **Start Menu > Visual Studio 2017** menu folder). Pay attention not to use any other Command Prompt.

### Obtain your API credentials

You will require **api_id** and **api_hash** to access the Telegram API servers. To learn how to obtain them [click here][api_credentials].

## Install third party software

* Download **ActivePerl** installer from [https://www.activestate.com/activeperl/downloads](https://www.activestate.com/activeperl/downloads) and install to ***BuildPath*\\ThirdParty\\Perl**
* Download **NASM** installer from [http://www.nasm.us](http://www.nasm.us) and install to ***BuildPath*\\ThirdParty\\NASM**
* Download **Yasm** executable from [http://yasm.tortall.net/Download.html](http://yasm.tortall.net/Download.html), rename to *yasm.exe* and put to ***BuildPath*\\ThirdParty\\yasm**
* Download **MSYS2** installer from [http://www.msys2.org/](http://www.msys2.org/) and install to ***BuildPath*\\ThirdParty\\msys64**
* Download **jom** archive from [http://download.qt.io/official_releases/jom/jom.zip](http://download.qt.io/official_releases/jom/jom.zip) and unpack to ***BuildPath*\\ThirdParty\\jom**
* Download **Python 2.7** installer from [https://www.python.org/downloads/](https://www.python.org/downloads/) and install to ***BuildPath*\\ThirdParty\\Python27**
* Download **CMake** installer from [https://cmake.org/download/](https://cmake.org/download/) and install to ***BuildPath*\\ThirdParty\\cmake**
* Download **Ninja** executable from [https://github.com/ninja-build/ninja/releases/download/v1.7.2/ninja-win.zip](https://github.com/ninja-build/ninja/releases/download/v1.7.2/ninja-win.zip) and unpack to ***BuildPath*\\ThirdParty\\Ninja**

Open **x86 Native Tools Command Prompt for VS 2017.bat**, go to ***BuildPath*** and run

    cd ThirdParty
    git clone https://chromium.googlesource.com/external/gyp
    cd gyp
    git checkout a478c1ab51
    cd ..\..

Add **GYP** and **Ninja** to your PATH:

* Open **Control Panel** -> **System** -> **Advanced system settings**
* Press **Environment Variables...**
* Select **Path**
* Press **Edit**
* Add ***BuildPath*\\ThirdParty\\gyp** value
* Add ***BuildPath*\\ThirdParty\\Ninja** value

## Clone source code and prepare libraries

Open **x86 Native Tools Command Prompt for VS 2017.bat**, go to ***BuildPath*** and run

    SET PATH=%cd%\ThirdParty\Perl\bin;%cd%\ThirdParty\Python27;%cd%\ThirdParty\NASM;%cd%\ThirdParty\jom;%cd%\ThirdParty\cmake\bin;%cd%\ThirdParty\yasm;%PATH%

    git clone --recursive https://github.com/telegramdesktop/tdesktop.git

    mkdir Libraries
    cd Libraries

    git clone https://github.com/Microsoft/Range-V3-VS2015 range-v3

    git clone https://github.com/telegramdesktop/lzma.git
    cd lzma\C\Util\LzmaLib
    msbuild LzmaLib.sln /property:Configuration=Debug
    msbuild LzmaLib.sln /property:Configuration=Release
    cd ..\..\..\..

    git clone https://github.com/openssl/openssl.git
    cd openssl
    git checkout OpenSSL_1_0_1-stable
    perl Configure no-shared --prefix=%cd%\Release --openssldir=%cd%\Release VC-WIN32
    ms\do_ms
    nmake -f ms\nt.mak
    nmake -f ms\nt.mak install
    xcopy tmp32\lib.pdb Release\lib\
    nmake -f ms\nt.mak clean
    perl Configure no-shared --prefix=%cd%\Debug --openssldir=%cd%\Debug debug-VC-WIN32
    ms\do_ms
    nmake -f ms\nt.mak
    nmake -f ms\nt.mak install
    xcopy tmp32.dbg\lib.pdb Debug\lib\
    cd ..

    git clone https://github.com/telegramdesktop/zlib.git
    cd zlib
    git checkout tdesktop
    cd contrib\vstudio\vc14
    msbuild zlibstat.vcxproj /property:Configuration=Debug
    msbuild zlibstat.vcxproj /property:Configuration=ReleaseWithoutAsm
    cd ..\..\..\..

    git clone https://github.com/john-preston/openal-soft.git
    cd openal-soft
    git checkout fix_macro
    cd build
    cmake -G "Visual Studio 15 2017" -D LIBTYPE:STRING=STATIC -D FORCE_STATIC_VCRT:STRING=ON ..
    msbuild OpenAL.vcxproj /property:Configuration=Debug
    msbuild OpenAL.vcxproj /property:Configuration=Release
    cd ..\..

    git clone https://github.com/google/breakpad
    cd breakpad
    git checkout a1dbcdcb43
    git apply ../../tdesktop/Telegram/Patches/breakpad.diff
    cd src
    git clone https://github.com/google/googletest testing
    cd client\windows
    set GYP_MSVS_VERSION=2017
    gyp --no-circular-check breakpad_client.gyp --format=ninja
    cd ..\..
    ninja -C out/Debug common crash_generation_client exception_handler
    ninja -C out/Release common crash_generation_client exception_handler
    cd tools\windows\dump_syms
    gyp dump_syms.gyp
    msbuild dump_syms.vcxproj /property:Configuration=Release
    cd ..\..\..\..\..

    git clone https://github.com/telegramdesktop/opus.git
    cd opus
    git checkout tdesktop
    cd win32\VS2015
    msbuild opus.sln /property:Configuration=Debug /property:Platform="Win32"
    msbuild opus.sln /property:Configuration=Release /property:Platform="Win32"

    cd ..\..\..\..
    SET PATH_BACKUP_=%PATH%
    SET PATH=%cd%\ThirdParty\msys64\usr\bin;%PATH%
    cd Libraries

    git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg
    cd ffmpeg
    git checkout release/3.4

    set CHERE_INVOKING=enabled_from_arguments
    set MSYS2_PATH_TYPE=inherit
    bash --login ../../tdesktop/Telegram/Patches/build_ffmpeg_win.sh

    SET PATH=%PATH_BACKUP_%
    cd ..

    git clone git://code.qt.io/qt/qt5.git qt5_6_2
    cd qt5_6_2
    perl init-repository --module-subset=qtbase,qtimageformats
    git checkout v5.6.2
    cd qtimageformats
    git checkout v5.6.2
    cd ..\qtbase
    git checkout v5.6.2
    git apply ../../../tdesktop/Telegram/Patches/qtbase_5_6_2.diff
    cd ..

    configure -debug-and-release -force-debug-info -opensource -confirm-license -static -I "%cd%\..\openssl\Release\include" -no-opengl -openssl-linked OPENSSL_LIBS_DEBUG="%cd%\..\openssl\Debug\lib\ssleay32.lib %cd%\..\openssl\Debug\lib\libeay32.lib" OPENSSL_LIBS_RELEASE="%cd%\..\openssl\Release\lib\ssleay32.lib %cd%\..\openssl\Release\lib\libeay32.lib" -mp -nomake examples -nomake tests -platform win32-msvc2015

    jom -j4
    jom -j4 install
    cd ..

    cd ../tdesktop/Telegram
    gyp\refresh.bat

## Build the project

If you want to pass a build define (like `TDESKTOP_DISABLE_AUTOUPDATE` or `TDESKTOP_DISABLE_NETWORK_PROXY`), call `set TDESKTOP_BUILD_DEFINES=TDESKTOP_DISABLE_AUTOUPDATE,TDESKTOP_DISABLE_NETWORK_PROXY,...` (comma seperated string)

After, call **gyp\refresh.bat** once again.

* Open ***BuildPath*\\tdesktop\\Telegram\\Telegram.sln** in Visual Studio 2017
* Select Telegram project and press Build > Build Telegram (Debug and Release configurations)
* The result Telegram.exe will be located in **D:\TBuild\tdesktop\out\Debug** (and **Release**)

### Qt Visual Studio Tools

For better debugging you may want to install Qt Visual Studio Tools:

* Open **Tools** -> **Extensions and Updates...**
* Go to **Online** tab
* Search for **Qt**
* Install **Qt Visual Studio Tools** extension

[api_credentials]: api_credentials.md
