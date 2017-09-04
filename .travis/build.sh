#!/bin/bash

set -e

REPO="$PWD"

BUILD="$REPO/build"
UPSTREAM="$REPO/upstream"
EXTERNAL="$REPO/external"
CACHE="$HOME/travisCacheDir"

QT_WAS_BUILT="0"

QT_VERSION=5.6.2

XKB_PATH="$BUILD/libxkbcommon"
XKB_CACHE_VERSION="3"

QT_PATH="$BUILD/qt"
QT_CACHE_VERSION="3"
QT_PATCH="$UPSTREAM/Telegram/Patches/qtbase_${QT_VERSION//\./_}.diff"

BREAKPAD_PATH="$BUILD/breakpad"
BREAKPAD_CACHE_VERSION="3"

GYP_PATH="$BUILD/gyp"
GYP_CACHE_VERSION="3"
GYP_PATCH="$UPSTREAM/Telegram/Patches/gyp.diff"

VA_PATH="$BUILD/libva"
VA_CACHE_VERSION="3"

VDPAU_PATH="$BUILD/libvdpau"
VDPAU_CACHE_VERSION="3"

FFMPEG_PATH="$BUILD/ffmpeg"
FFMPEG_CACHE_VERSION="3"

OPENAL_PATH="$BUILD/openal-soft"
OPENAL_CACHE_VERSION="3"

GYP_DEFINES=""

[[ ! $MAKE_ARGS ]] && MAKE_ARGS="--silent -j4"

run() {
  # Move files to subdir
  cd ..
  mv tdesktop tdesktop2
  mkdir tdesktop
  mv tdesktop2 "$UPSTREAM"

  mkdir "$BUILD"

  build
  check
}

build() {
  mkdir -p "$EXTERNAL"

  BUILD_VERSION_DATA=$(echo $BUILD_VERSION | cut -d'-' -f 1)

  # libxkbcommon
  getXkbCommon

  # libva
  getVa

  # libvdpau
  getVdpau

  # ffmpeg
  getFFmpeg

  # openal_soft
  getOpenAL

  # Patched Qt
  getCustomQt

  # Breakpad
  getBreakpad

  # Patched GYP (supports cmake precompiled headers)
  getGYP

  # Guideline Support Library
  getGSL

  if [ "$QT_WAS_BUILT" == "1" ]; then
    error_msg "Qt was built, please restart the job :("
    exit 1
  fi

  # Configure the build
  if [[ $BUILD_VERSION == *"disable_autoupdate"* ]]; then
    GYP_DEFINES+=",TDESKTOP_DISABLE_AUTOUPDATE"
  fi

  if [[ $BUILD_VERSION == *"disable_register_custom_scheme"* ]]; then
    GYP_DEFINES+=",TDESKTOP_DISABLE_REGISTER_CUSTOM_SCHEME"
  fi

  if [[ $BUILD_VERSION == *"disable_crash_reports"* ]]; then
    GYP_DEFINES+=",TDESKTOP_DISABLE_CRASH_REPORTS"
  fi

  if [[ $BUILD_VERSION == *"disable_network_proxy"* ]]; then
    GYP_DEFINES+=",TDESKTOP_DISABLE_NETWORK_PROXY"
  fi

  if [[ $BUILD_VERSION == *"disable_desktop_file_generation"* ]]; then
    GYP_DEFINES+=",TDESKTOP_DISABLE_DESKTOP_FILE_GENERATION"
  fi

  if [[ $BUILD_VERSION == *"disable_unity_integration"* ]]; then
    GYP_DEFINES+=",TDESKTOP_DISABLE_UNITY_INTEGRATION"
  fi

  info_msg "Build defines: ${GYP_DEFINES}"

  buildTelegram

  travisEndFold
}

getXkbCommon() {
  travisStartFold "Getting xkbcommon"

  local XKB_CACHE="$CACHE/libxkbcommon"
  local XKB_CACHE_FILE="$XKB_CACHE/.cache.txt"
  local XKB_CACHE_KEY="${XKB_CACHE_VERSION}"
  local XKB_CACHE_OUTDATED="1"

  if [ ! -d "$XKB_CACHE" ]; then
    mkdir -p "$XKB_CACHE"
  fi

  ln -sf "$XKB_CACHE" "$XKB_PATH"

  if [ -f "$XKB_CACHE_FILE" ]; then
    local XKB_CACHE_KEY_FOUND=`tail -n 1 $XKB_CACHE_FILE`
    if [ "$XKB_CACHE_KEY" == "$XKB_CACHE_KEY_FOUND" ]; then
      XKB_CACHE_OUTDATED="0"
    else
      info_msg "Cache key '$XKB_CACHE_KEY_FOUND' does not match '$XKB_CACHE_KEY', rebuilding libxkbcommon"
    fi
  fi
  if [ "$XKB_CACHE_OUTDATED" == "1" ]; then
    buildXkbCommon
    sudo echo $XKB_CACHE_KEY > "$XKB_CACHE_FILE"
  else
    info_msg "Using cached libxkbcommon"
  fi
}

buildXkbCommon() {
  info_msg "Downloading and building libxkbcommon"

  if [ -d "$EXTERNAL/libxkbcommon" ]; then
    rm -rf "$EXTERNAL/libxkbcommon"
  fi
  cd $XKB_PATH
  rm -rf *

  cd "$EXTERNAL"
  git clone https://github.com/xkbcommon/libxkbcommon.git

  cd "$EXTERNAL/libxkbcommon"
  ./autogen.sh --prefix=$XKB_PATH
  make $MAKE_ARGS
  sudo make install
  sudo ldconfig
}

getVa() {
  travisStartFold "Getting libva"

  local VA_CACHE="$CACHE/libva"
  local VA_CACHE_FILE="$VA_CACHE/.cache.txt"
  local VA_CACHE_KEY="${VA_CACHE_VERSION}"
  local VA_CACHE_OUTDATED="1"

  if [ ! -d "$VA_CACHE" ]; then
    mkdir -p "$VA_CACHE"
  fi

  ln -sf "$VA_CACHE" "$VA_PATH"

  if [ -f "$VA_CACHE_FILE" ]; then
    local VA_CACHE_KEY_FOUND=`tail -n 1 $VA_CACHE_FILE`
    if [ "$VA_CACHE_KEY" == "$VA_CACHE_KEY_FOUND" ]; then
      VA_CACHE_OUTDATED="0"
    else
      info_msg "Cache key '$VA_CACHE_KEY_FOUND' does not match '$VA_CACHE_KEY', rebuilding libva"
    fi
  fi
  if [ "$VA_CACHE_OUTDATED" == "1" ]; then
    buildVa
    sudo echo $VA_CACHE_KEY > "$VA_CACHE_FILE"
  else
    info_msg "Using cached libva"
  fi
}

buildVa() {
  info_msg "Downloading and building libva"

  if [ -d "$EXTERNAL/libva" ]; then
    rm -rf "$EXTERNAL/libva"
  fi
  cd $VA_PATH
  rm -rf *

  cd "$EXTERNAL"
  git clone https://github.com/01org/libva

  cd "$EXTERNAL/libva"
  ./autogen.sh --prefix=$VA_PATH --enable-static
  make $MAKE_ARGS
  sudo make install
  sudo ldconfig
}

getVdpau() {
  travisStartFold "Getting libvdpau"

  local VDPAU_CACHE="$CACHE/libvdpau"
  local VDPAU_CACHE_FILE="$VDPAU_CACHE/.cache.txt"
  local VDPAU_CACHE_KEY="${VDPAU_CACHE_VERSION}"
  local VDPAU_CACHE_OUTDATED="1"

  if [ ! -d "$VDPAU_CACHE" ]; then
    mkdir -p "$VDPAU_CACHE"
  fi

  ln -sf "$VDPAU_CACHE" "$VDPAU_PATH"

  if [ -f "$VDPAU_CACHE_FILE" ]; then
    local VDPAU_CACHE_KEY_FOUND=`tail -n 1 $VDPAU_CACHE_FILE`
    if [ "$VDPAU_CACHE_KEY" == "$VDPAU_CACHE_KEY_FOUND" ]; then
      VDPAU_CACHE_OUTDATED="0"
    else
      info_msg "Cache key '$VDPAU_CACHE_KEY_FOUND' does not match '$VDPAU_CACHE_KEY', rebuilding libvdpau"
    fi
  fi
  if [ "$VDPAU_CACHE_OUTDATED" == "1" ]; then
    buildVdpau
    sudo echo $VDPAU_CACHE_KEY > "$VDPAU_CACHE_FILE"
  else
    info_msg "Using cached libvdpau"
  fi
}

buildVdpau() {
  info_msg "Downloading and building libvdpau"

  if [ -d "$EXTERNAL/libvdpau" ]; then
    rm -rf "$EXTERNAL/libvdpau"
  fi
  cd $VDPAU_PATH
  rm -rf *

  cd "$EXTERNAL"
  git clone git://anongit.freedesktop.org/vdpau/libvdpau

  cd "$EXTERNAL/libvdpau"
  ./autogen.sh --prefix=$VDPAU_PATH --enable-static
  make $MAKE_ARGS
  sudo make install
  sudo ldconfig
}

getFFmpeg() {
  travisStartFold "Getting ffmpeg"

  local FFMPEG_CACHE="$CACHE/ffmpeg"
  local FFMPEG_CACHE_FILE="$FFMPEG_CACHE/.cache.txt"
  local FFMPEG_CACHE_KEY="${FFMPEG_CACHE_VERSION}"
  local FFMPEG_CACHE_OUTDATED="1"

  if [ ! -d "$FFMPEG_CACHE" ]; then
    mkdir -p "$FFMPEG_CACHE"
  fi

  ln -sf "$FFMPEG_CACHE" "$FFMPEG_PATH"

  if [ -f "$FFMPEG_CACHE_FILE" ]; then
    local FFMPEG_CACHE_KEY_FOUND=`tail -n 1 $FFMPEG_CACHE_FILE`
    if [ "$FFMPEG_CACHE_KEY" == "$FFMPEG_CACHE_KEY_FOUND" ]; then
      FFMPEG_CACHE_OUTDATED="0"
    else
      info_msg "Cache key '$FFMPEG_CACHE_KEY_FOUND' does not match '$FFMPEG_CACHE_KEY', rebuilding ffmpeg"
    fi
  fi
  if [ "$FFMPEG_CACHE_OUTDATED" == "1" ]; then
    buildFFmpeg
    sudo echo $FFMPEG_CACHE_KEY > "$FFMPEG_CACHE_FILE"
  else
    info_msg "Using cached ffmpeg"
  fi
}

buildFFmpeg() {
  info_msg "Downloading and building ffmpeg"

  if [ -d "$EXTERNAL/ffmpeg" ]; then
    rm -rf "$EXTERNAL/ffmpeg"
  fi
  cd $FFMPEG_PATH
  rm -rf *

  cd "$EXTERNAL"
  git clone https://git.ffmpeg.org/ffmpeg.git

  cd "$EXTERNAL/ffmpeg"
  ./configure \
      --prefix=$FFMPEG_PATH \
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
}

getOpenAL() {
  travisStartFold "Getting openal-soft"

  local OPENAL_CACHE="$CACHE/openal-soft"
  local OPENAL_CACHE_FILE="$OPENAL_CACHE/.cache.txt"
  local OPENAL_CACHE_KEY="${OPENAL_CACHE_VERSION}"
  local OPENAL_CACHE_OUTDATED="1"

  if [ ! -d "$OPENAL_CACHE" ]; then
    mkdir -p "$OPENAL_CACHE"
  fi

  ln -sf "$OPENAL_CACHE" "$OPENAL_PATH"

  if [ -f "$OPENAL_CACHE_FILE" ]; then
    local OPENAL_CACHE_KEY_FOUND=`tail -n 1 $OPENAL_CACHE_FILE`
    if [ "$OPENAL_CACHE_KEY" == "$OPENAL_CACHE_KEY_FOUND" ]; then
      OPENAL_CACHE_OUTDATED="0"
    else
      info_msg "Cache key '$OPENAL_CACHE_KEY_FOUND' does not match '$OPENAL_CACHE_KEY', rebuilding openal-soft"
    fi
  fi
  if [ "$OPENAL_CACHE_OUTDATED" == "1" ]; then
    buildOpenAL
    sudo echo $OPENAL_CACHE_KEY > "$OPENAL_CACHE_FILE"
  else
    info_msg "Using cached openal-soft"
  fi
}

buildOpenAL() {
  info_msg "Downloading and building openal-soft"

  if [ -d "$EXTERNAL/openal-soft" ]; then
    rm -rf "$EXTERNAL/openal-soft"
  fi
  cd $OPENAL_PATH
  rm -rf *

  cd "$EXTERNAL"
  git clone https://github.com/kcat/openal-soft.git

  cd "$EXTERNAL/openal-soft/build"
  cmake \
      -D CMAKE_INSTALL_PREFIX=$OPENAL_PATH \
      -D CMAKE_BUILD_TYPE=Release \
      -D LIBTYPE=STATIC \
      ..
  make $MAKE_ARGS
  sudo make install
  sudo ldconfig
}

getBreakpad() {
  travisStartFold "Getting breakpad"

  local BREAKPAD_CACHE="$CACHE/breakpad"
  local BREAKPAD_CACHE_FILE="$BREAKPAD_CACHE/.cache.txt"
  local BREAKPAD_CACHE_KEY="${BREAKPAD_CACHE_VERSION}"
  local BREAKPAD_CACHE_OUTDATED="1"

  if [ ! -d "$BREAKPAD_CACHE" ]; then
    mkdir -p "$BREAKPAD_CACHE"
  fi

  ln -sf "$BREAKPAD_CACHE" "$BREAKPAD_PATH"

  if [ -f "$BREAKPAD_CACHE_FILE" ]; then
    local BREAKPAD_CACHE_KEY_FOUND=`tail -n 1 $BREAKPAD_CACHE_FILE`
    if [ "$BREAKPAD_CACHE_KEY" == "$BREAKPAD_CACHE_KEY_FOUND" ]; then
      BREAKPAD_CACHE_OUTDATED="0"
    else
      info_msg "Cache key '$BREAKPAD_CACHE_KEY_FOUND' does not match '$BREAKPAD_CACHE_KEY', rebuilding breakpad"
    fi
  fi
  if [ "$BREAKPAD_CACHE_OUTDATED" == "1" ]; then
    buildBreakpad
    sudo echo $BREAKPAD_CACHE_KEY > "$BREAKPAD_CACHE_FILE"
  else
    info_msg "Using cached breakpad"
  fi
}

buildBreakpad() {
  info_msg "Downloading and building breakpad"

  if [ -d "$EXTERNAL/breakpad" ]; then
    rm -rf "$EXTERNAL/breakpad"
  fi
  cd $BREAKPAD_PATH
  rm -rf *

  cd "$EXTERNAL"
  git clone https://chromium.googlesource.com/breakpad/breakpad

  cd "$EXTERNAL/breakpad/src/third_party"
  git clone https://chromium.googlesource.com/linux-syscall-support lss

  cd "$EXTERNAL/breakpad"
  ./configure --prefix=$BREAKPAD_PATH
  make $MAKE_ARGS
  sudo make install
  sudo ldconfig
}

getCustomQt() {
  travisStartFold "Getting patched Qt"

  local QT_CACHE="$CACHE/qtPatched"
  local QT_CACHE_FILE="$QT_CACHE/.cache.txt"
  local QT_PATCH_CHECKSUM=`sha1sum $QT_PATCH`
  local QT_CACHE_KEY="${QT_VERSION}_${QT_CACHE_VERSION}_${QT_PATCH_CHECKSUM:0:32}"
  local QT_CACHE_OUTDATED="1"

  if [ ! -d "$QT_CACHE" ]; then
    mkdir -p "$QT_CACHE"
  fi

  ln -sf "$QT_CACHE" "$QT_PATH"

  if [ -f "$QT_CACHE_FILE" ]; then
    local QT_CACHE_KEY_FOUND=`tail -n 1 $QT_CACHE_FILE`
    if [ "$QT_CACHE_KEY" == "$QT_CACHE_KEY_FOUND" ]; then
      QT_CACHE_OUTDATED="0"
    else
      info_msg "Cache key '$QT_CACHE_KEY_FOUND' does not match '$QT_CACHE_KEY', rebuilding patched Qt"
    fi
  fi
  if [ "$QT_CACHE_OUTDATED" == "1" ]; then
    buildCustomQt
    sudo echo $QT_CACHE_KEY > "$QT_CACHE_FILE"
  else
    info_msg "Using cached patched Qt"
  fi

  export PATH="$QT_PATH/bin:$PATH"
}

buildCustomQt() {
  QT_WAS_BUILT="1"
  info_msg "Downloading and building patched qt"

  if [ -d "$EXTERNAL/qt${QT_VERSION}" ]; then
    rm -rf "$EXTERNAL/qt${QT_VERSION}"
  fi
  cd $QT_PATH
  rm -rf *

  cd "$EXTERNAL"
  git clone git://code.qt.io/qt/qt5.git qt${QT_VERSION}

  cd "$EXTERNAL/qt${QT_VERSION}"
  perl init-repository --branch --module-subset=qtbase,qtimageformats
  git checkout v${QT_VERSION}
  cd qtbase && git checkout v${QT_VERSION} && cd ..
  cd qtimageformats && git checkout v${QT_VERSION} && cd ..

  cd "$EXTERNAL/qt${QT_VERSION}/qtbase"
  git apply "$QT_PATCH"
  cd ..

  ./configure -prefix $QT_PATH -release -opensource -confirm-license -qt-zlib \
              -qt-libpng -qt-libjpeg -qt-freetype -qt-harfbuzz -qt-pcre -qt-xcb \
              -qt-xkbcommon-x11 -no-opengl -no-gtkstyle -static \
              -nomake examples -nomake tests \
              -dbus-runtime -no-gstreamer -no-mtdev # <- Not sure about these
  make $MAKE_ARGS
  sudo make install
}

getGSL() {
  cd "$UPSTREAM"
  git submodule init
  git submodule update
}

getGYP() {
  travisStartFold "Getting patched GYP"

  local GYP_CACHE="$CACHE/gyp"
  local GYP_CACHE_FILE="$GYP_CACHE/.cache.txt"
  local GYP_PATCH_CHECKSUM=`sha1sum $GYP_PATCH`
  local GYP_CACHE_KEY="${GYP_CACHE_VERSION}_${GYP_PATCH_CHECKSUM:0:32}"
  local GYP_CACHE_OUTDATED="1"

  if [ ! -d "$GYP_CACHE" ]; then
    mkdir -p "$GYP_CACHE"
  fi

  ln -sf "$GYP_CACHE" "$GYP_PATH"

  if [ -f "$GYP_CACHE_FILE" ]; then
    local GYP_CACHE_KEY_FOUND=`tail -n 1 $GYP_CACHE_FILE`
    if [ "$GYP_CACHE_KEY" == "$GYP_CACHE_KEY_FOUND" ]; then
      GYP_CACHE_OUTDATED="0"
    else
      info_msg "Cache key '$GYP_CACHE_KEY_FOUND' does not match '$GYP_CACHE_KEY', rebuilding patched GYP"
    fi
  fi
  if [ "$GYP_CACHE_OUTDATED" == "1" ]; then
    buildGYP
    sudo echo $GYP_CACHE_KEY > "$GYP_CACHE_FILE"
  else
    info_msg "Using cached patched GYP"
  fi
}

buildGYP() {
  info_msg "Downloading and building patched GYP"

  if [ -d "$EXTERNAL/gyp" ]; then
    rm -rf "$EXTERNAL/gyp"
  fi
  cd $GYP_PATH
  rm -rf *

  cd "$EXTERNAL"
  git clone https://chromium.googlesource.com/external/gyp

  cd "$EXTERNAL/gyp"
  git checkout 702ac58e47
  git apply "$GYP_PATCH"
  cp -r * "$GYP_PATH/"
}

buildTelegram() {
  travisStartFold "Build tdesktop"

  cd "$UPSTREAM/Telegram/gyp"
  "$GYP_PATH/gyp" \
      -Dbuild_defines=${GYP_DEFINES:1} \
      -Dlinux_path_xkbcommon=$XKB_PATH \
      -Dlinux_path_va=$VA_PATH \
      -Dlinux_path_vdpau=$VDPAU_PATH \
      -Dlinux_path_ffmpeg=$FFMPEG_PATH \
      -Dlinux_path_openal=$OPENAL_PATH \
      -Dlinux_path_qt=$QT_PATH \
      -Dlinux_path_breakpad=$BREAKPAD_PATH \
      -Dlinux_path_libexif_lib=/usr/local/lib \
      -Dlinux_path_opus_include=/usr/include/opus \
      -Dlinux_lib_ssl=-lssl \
      -Dlinux_lib_crypto=-lcrypto \
      -Dlinux_lib_icu=-licuuc\ -licutu\ -licui18n \
      --depth=. --generator-output=.. --format=cmake -Goutput_dir=../out \
      Telegram.gyp
  cd "$UPSTREAM/out/Debug"

  export ASM="gcc"
  cmake .
  make $MAKE_ARGS
}

check() {
  local filePath="$UPSTREAM/out/Debug/Telegram"
  if test -f "$filePath"; then
    success_msg "Build successfully done! :)"

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
