from os import system as cmd

"""
Hello, This Is Akash Pattnaik From India.
I Was Facing A Problem While Building TeleGram
So I Decided To Make A Script Which Auto Builds The
App And We Don\'t Need To Write Each Command Again And Again...


N.B : I Made It For Those Who Are Facing Problem While
      Coping Each And Every Code From Github...
      Its Really Easy...

Github : https://github.com/BLUE-DEVIL1134/
Telegram : https://t.me/AKASH_AM1
"""

# Next Part
cmd(
    """SET PATH=%cd%\\ThirdParty\\Strawberry\\perl\\bin;%cd%\ThirdParty\\Python27;%cd%\\ThirdParty\\NASM;%cd%\\ThirdParty\\jom;%cd%\\ThirdParty\\cmake\\bin;%cd%\\ThirdParty\\yasm;%PATH%""")
cmd("git clone --recursive https://github.com/telegramdesktop/tdesktop.git")
cmd("cls")
cmd("mkdir Libraries")
cmd("cd Libraries")
cmd("SET LibrariesPath=%cd%")
cmd("git clone https://github.com/desktop-app/patches.git")
cmd("cd patches")
cmd("git checkout ddd4084")
cmd("cd ..")

# Next Part
cmd("git clone https://github.com/desktop-app/lzma.git")
cmd("cd lzma\\C\\Util\\LzmaLib")
cmd("msbuild LzmaLib.sln /property:Configuration=Debug")
cmd("msbuild LzmaLib.sln /property:Configuration=Release")
cmd("cd ..\\..\\..\\..")

# Next Part
cmd("git clone https://github.com/openssl/openssl.git openssl_1_1_1")
cmd("cd openssl_1_1_1")
cmd("git checkout OpenSSL_1_1_1-stable")
cmd("perl Configure no-shared no-tests debug-VC-WIN32")
cmd("nmake")
cmd("mkdir out32.dbg")
cmd("move libcrypto.lib out32.dbg")
cmd("move libssl.lib out32.dbg")
cmd("move ossl_static.pdb out32.dbg\\ossl_static")
cmd("nmake clean")
cmd("move out32.dbg\\ossl_static out32.dbg\\ossl_static.pdb")
cmd("perl Configure no-shared no-tests VC-WIN32")
cmd("nmake")
cmd("mkdir out32")
cmd("move libcrypto.lib out32")
cmd("move libssl.lib out32")
cmd("move ossl_static.pdb out32")
cmd("cd ..")

# Next Part
cmd("git clone https://github.com/desktop-app/zlib.git")
cmd("cd zlib")
cmd("cd contrib\\vstudio\\vc14")
cmd("msbuild zlibstat.vcxproj /property:Configuration=Debug")
cmd("msbuild zlibstat.vcxproj /property:Configuration=ReleaseWithoutAsm")
cmd("cd ..\\..\\..\\..")

# Next Part
cmd("git clone https://github.com/telegramdesktop/openal-soft.git")
cmd("cd openal-soft")
cmd("git checkout fix_capture")
cmd("cd build")
cmd(
    """cmake .. -G "Visual Studio 16 2019" -A Win32 -D LIBTYPE:STRING=STATIC -D FORCE_STATIC_VCRT=ON -D ALSOFT_BACKEND_WASAPI=OFF""")
cmd("msbuild OpenAL.vcxproj /property:Configuration=Debug")
cmd("msbuild OpenAL.vcxproj /property:Configuration=Release")
cmd("cd ..\..")

# Next Part
cmd("git clone https://github.com/google/breakpad")
cmd("cd breakpad")
cmd("git checkout a1dbcdcb43")
cmd("git apply ../../tdesktop/Telegram/Patches/breakpad.diff")
cmd("cd src")
cmd("git clone https://github.com/google/googletest testing")
cmd("cd client\\windows")
cmd("gyp --no-circular-check breakpad_client.gyp --format=ninja")
cmd("cd ..\\..")
cmd("ninja -C out/Debug common crash_generation_client exception_handler")
cmd("ninja -C out/Release common crash_generation_client exception_handler")
cmd("cd tools\\windows\\dump_syms")
cmd("gyp dump_syms.gyp")
cmd("msbuild dump_syms.vcxproj /property:Configuration=Release")
cmd("cd ..\\..\\..\\..\\..")

# Next Part
cmd("git clone https://github.com/telegramdesktop/opus.git")
cmd("cd opus")
cmd("git checkout tdesktop")
cmd("cd win32\VS2015")
cmd("msbuild opus.sln /property:Configuration=Debug /property:Platform=\"Win32\"")
cmd("msbuild opus.sln /property:Configuration=Release /property:Platform=\"Win32\"")

# Next Part
cmd("cd ..\\..\\..\\..")
cmd("SET PATH_BACKUP_=%PATH%")
cmd("SET PATH=%cd%\\ThirdParty\\msys64\\usr\\bin;%PATH%")
cmd("cd Libraries")

# Next Part
cmd("git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg")
cmd("cd ffmpeg")
cmd("git checkout release/3.4")

# Next part
cmd("set CHERE_INVOKING=enabled_from_arguments")
cmd("set MSYS2_PATH_TYPE=inherit")
cmd("bash --login ../../tdesktop/Telegram/Patches/build_ffmpeg_win.sh")

# Next part
cmd("SET PATH=%PATH_BACKUP_%")
cmd("cd ..")

# Next Part
cmd("git clone git://code.qt.io/qt/qt5.git qt_5_12_8")
cmd("cd qt_5_12_8")
cmd("perl init-repository --module-subset=qtbase,qtimageformats")
cmd("git checkout v5.12.8")
cmd("git submodule update qtbase qtimageformats")
cmd("cd qtbase")
cmd("for /r %i in (..\\..\\patches\\qtbase_5_12_8\\*) do git apply %i")
cmd("cd ..")

# Next Part
cmd("configure -prefix \"%LibrariesPath%\\Qt-5.12.8\" -debug-and-release -force-debug-info -opensource "
    "-confirm-license -static -static-runtime -I \"%LibrariesPath%\\openssl_1_1_1\\include\" -no-opengl "
    "-openssl-linked OPENSSL_LIBS_DEBUG=\"%LibrariesPath%\\openssl_1_1_1\\out32.dbg\\libssl.lib "
    "%LibrariesPath%\\openssl_1_1_1\\out32.dbg\\libcrypto.lib Ws2_32.lib Gdi32.lib Advapi32.lib "
    "Crypt32.lib User32.lib\" OPENSSL_LIBS_RELEASE=\"%LibrariesPath%\\openssl_1_1_1\\out32\\libssl.lib "
    "%LibrariesPath%\\openssl_1_1_1\\out32\\libcrypto.lib Ws2_32.lib Gdi32.lib Advapi32.lib Crypt32.lib "
    "User32.lib\" -mp -nomake examples -nomake tests -platform win32-msvc")

# Next Part
cmd("jom -j4")
cmd("jom -j4 install")
cmd("cd ..")

# Final part
cmd("git clone https://github.com/desktop-app/tg_owt.git")
cmd("cd tg_owt")
cmd("mkdir out")
cmd("cd out")
cmd("mkdir Debug")
cmd("cd Debug")
cmd("cmake -G Ninja "
    "-DCMAKE_BUILD_TYPE=Debug "
    "-DTG_OWT_SPECIAL_TARGET=win "
    "-DTG_OWT_LIBJPEG_INCLUDE_PATH=%cd%/../../../qt_5_12_8/qtbase/src/3rdparty/libjpeg "
    "-DTG_OWT_OPENSSL_INCLUDE_PATH=%cd%/../../../openssl_1_1_1/include "
    "-DTG_OWT_OPUS_INCLUDE_PATH=%cd%/../../../opus/include "
    "-DTG_OWT_FFMPEG_INCLUDE_PATH=%cd%/../../../ffmpeg ../..")
cmd("ninja")
cmd("cd ..")
cmd("mkdir Release")
cmd("cd Release")
cmd("cmake -G Ninja "
    "-DCMAKE_BUILD_TYPE=Release "
    "-DTG_OWT_SPECIAL_TARGET=win "
    "-DTG_OWT_LIBJPEG_INCLUDE_PATH=%cd%/../../../qt_5_12_8/qtbase/src/3rdparty/libjpeg "
    "-DTG_OWT_OPENSSL_INCLUDE_PATH=%cd%/../../../openssl_1_1_1/include "
    "-DTG_OWT_OPUS_INCLUDE_PATH=%cd%/../../../opus/include "
    "-DTG_OWT_FFMPEG_INCLUDE_PATH=%cd%/../../../ffmpeg ../..")
cmd("ninja")
cmd("cd ..\\..\\..")
