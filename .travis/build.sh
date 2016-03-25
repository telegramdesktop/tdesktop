#!/bin/bash
# Installs libs and compiles tdesktop

run() {
	info_msg "Build version: ${BUILD_VERSION}"

	downloadLibs
	prepare
	build
	check
}

# set colors
RCol='\e[0m'    # Text Reset

# Regular           Bold                Underline           High Intensity      BoldHigh Intens     Background          High Intensity Backgrounds
Bla='\e[0;30m';     BBla='\e[1;30m';    UBla='\e[4;30m';    IBla='\e[0;90m';    BIBla='\e[1;90m';   On_Bla='\e[40m';    On_IBla='\e[0;100m';
Red='\e[0;31m';     BRed='\e[1;31m';    URed='\e[4;31m';    IRed='\e[0;91m';    BIRed='\e[1;91m';   On_Red='\e[41m';    On_IRed='\e[0;101m';
Gre='\e[0;32m';     BGre='\e[1;32m';    UGre='\e[4;32m';    IGre='\e[0;92m';    BIGre='\e[1;92m';   On_Gre='\e[42m';    On_IGre='\e[0;102m';
Yel='\e[0;33m';     BYel='\e[1;33m';    UYel='\e[4;33m';    IYel='\e[0;93m';    BIYel='\e[1;93m';   On_Yel='\e[43m';    On_IYel='\e[0;103m';
Blu='\e[0;34m';     BBlu='\e[1;34m';    UBlu='\e[4;34m';    IBlu='\e[0;94m';    BIBlu='\e[1;94m';   On_Blu='\e[44m';    On_IBlu='\e[0;104m';
Pur='\e[0;35m';     BPur='\e[1;35m';    UPur='\e[4;35m';    IPur='\e[0;95m';    BIPur='\e[1;95m';   On_Pur='\e[45m';    On_IPur='\e[0;105m';
Cya='\e[0;36m';     BCya='\e[1;36m';    UCya='\e[4;36m';    ICya='\e[0;96m';    BICya='\e[1;96m';   On_Cya='\e[46m';    On_ICya='\e[0;106m';
Whi='\e[0;37m';     BWhi='\e[1;37m';    UWhi='\e[4;37m';    IWhi='\e[0;97m';    BIWhi='\e[1;97m';   On_Whi='\e[47m';    On_IWhi='\e[0;107m';

# Set variables
_qtver=5.5.1
srcdir=${PWD}

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

	info_msg "Build MetaStyle"
	# Build MetaStyle
	mkdir -p "$srcdir/tdesktop/Linux/DebugIntermediateStyle"
	cd "$srcdir/tdesktop/Linux/DebugIntermediateStyle"
	qmake CONFIG+=debug "../../Telegram/MetaStyle.pro"
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

start_msg() {
	echo -e "\n${Gre}$*${RCol}"
}

info_msg() {
	echo -e "\n${Cya}$*${RCol}"
}

error_msg() {
	echo -e "\n${BRed}$*${RCol}"
}

success_msg() {
	echo -e "\n${BGre}$*${RCol}"
}

travis_fold_start() {
	echo "travis_fold:start:$*"
}

travis_fold_end() {
	echo "travis_fold:end:$*"
}

run
