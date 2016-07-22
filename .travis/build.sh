#!/bin/bash

set -e

REPO="$PWD"

BUILD="$REPO/build"
UPSTREAM="$REPO/upstream"
EXTERNAL="$REPO/external"
CACHE="$HOME/travisCacheDir"

QT_PATH="$BUILD/qt"
BREAKPAD_PATH="$EXTERNAL/breakpad"

[[ ! $MAKE_ARGS ]] && MAKE_ARGS="--quiet -j4"
QT_VERSION=5.6.0

run() {
    # Move files to subdir
    cd ..
    mv tdesktop tdesktop2
    mkdir tdesktop
    mv tdesktop2 "$UPSTREAM"

    mkdir "$BUILD"

    downloadLibs
    build
    check
}

# install
downloadLibs() {
    travisStartFold "Download libraries"

    cd "$REPO"
    mkdir external && cd external

    git clone https://chromium.googlesource.com/linux-syscall-support
    git clone https://git.ffmpeg.org/ffmpeg.git
    git clone https://github.com/xkbcommon/libxkbcommon.git
    git clone https://github.com/kcat/openal-soft.git
}

build() {
    travisStartFold "Build libraries"

    # libxkbcommon
    cd "$EXTERNAL/libxkbcommon"
    ./autogen.sh \
        --prefix='/usr/local'
    make $MAKE_ARGS
    sudo make install
    sudo ldconfig

    # ffmpeg
    cd "$EXTERNAL/ffmpeg"
    ./configure \
        --prefix='/usr/local' \
        --disable-debug \
        --disable-programs \
        --disable-doc \
        --disable-everything \
        --enable-gpl \
        --enable-version3 \
        --enable-libopus \
        --enable-decoder=aac \
        --enable-decoder=aac_latm \
        --enable-decoder=aasc \
        --enable-decoder=flac \
        --enable-decoder=gif \
        --enable-decoder=h264 \
        --enable-decoder=h264_vdpau \
        --enable-decoder=mp1 \
        --enable-decoder=mp1float \
        --enable-decoder=mp2 \
        --enable-decoder=mp2float \
        --enable-decoder=mp3 \
        --enable-decoder=mp3adu \
        --enable-decoder=mp3adufloat \
        --enable-decoder=mp3float \
        --enable-decoder=mp3on4 \
        --enable-decoder=mp3on4float \
        --enable-decoder=mpeg4 \
        --enable-decoder=mpeg4_vdpau \
        --enable-decoder=msmpeg4v2 \
        --enable-decoder=msmpeg4v3 \
        --enable-decoder=opus \
        --enable-decoder=vorbis \
        --enable-decoder=wavpack \
        --enable-decoder=wmalossless \
        --enable-decoder=wmapro \
        --enable-decoder=wmav1 \
        --enable-decoder=wmav2 \
        --enable-decoder=wmavoice \
        --enable-encoder=libopus \
        --enable-hwaccel=h264_vaapi \
        --enable-hwaccel=h264_vdpau \
        --enable-hwaccel=mpeg4_vaapi \
        --enable-hwaccel=mpeg4_vdpau \
        --enable-parser=aac \
        --enable-parser=aac_latm \
        --enable-parser=flac \
        --enable-parser=h264 \
        --enable-parser=mpeg4video \
        --enable-parser=mpegaudio \
        --enable-parser=opus \
        --enable-parser=vorbis \
        --enable-demuxer=aac \
        --enable-demuxer=flac \
        --enable-demuxer=gif \
        --enable-demuxer=h264 \
        --enable-demuxer=mov \
        --enable-demuxer=mp3 \
        --enable-demuxer=ogg \
        --enable-demuxer=wav \
        --enable-muxer=ogg \
        --enable-muxer=opus
    make $MAKE_ARGS
    sudo make install
    sudo ldconfig

    # openal_soft
    cd "$EXTERNAL/openal-soft/build"
    cmake \
        -D CMAKE_INSTALL_PREFIX=/usr/local \
        -D CMAKE_BUILD_TYPE=Release \
        -D LIBTYPE=STATIC \
        ..
    make $MAKE_ARGS
    sudo make install
    sudo ldconfig

    # Qt
    getCustomQt

    # Breakpad
    getBreakpad

    travisStartFold "Patch tdesktop"

    # Patch tdesktop
    sed -i 's/CUSTOM_API_ID//g' "$UPSTREAM/Telegram/Telegram.pro"
    sed -i "s,\..*/Libraries/breakpad/,$BREAKPAD_PATH/,g" "$UPSTREAM/Telegram/Telegram.pro"

	local options=""

	if [[ $BUILD_VERSION == *"disable_autoupdate"* ]]; then
		options+="\nDEFINES += TDESKTOP_DISABLE_AUTOUPDATE"
	fi

	if [[ $BUILD_VERSION == *"disable_register_custom_scheme"* ]]; then
		options+="\nDEFINES += TDESKTOP_DISABLE_REGISTER_CUSTOM_SCHEME"
	fi

	if [[ $BUILD_VERSION == *"disable_crash_reports"* ]]; then
		options+="\nDEFINES += TDESKTOP_DISABLE_CRASH_REPORTS"
	fi

	if [[ $BUILD_VERSION == *"disable_network_proxy"* ]]; then
		options+="\nDEFINES += TDESKTOP_DISABLE_NETWORK_PROXY"
	fi

	if [[ $BUILD_VERSION == *"disable_desktop_file_generation"* ]]; then
		options+="\nDEFINES += TDESKTOP_DISABLE_DESKTOP_FILE_GENERATION"
	fi

	if [[ $BUILD_VERSION == *"disable_unity_integration"* ]]; then
		options+="\nDEFINES += TDESKTOP_DISABLE_UNITY_INTEGRATION"
	fi

	info_msg "Build options: ${options}"

	echo -e "${options}" >> "$UPSTREAM/Telegram/Telegram.pro"

    travisStartFold "Build tdesktop"

    buildTelegram
    
    travisEndFold
}

getBreakpad() {
    travisStartFold "Getting breakpad"

    local BREAKPAD_CACHE="$CACHE/breakpad"
    local BREAKPAD_CACHE_FILE="$BREAKPAD_CACHE/.cache.txt"
    
    if [ ! -d "$BREAKPAD_CACHE" ]; then
        mkdir -p "$BREAKPAD_CACHE"
    fi

    ln -sf "$BREAKPAD_CACHE" "$BREAKPAD_PATH"

    if [ -f "$BREAKPAD_CACHE_FILE" ]; then
        info_msg "Using cached breakpad"
        makeBreakpadLink
    else
        buildBreakpad
        sudo touch "$BREAKPAD_CACHE_FILE"
    fi
}

buildBreakpad() {
    info_msg "Downloading and building breakpad"

    cd "$EXTERNAL"
    git clone https://chromium.googlesource.com/breakpad/breakpad

    makeBreakpadLink
    cd "$BREAKPAD_PATH"
    ./configure
    make $MAKE_ARGS
}

makeBreakpadLink() {
    local LSS_PATH="$BREAKPAD_PATH/src/third_party/lss"
    ln -s -f "$EXTERNAL/linux-syscall-support" "$LSS_PATH"
    
    local LSS_GIT_PATH="$LSS_PATH/.git"
    
    if [ -d "$LSS_GIT_PATH" ]; then # Remove git dir to prevent cache changes
        rm -rf "$LSS_GIT_PATH"
    fi
}

getCustomQt() {
    travisStartFold "Getting patched QT"

    local QT_CACHE="$CACHE/qtPatched"
    local QT_CACHE_FILE="$QT_CACHE/.cache.txt"

    if [ ! -d "$QT_CACHE" ]; then
        mkdir -p "$QT_CACHE"
    fi

    ln -sf "$QT_CACHE" "$QT_PATH"

    if [ -f "$QT_CACHE_FILE" ]; then
        info_msg "Using cached patched qt"
    else
        buildCustomQt
        sudo touch "$QT_CACHE_FILE"
    fi

    export PATH="$QT_PATH/bin:$PATH"
}

buildCustomQt() {
    info_msg "Downloading and building patched qt"

    cd "$EXTERNAL"
    echo -e "Clone Qt ${QT_VERSION}\n"
    git clone git://code.qt.io/qt/qt5.git qt${QT_VERSION}
    cd qt${QT_VERSION}
    git checkout "$(echo ${QT_VERSION} | sed -e s/\..$//)"
    perl init-repository --module-subset=qtbase,qtimageformats
    git checkout v${QT_VERSION}
    cd qtbase && git checkout v${QT_VERSION} && cd ..
    cd qtimageformats && git checkout v${QT_VERSION} && cd ..
    cd ..

    cd "$EXTERNAL/qt${QT_VERSION}/qtbase"
    git apply "$UPSTREAM/Telegram/Patches/qtbase_${QT_VERSION//\./_}.diff"
    cd ..
    ./configure -prefix "$QT_PATH" -release -opensource -confirm-license -qt-zlib \
                -qt-libpng -qt-libjpeg -qt-freetype -qt-harfbuzz -qt-pcre -qt-xcb \
                -qt-xkbcommon-x11 -no-opengl -static -nomake examples -nomake tests \
                -dbus-runtime -openssl-linked -no-gstreamer -no-mtdev # <- Not sure about these
    make $MAKE_ARGS
    sudo make install
}

buildTelegram() {
	info_msg "Build codegen_style"
	# Build codegen_style
	mkdir -p "$UPSTREAM/Linux/obj/codegen_style/Debug"
	cd "$UPSTREAM/Linux/obj/codegen_style/Debug"
	qmake QT_TDESKTOP_PATH="${QT_PATH}" QT_TDESKTOP_VERSION=${QT_VERSION} CONFIG+=debug "../../../../Telegram/build/qmake/codegen_style/codegen_style.pro"
	make $MAKE_ARGS

	info_msg "Build codegen_numbers"
	# Build codegen_numbers
	mkdir -p "$UPSTREAM/Linux/obj/codegen_numbers/Debug"
	cd "$UPSTREAM/Linux/obj/codegen_numbers/Debug"
	qmake QT_TDESKTOP_PATH="${QT_PATH}" QT_TDESKTOP_VERSION=${QT_VERSION} CONFIG+=debug "../../../../Telegram/build/qmake/codegen_numbers/codegen_numbers.pro"
	make $MAKE_ARGS

	info_msg "Build MetaLang"
	# Build MetaLang
	mkdir -p "$UPSTREAM/Linux/DebugIntermediateLang"
	cd "$UPSTREAM/Linux/DebugIntermediateLang"
	qmake QT_TDESKTOP_PATH="${QT_PATH}" QT_TDESKTOP_VERSION=${QT_VERSION} CONFIG+=debug "../../Telegram/MetaLang.pro"
	make $MAKE_ARGS

	info_msg "Build Telegram Desktop"
	# Build Telegram Desktop
	mkdir -p "$UPSTREAM/Linux/DebugIntermediate"
	cd "$UPSTREAM/Linux/DebugIntermediate"

	./../codegen/Debug/codegen_style "-I./../../Telegram/Resources" "-I./../../Telegram/SourceFiles" "-o./GeneratedFiles/styles" all_files.style --rebuild
	./../codegen/Debug/codegen_numbers "-o./GeneratedFiles" "./../../Telegram/Resources/numbers.txt"
	./../DebugLang/MetaLang -lang_in ./../../Telegram/Resources/langs/lang.strings -lang_out ./GeneratedFiles/lang_auto
	qmake QT_TDESKTOP_PATH="${QT_PATH}" QT_TDESKTOP_VERSION=${QT_VERSION} CONFIG+=debug "../../Telegram/Telegram.pro"
	make $MAKE_ARGS
}

check() {
	local filePath="$UPSTREAM/Linux/Debug/Telegram"
	if test -f "$filePath"; then
		success_msg "Build successful done! :)"

		local size;
		size=$(stat -c %s "$filePath")
		success_msg "File size of ${filePath}: ${size} Bytes"
	else
		error_msg "Build error, output file does not exist"
		exit 1
	fi
}

source ./.travis/common.sh

run