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

	echo -e "\nDownload and extract qt"
	qt_file=qt-everywhere-opensource-src-$_qtver.tar.xz
	echo -e "QT-File: ${qt_file}"

	wget "http://download.qt.io/official_releases/qt/${_qtver%.*}/$_qtver/single/$qt_file"
	tar xf $qt_file
	rm $qt_file

	echo -e "Clone Breakpad"
	git clone https://chromium.googlesource.com/breakpad/breakpad breakpad

	echo -e "\nClone Linux Syscall Support"
	git clone https://chromium.googlesource.com/linux-syscall-support breakpad-lss

	echo -e "\nLets view the folder content"
	ls
	travis_fold_end "download_libs"
}

prepare() {
	travis_fold_start "prepare"
	start_msg "Preparing the libraries..."

	cd "$srcdir/tdesktop"

	mkdir -p "$srcdir/Libraries"

	local qt_patch_file="$srcdir/tdesktop/Telegram/_qtbase_${_qtver//./_}_patch.diff"
	if [ "$qt_patch_file" -nt "$srcdir/Libraries/QtStatic" ]; then
		rm -rf "$srcdir/Libraries/QtStatic"
		mv "$srcdir/qt-everywhere-opensource-src-$_qtver" "$srcdir/Libraries/QtStatic"
		cd "$srcdir/Libraries/QtStatic/qtbase"
		patch -p1 -i "$qt_patch_file"
	fi

	if [ ! -h "$srcdir/Libraries/breakpad" ]; then
		ln -s "$srcdir/breakpad" "$srcdir/Libraries/breakpad"
		ln -s "$srcdir/breakpad-lss" "$srcdir/Libraries/breakpad/src/third_party/lss"
	fi

	sed -i 's/CUSTOM_API_ID//g' "$srcdir/tdesktop/Telegram/Telegram.pro"
	sed -i 's,LIBS += /usr/local/lib/libxkbcommon.a,,g' "$srcdir/tdesktop/Telegram/Telegram.pro"
	sed -i 's,LIBS += /usr/local/lib/libz.a,LIBS += -lz,g' "$srcdir/tdesktop/Telegram/Telegram.pro"

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
	cd "$srcdir/Libraries/QtStatic"
	./configure -prefix "$srcdir/qt" -release -opensource -confirm-license -qt-zlib \
	            -qt-libpng -qt-libjpeg -qt-freetype -qt-harfbuzz -qt-pcre -qt-xcb \
	            -qt-xkbcommon-x11 -no-opengl -static -nomake examples -nomake tests
	make --silent module-qtbase module-qtimageformats
	make --silent module-qtbase-install_subtargets module-qtimageformats-install_subtargets

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
	mkdir -p "$srcdir/tdesktop/Linux/ReleaseIntermediate"
	cd "$srcdir/tdesktop/Linux/ReleaseIntermediate"

	qmake CONFIG+=release "../../Telegram/Telegram.pro"
	local pattern="^PRE_TARGETDEPS +="
	grep "$pattern" "$srcdir/tdesktop/Telegram/Telegram.pro" | sed "s/$pattern//g" | xargs make

	qmake CONFIG+=release "../../Telegram/Telegram.pro"
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
