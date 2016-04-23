#!/bin/bash
# Installs libs and compiles tdesktop

run() {
	info_msg "Build version: ${BUILD_VERSION}"

	downloadLibs
	prepare
	build
	check
}

downloadLibs() {
	travis_fold_start "download_libs"
	# Move telegram project to subfolder
	mkdir tdesktop
	mv -f Telegram tdesktop

	# Download libraries
	info_msg "QT-Version: ${_qtver}, SRC-Dir: ${srcdir}"

	echo -e "Clone Qt\n"
	git clone git://code.qt.io/qt/qt5.git qt5_6_0
	cd qt5_6_0
	git checkout 5.6
	perl init-repository --module-subset=qtbase,qtimageformats
	git checkout v5.6.0
	cd qtbase && git checkout v5.6.0 && cd ..
	cd qtimageformats && git checkout v5.6.0 && cd ..

	echo -e "Clone Breakpad\n"
	git clone https://chromium.googlesource.com/breakpad/breakpad breakpad

	echo -e "Clone Linux Syscall Support\n"
	git clone https://chromium.googlesource.com/linux-syscall-support breakpad-lss

	echo -e "Lets view the folder content\n"
	ls
	travis_fold_end "download_libs"
}

prepare() {
	travis_fold_start "prepare"
	start_msg "Preparing the libraries..."

	cd "$srcdir/tdesktop"

	mkdir -p "$srcdir/Libraries"

	ln -s "$srcdir/qt5_6_0" "$srcdir/Libraries/qt5_6_0"
	cd "$srcdir/Libraries/qt5_6_0/qtbase"
	git apply "$srcdir/tdesktop/Telegram/Patches/qtbase_5_6_0.diff"

	if [ ! -h "$srcdir/Libraries/breakpad" ]; then
		ln -s "$srcdir/breakpad" "$srcdir/Libraries/breakpad"
		ln -s "$srcdir/breakpad-lss" "$srcdir/Libraries/breakpad/src/third_party/lss"
	fi

	sed -i 's/CUSTOM_API_ID//g' "$srcdir/tdesktop/Telegram/Telegram.pro"
	sed -i 's,LIBS += /usr/local/lib/libxkbcommon.a,,g' "$srcdir/tdesktop/Telegram/Telegram.pro"
	sed -i 's,LIBS += /usr/local/lib/libz.a,LIBS += -lz,g' "$srcdir/tdesktop/Telegram/Telegram.pro"
	sed -i "s,/usr/local/tdesktop/Qt-5.6.0,$srcdir/qt,g" "$srcdir/tdesktop/Telegram/Telegram.pro"

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

	options+='\nINCLUDEPATH += "/usr/lib/glib-2.0/include"'
	options+='\nINCLUDEPATH += "/usr/lib/gtk-2.0/include"'
	options+='\nINCLUDEPATH += "/usr/include/opus"'
	options+='\nLIBS += -lcrypto -lssl'

	info_msg "Build options: ${options}"

	echo -e "${options}" >> "$srcdir/tdesktop/Telegram/Telegram.pro"

	success_msg "Prepare done! :)"
	travis_fold_end "prepare"
}

build() {
	start_msg "Building the projects..."

	info_msg "Build patched Qt"
	# Build patched Qt
	cd "$srcdir/Libraries/qt5_6_0"
	./configure -prefix "$srcdir/qt" -release -opensource -confirm-license -qt-zlib \
	            -qt-libpng -qt-libjpeg -qt-freetype -qt-harfbuzz -qt-pcre -qt-xcb \
	            -qt-xkbcommon-x11 -no-opengl -static -nomake examples -nomake tests
	make --silent
	make --silent install

	export PATH="$srcdir/qt/bin:$PATH"

	info_msg "Build breakpad"
	# Build breakpad
	cd "$srcdir/Libraries/breakpad"
	./configure
	make --silent

  info_msg "Build codegen_style"
  # Build codegen_style
  mkdir -p "$srcdir/tdesktop/Linux/obj/codegen_style/Debug"
  cd "$srcdir/tdesktop/Linux/obj/codegen_style/Debug"
  qmake CONFIG+=debug "../../../../Telegram/build/qmake/codegen_style/codegen_style.pro"
  make --silent

  info_msg "Build codegen_numbers"
  # Build codegen_numbers
  mkdir -p "$srcdir/tdesktop/Linux/obj/codegen_numbers/Debug"
  cd "$srcdir/tdesktop/Linux/obj/codegen_numbers/Debug"
  qmake CONFIG+=debug "../../../../Telegram/build/qmake/codegen_numbers/codegen_numbers.pro"
  make --silent

	info_msg "Build MetaLang"
	# Build MetaLang
	mkdir -p "$srcdir/tdesktop/Linux/DebugIntermediateLang"
	cd "$srcdir/tdesktop/Linux/DebugIntermediateLang"
	qmake CONFIG+=debug "../../Telegram/MetaLang.pro"
	make --silent

	info_msg "Build Telegram Desktop"
	# Build Telegram Desktop
	mkdir -p "$srcdir/tdesktop/Linux/DebugIntermediate"
	cd "$srcdir/tdesktop/Linux/DebugIntermediate"

	qmake CONFIG+=debug "../../Telegram/Telegram.pro"
	make
}

check() {
	local filePath="$srcdir/tdesktop/Linux/Release/Telegram"
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
