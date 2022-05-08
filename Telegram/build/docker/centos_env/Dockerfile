ARG DEBUG 1

FROM centos:7 AS builder
ENV GIT https://github.com
ENV GIT_FREEDESKTOP ${GIT}/gitlab-freedesktop-mirrors
ENV PKG_CONFIG_PATH /usr/local/lib64/pkgconfig:/usr/local/lib/pkgconfig:/usr/local/share/pkgconfig
ENV QT 6_3_0
ENV QT_TAG v6.3.0
ENV QT_PREFIX /usr/local/desktop-app/Qt-6.3.0
ENV OPENSSL_VER 1_1_1
ENV OPENSSL_PREFIX /usr/local/desktop-app/openssl-1.1.1
ENV OPENSSL_ROOT_DIR ${OPENSSL_PREFIX}
ENV CMAKE_VER 3.21.3
ENV CMAKE_FILE cmake-$CMAKE_VER-Linux-x86_64.sh
ENV CMAKE_PREFIX_PATH ${QT_PREFIX}
ENV PATH ${QT_PREFIX}/bin:${PATH}
ENV HFLAGS_DEBUG "-fstack-protector-all -fstack-clash-protection -fPIC"
ENV HFLAGS "$HFLAGS_DEBUG -D_FORTIFY_SOURCE=2"

RUN yum -y install https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm \
	&& yum -y install https://packages.endpointdev.com/rhel/7/os/x86_64/endpoint-repo.x86_64.rpm \
	&& yum -y install centos-release-scl \
	&& yum -y install git meson ninja-build autoconf automake libtool patch \
		fontconfig-devel freetype-devel libX11-devel at-spi2-core-devel alsa-lib-devel \
		pulseaudio-libs-devel mesa-libGL-devel mesa-libEGL-devel mesa-libgbm-devel \
		libdrm-devel gtk3-devel \
		perl-XML-Parser pkgconfig bison yasm file which xorg-x11-util-macros \
		devtoolset-10-make devtoolset-10-gcc devtoolset-10-gcc-c++ \
		devtoolset-10-binutils llvm-toolset-7.0 llvm-toolset-7.0-clang-devel \
		llvm-toolset-7.0-llvm-devel \
	&& yum clean all

# Fix a bug with argument naming in CentOS 7 glibc
RUN sed -i 's/char \*__block/char */' /usr/include/unistd.h

SHELL [ "bash", "-c", ". /opt/rh/devtoolset-10/enable; exec bash -c \"$@\"", "-s"]

ENV LibrariesPath /usr/src/Libraries
WORKDIR $LibrariesPath

RUN mkdir /opt/cmake \
	&& curl -sSLo $CMAKE_FILE $GIT/Kitware/CMake/releases/download/v$CMAKE_VER/$CMAKE_FILE \
	&& sh $CMAKE_FILE --prefix=/opt/cmake --skip-license \
	&& ln -s /opt/cmake/bin/cmake /usr/local/bin/cmake \
	&& rm $CMAKE_FILE

FROM builder AS patches
RUN git clone $GIT/desktop-app/patches.git \
	&& cd patches \
	&& git checkout 4a676dea5a \
	&& rm -rf .git

FROM builder AS libffi
RUN git clone -b v3.4.2 --depth=1 $GIT/libffi/libffi.git \
	&& cd libffi \
	&& ./autogen.sh \
	&& ./configure --enable-static --disable-docs \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/libffi-cache" install \
	&& cd .. \
	&& rm -rf libffi

FROM builder AS zlib
RUN git clone -b v1.2.11 --depth=1 $GIT/madler/zlib.git \
	&& cd zlib \
	&& CFLAGS="-O3 $HFLAGS" ./configure --static \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/zlib-cache" install \
	&& cd .. \
	&& rm -rf zlib

FROM builder AS xz
RUN git clone -b v5.2.5 https://git.tukaani.org/xz.git \
	&& cd xz \
	&& CFLAGS="$HFLAGS" cmake -GNinja -B build . -DCMAKE_BUILD_TYPE=Release \
	&& cmake --build build --parallel \
	&& DESTDIR="$LibrariesPath/xz-cache" cmake --install build \
	&& cd .. \
	&& rm -rf xz

FROM patches AS libproxy
RUN git clone -b 0.4.17 --depth=1 $GIT/libproxy/libproxy.git \
	&& cd libproxy \
	&& git apply ../patches/libproxy.patch \
	&& CFLAGS="$HFLAGS" CXXFLAGS="$HFLAGS" cmake -GNinja -B build . \
		-DCMAKE_BUILD_TYPE=Release \
		-DWITH_DBUS=OFF \
		-DWITH_NM=OFF \
		-DWITH_NMold=OFF \
	&& cmake --build build --parallel \
	&& DESTDIR="$LibrariesPath/libproxy-cache" cmake --install build \
	&& cd .. \
	&& rm -rf libproxy

FROM builder AS mozjpeg
RUN git clone -b v4.0.3 --depth=1 $GIT/mozilla/mozjpeg.git \
	&& cd mozjpeg \
	&& CFLAGS="$HFLAGS" cmake -GNinja -B build . \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX=/usr/local \
		-DWITH_JPEG8=ON \
		-DPNG_SUPPORTED=OFF \
	&& cmake --build build --parallel \
	&& DESTDIR="$LibrariesPath/mozjpeg-cache" cmake --install build \
	&& cd .. \
	&& rm -rf mozjpeg

FROM builder AS opus
RUN git clone -b v1.3.1 --depth=1 $GIT/xiph/opus.git \
	&& cd opus \
	&& ./autogen.sh \
	&& CFLAGS="-g -O2 $HFLAGS" ./configure \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/opus-cache" install \
	&& cd .. \
	&& rm -rf opus

FROM builder AS libvpx
RUN git clone -b v1.11.0 --depth=1 $GIT/webmproject/libvpx.git \
	&& cd libvpx \
	&& ./configure \
		--disable-examples \
		--disable-unit-tests \
		--disable-tools \
		--disable-docs \
		--enable-vp8 \
		--enable-vp9 \
		--enable-webm-io \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/libvpx-cache" install \
	&& cd .. \
	&& rm -rf libvpx

FROM builder AS rnnoise
RUN git clone -b master --depth=1 $GIT/desktop-app/rnnoise \
	&& cd rnnoise \
	&& CFLAGS="$HFLAGS" cmake -GNinja -B build . -DCMAKE_BUILD_TYPE=Release \
	&& cmake --build build --parallel \
	&& mkdir -p "$LibrariesPath/rnnoise-cache/usr/local/include" \
	&& cp "include/rnnoise.h" "$LibrariesPath/rnnoise-cache/usr/local/include/" \
	&& mkdir -p "$LibrariesPath/rnnoise-cache/usr/local/lib" \
	&& cp "build/librnnoise.a" "$LibrariesPath/rnnoise-cache/usr/local/lib/" \
	&& cd .. \
	&& rm -rf rnnoise

FROM builder AS xcb-proto
RUN git clone -b xcb-proto-1.14.1 --depth=1 $GIT_FREEDESKTOP/xcbproto.git \
	&& cd xcbproto \
	&& ./autogen.sh \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/xcb-proto-cache" install \
	&& cd .. \
	&& rm -rf xcbproto

FROM builder AS xcb
COPY --from=xcb-proto ${LibrariesPath}/xcb-proto-cache /

RUN git clone -b libxcb-1.14 --depth=1 $GIT_FREEDESKTOP/libxcb.git \
	&& cd libxcb \
	&& CFLAGS="-g -O2 $HFLAGS" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/xcb-cache" install \
	&& cd .. \
	&& rm -rf libxcb

FROM builder AS xcb-wm
RUN git clone -b 0.4.1 --depth=1 --recursive $GIT_FREEDESKTOP/libxcb-wm.git \
	&& cd libxcb-wm \
	&& ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/xcb-wm-cache" install \
	&& cd .. \
	&& rm -rf libxcb-wm

FROM builder AS xcb-util
RUN git clone -b 0.4.0 --depth=1 --recursive $GIT_FREEDESKTOP/libxcb-util.git \
	&& cd libxcb-util \
	&& ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/xcb-util-cache" install \
	&& cd .. \
	&& rm -rf libxcb-util

FROM builder AS xcb-image
COPY --from=xcb-util ${LibrariesPath}/xcb-util-cache /

RUN git clone -b 0.4.0 --depth=1 --recursive $GIT_FREEDESKTOP/libxcb-image.git \
	&& cd libxcb-image \
	&& CFLAGS="-g -O2 $HFLAGS" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/xcb-image-cache" install \
	&& cd .. \
	&& rm -rf libxcb-image

FROM builder AS xcb-keysyms
RUN git clone -b 0.4.0 --depth=1 --recursive $GIT_FREEDESKTOP/libxcb-keysyms.git \
	&& cd libxcb-keysyms \
	&& CFLAGS="-g -O2 $HFLAGS" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/xcb-keysyms-cache" install \
	&& cd .. \
	&& rm -rf libxcb-keysyms

FROM builder AS xcb-render-util
RUN git clone -b 0.3.9 --depth=1 --recursive $GIT_FREEDESKTOP/libxcb-render-util.git \
	&& cd libxcb-render-util \
	&& CFLAGS="-g -O2 $HFLAGS" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/xcb-render-util-cache" install \
	&& cd .. \
	&& rm -rf libxcb-render-util

FROM builder AS libXext
RUN git clone -b libXext-1.3.4 --depth=1 $GIT_FREEDESKTOP/libxext.git \
	&& cd libxext \
	&& CFLAGS="-g -O2 $HFLAGS" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/libXext-cache" install \
	&& cd .. \
	&& rm -rf libxext

FROM builder AS libXtst
RUN git clone -b libXtst-1.2.3 --depth=1 $GIT_FREEDESKTOP/libxtst.git \
	&& cd libxtst \
	&& CFLAGS="-g -O2 $HFLAGS" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/libXtst-cache" install \
	&& cd .. \
	&& rm -rf libxtst

FROM builder AS libXfixes
RUN git clone -b libXfixes-5.0.3 --depth=1 $GIT_FREEDESKTOP/libxfixes.git \
	&& cd libxfixes \
	&& CFLAGS="-g -O2 $HFLAGS" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/libXfixes-cache" install \
	&& cd .. \
	&& rm -rf libxfixes

FROM builder AS libXrandr
RUN git clone -b libXrandr-1.5.2 --depth=1 $GIT_FREEDESKTOP/libxrandr.git \
	&& cd libxrandr \
	&& CFLAGS="-g -O2 $HFLAGS" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/libXrandr-cache" install \
	&& cd .. \
	&& rm -rf libxrandr

FROM builder AS libXrender
RUN git clone -b libXrender-0.9.10 --depth=1 $GIT_FREEDESKTOP/libxrender.git \
	&& cd libxrender \
	&& CFLAGS="-g -O2 $HFLAGS" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/libXrender-cache" install \
	&& cd .. \
	&& rm -rf libxrender

FROM builder AS libXdamage
RUN git clone -b libXdamage-1.1.5 --depth=1 $GIT_FREEDESKTOP/libxdamage.git \
	&& cd libxdamage \
	&& CFLAGS="-g -O2 $HFLAGS" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/libXdamage-cache" install \
	&& cd .. \
	&& rm -rf libxdamage

FROM builder AS libXcomposite
RUN git clone -b libXcomposite-0.4.5 --depth=1 $GIT_FREEDESKTOP/libxcomposite.git \
	&& cd libxcomposite \
	&& CFLAGS="-g -O2 $HFLAGS" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/libXcomposite-cache" install \
	&& cd .. \
	&& rm -rf libxcomposite

FROM builder AS wayland
COPY --from=libffi ${LibrariesPath}/libffi-cache /

RUN git clone -b 1.20.0 --depth=1 $GIT_FREEDESKTOP/wayland.git \
	&& cd wayland \
	&& meson build \
		--buildtype=release \
		--default-library=both \
		-Dtests=false \
		-Ddocumentation=false \
		-Ddtd_validation=false \
		-Dicon_directory=/usr/share/icons \
	&& meson compile -C build \
	&& DESTDIR="$LibrariesPath/wayland-cache" meson install -C build \
	&& cd .. \
	&& rm -rf wayland

FROM builder AS nv-codec-headers
RUN git clone -b n11.1.5.1 --depth=1 https://github.com/FFmpeg/nv-codec-headers.git \
	&& DESTDIR="$LibrariesPath/nv-codec-headers-cache" make -C nv-codec-headers install \
	&& rm -rf nv-codec-headers

FROM builder AS ffmpeg
COPY --from=opus ${LibrariesPath}/opus-cache /
COPY --from=libvpx ${LibrariesPath}/libvpx-cache /
COPY --from=nv-codec-headers ${LibrariesPath}/nv-codec-headers-cache /

RUN git init ffmpeg \
	&& cd ffmpeg \
	&& git remote add origin $GIT/FFmpeg/FFmpeg.git \
	&& git fetch --depth=1 origin cc33e73618a981de7fd96385ecb34719de031f16 \
	&& git reset --hard FETCH_HEAD \
	&& ./configure \
		--extra-cflags="-DCONFIG_SAFE_BITSTREAM_READER=1 $HFLAGS" \
		--extra-cxxflags="-DCONFIG_SAFE_BITSTREAM_READER=1 $HFLAGS" \
		--disable-debug \
		--disable-programs \
		--disable-doc \
		--disable-network \
		--disable-autodetect \
		--disable-everything \
		--enable-libopus \
		--enable-libvpx \
		--enable-ffnvcodec \
		--enable-nvdec \
		--enable-cuvid \
		--enable-protocol=file \
		--enable-hwaccel=av1_nvdec \
		--enable-hwaccel=h264_nvdec \
		--enable-hwaccel=hevc_nvdec \
		--enable-hwaccel=mpeg2_nvdec \
		--enable-hwaccel=mpeg4_nvdec \
		--enable-hwaccel=vp8_nvdec \
		--enable-decoder=aac \
		--enable-decoder=aac_fixed \
		--enable-decoder=aac_latm \
		--enable-decoder=aasc \
		--enable-decoder=alac \
		--enable-decoder=av1 \
		--enable-decoder=av1_cuvid \
		--enable-decoder=flac \
		--enable-decoder=gif \
		--enable-decoder=h264 \
		--enable-decoder=hevc \
		--enable-decoder=libvpx_vp8 \
		--enable-decoder=libvpx_vp9 \
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
		--enable-decoder=msmpeg4v2 \
		--enable-decoder=msmpeg4v3 \
		--enable-decoder=opus \
		--enable-decoder=pcm_alaw \
		--enable-decoder=pcm_f32be \
		--enable-decoder=pcm_f32le \
		--enable-decoder=pcm_f64be \
		--enable-decoder=pcm_f64le \
		--enable-decoder=pcm_lxf \
		--enable-decoder=pcm_mulaw \
		--enable-decoder=pcm_s16be \
		--enable-decoder=pcm_s16be_planar \
		--enable-decoder=pcm_s16le \
		--enable-decoder=pcm_s16le_planar \
		--enable-decoder=pcm_s24be \
		--enable-decoder=pcm_s24daud \
		--enable-decoder=pcm_s24le \
		--enable-decoder=pcm_s24le_planar \
		--enable-decoder=pcm_s32be \
		--enable-decoder=pcm_s32le \
		--enable-decoder=pcm_s32le_planar \
		--enable-decoder=pcm_s64be \
		--enable-decoder=pcm_s64le \
		--enable-decoder=pcm_s8 \
		--enable-decoder=pcm_s8_planar \
		--enable-decoder=pcm_u16be \
		--enable-decoder=pcm_u16le \
		--enable-decoder=pcm_u24be \
		--enable-decoder=pcm_u24le \
		--enable-decoder=pcm_u32be \
		--enable-decoder=pcm_u32le \
		--enable-decoder=pcm_u8 \
		--enable-decoder=pcm_zork \
		--enable-decoder=vorbis \
		--enable-decoder=vp8 \
		--enable-decoder=wavpack \
		--enable-decoder=wmalossless \
		--enable-decoder=wmapro \
		--enable-decoder=wmav1 \
		--enable-decoder=wmav2 \
		--enable-decoder=wmavoice \
		--enable-encoder=libopus \
		--enable-parser=aac \
		--enable-parser=aac_latm \
		--enable-parser=flac \
		--enable-parser=h264 \
		--enable-parser=hevc \
		--enable-parser=mpeg4video \
		--enable-parser=mpegaudio \
		--enable-parser=opus \
		--enable-parser=vorbis \
		--enable-demuxer=aac \
		--enable-demuxer=flac \
		--enable-demuxer=gif \
		--enable-demuxer=h264 \
		--enable-demuxer=hevc \
		--enable-demuxer=matroska \
		--enable-demuxer=m4v \
		--enable-demuxer=mov \
		--enable-demuxer=mp3 \
		--enable-demuxer=ogg \
		--enable-demuxer=wav \
		--enable-muxer=ogg \
		--enable-muxer=opus \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/ffmpeg-cache" install \
	&& cd .. \
	&& rm -rf ffmpeg

FROM builder AS openal
ADD https://api.github.com/repos/telegramdesktop/openal-soft/git/refs/heads/fix_pulse_default openal-soft-version.json
RUN git clone -b fix_pulse_default --depth=1 $GIT/telegramdesktop/openal-soft.git \
	&& cd openal-soft \
	&& CFLAGS="$HFLAGS" CXXFLAGS="$HFLAGS" cmake -GNinja -B build . \
		-DCMAKE_BUILD_TYPE=Release \
		-DLIBTYPE:STRING=STATIC \
		-DALSOFT_EXAMPLES=OFF \
		-DALSOFT_TESTS=OFF \
		-DALSOFT_UTILS=OFF \
		-DALSOFT_CONFIG=OFF \
	&& cmake --build build --parallel \
	&& DESTDIR="$LibrariesPath/openal-cache" cmake --install build \
	&& cd .. \
	&& rm -rf openal-soft

FROM builder AS openssl
ENV opensslDir openssl_${OPENSSL_VER}
RUN git clone -b OpenSSL_${OPENSSL_VER}-stable --depth=1 $GIT/openssl/openssl.git $opensslDir \
	&& cd $opensslDir \
	&& ./config \
		--prefix="$OPENSSL_PREFIX" \
		--openssldir=/etc/ssl \
		no-tests \
		no-dso \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/openssl-cache" install_sw \
	&& cd .. \
	&& rm -rf $opensslDir

FROM builder AS xkbcommon
COPY --from=xcb ${LibrariesPath}/xcb-cache /

RUN git clone -b xkbcommon-1.3.1 --depth=1 $GIT/xkbcommon/libxkbcommon.git \
	&& cd libxkbcommon \
	&& meson build \
		--buildtype=release \
		--default-library=both \
		-Denable-docs=false \
		-Denable-wayland=false \
		-Denable-xkbregistry=false \
		-Dxkb-config-root=/usr/share/X11/xkb \
		-Dxkb-config-extra-path=/etc/xkb \
		-Dx-locale-root=/usr/share/X11/locale \
	&& meson compile -C build \
	&& DESTDIR="$LibrariesPath/xkbcommon-cache" meson install -C build \
	&& cd .. \
	&& rm -rf libxkbcommon

FROM builder AS mm-common
RUN git clone -b 1.0.3 --depth=1 $GIT/GNOME/mm-common.git \
	&& cd mm-common \
	&& NOCONFIGURE=1 ./autogen.sh \
	&& ./configure --enable-network \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/mm-common-cache" install \
	&& cd .. \
	&& rm -rf mm-common

FROM builder AS libsigcplusplus
COPY --from=mm-common ${LibrariesPath}/mm-common-cache /

RUN git clone -b 2.10.7 --depth=1 $GIT/libsigcplusplus/libsigcplusplus.git \
	&& cd libsigcplusplus \
	&& export ACLOCAL_PATH="/usr/local/share/aclocal" \
	&& NOCONFIGURE=1 ./autogen.sh \
	&& CFLAGS="-g -O2 $HFLAGS" CXXFLAGS="-g -O2 $HFLAGS" ./configure \
		--enable-maintainer-mode \
		--enable-static \
		--disable-documentation \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/libsigcplusplus-cache" install \
	&& cd .. \
	&& rm -rf libsigcplusplus

FROM patches AS glibmm
COPY --from=mm-common ${LibrariesPath}/mm-common-cache /
COPY --from=libsigcplusplus ${LibrariesPath}/libsigcplusplus-cache /

# equals to glib version of Ubuntu 14.04
RUN git clone -b 2.40.0 --depth=1 $GIT/GNOME/glibmm.git \
	&& cd glibmm \
	&& git apply ../patches/glibmm.patch \
	&& export ACLOCAL_PATH="/usr/local/share/aclocal" \
	&& NOCONFIGURE=1 ./autogen.sh \
	&& CC="gcc -flto $HFLAGS" CXX="g++ -flto $HFLAGS" AR=gcc-ar RANLIB=gcc-ranlib ./configure \
		--enable-maintainer-mode \
		--enable-static \
		--disable-documentation \
	&& make -j$(nproc) \
	&& make DESTDIR="$LibrariesPath/glibmm-cache" install \
	&& cd .. \
	&& rm -rf glibmm

FROM patches AS qt
COPY --from=libffi ${LibrariesPath}/libffi-cache /
COPY --from=zlib ${LibrariesPath}/zlib-cache /
COPY --from=libproxy ${LibrariesPath}/libproxy-cache /
COPY --from=mozjpeg ${LibrariesPath}/mozjpeg-cache /
COPY --from=xcb ${LibrariesPath}/xcb-cache /
COPY --from=xcb-wm ${LibrariesPath}/xcb-wm-cache /
COPY --from=xcb-util ${LibrariesPath}/xcb-util-cache /
COPY --from=xcb-image ${LibrariesPath}/xcb-image-cache /
COPY --from=xcb-keysyms ${LibrariesPath}/xcb-keysyms-cache /
COPY --from=xcb-render-util ${LibrariesPath}/xcb-render-util-cache /
COPY --from=wayland ${LibrariesPath}/wayland-cache /
COPY --from=openssl ${LibrariesPath}/openssl-cache /
COPY --from=xkbcommon ${LibrariesPath}/xkbcommon-cache /

ARG DEBUG
RUN git clone -b ${QT_TAG} --depth=1 git://code.qt.io/qt/qt5.git qt_${QT} \
	&& cd qt_${QT} \
	&& git submodule update --init --recursive --depth=1 qtbase qtwayland qtimageformats qtsvg qt5compat \
	&& cd qtbase \
	&& find ../../patches/qtbase_${QT} -type f -print0 | sort -z | xargs -r0 git apply \
	&& cd .. \
	&& ./configure -prefix "$QT_PREFIX" \
		-release \
		${DEBUG:+-force-debug-info} \
		-opensource \
		-confirm-license \
		-libproxy \
		-qt-libpng \
		-qt-harfbuzz \
		-qt-pcre \
		-no-icu \
		-no-feature-xcb-sm \
		-no-feature-egl-extension-platform-wayland \
		-no-feature-highdpiscaling \
		-static \
		-dbus-runtime \
		-openssl-linked \
		-nomake examples \
		-nomake tests \
	&& cmake --build . --parallel \
	&& DESTDIR="$LibrariesPath/qt-cache" cmake --install . \
	&& cd .. \
	&& rm -rf qt_${QT}

FROM patches AS breakpad
RUN git clone https://chromium.googlesource.com/breakpad/breakpad.git \
	&& cd breakpad \
	&& git checkout dfcb7b6799 \
	&& git apply ../patches/breakpad.diff \
	&& git clone https://chromium.googlesource.com/linux-syscall-support.git src/third_party/lss \
	&& cd src/third_party/lss \
	&& git checkout e1e7b0ad8e \
	&& cd ../../.. \
	&& ./configure \
	&& make -j$(nproc) \
	&& make DESTDIR="${LibrariesPath}/breakpad-cache" install \
	&& cd .. \
	&& rm -rf breakpad

FROM builder AS webrtc
COPY --from=mozjpeg ${LibrariesPath}/mozjpeg-cache /
COPY --from=opus ${LibrariesPath}/opus-cache /
COPY --from=libvpx ${LibrariesPath}/libvpx-cache /
COPY --from=ffmpeg ${LibrariesPath}/ffmpeg-cache /
COPY --from=openssl ${LibrariesPath}/openssl-cache /
COPY --from=libXtst ${LibrariesPath}/libXtst-cache /

# Shallow clone on a specific commit.
RUN git init tg_owt \
	&& cd tg_owt \
	&& git remote add origin $GIT/desktop-app/tg_owt.git \
	&& git fetch --depth=1 origin 63a934db1ed212ebf8aaaa20f0010dd7b0d7b396 \
	&& git reset --hard FETCH_HEAD \
	&& git submodule update --init --recursive --depth=1 \
	&& rm -rf .git \
	&& cd src/third_party/pipewire \
	&& meson build -Dspa-plugins=disabled \
	&& cd ../../.. \
	&& cmake -G"Ninja Multi-Config" -B out . \
		-DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG $HFLAGS" \
		-DCMAKE_C_FLAGS_DEBUG="-g $HFLAGS_DEBUG" \
		-DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG $HFLAGS" \
		-DCMAKE_CXX_FLAGS_DEBUG="-g $HFLAGS_DEBUG" \
		-DTG_OWT_SPECIAL_TARGET=linux \
		-DTG_OWT_LIBJPEG_INCLUDE_PATH=/usr/local/include \
		-DTG_OWT_OPENSSL_INCLUDE_PATH=$OPENSSL_PREFIX/include \
		-DTG_OWT_OPUS_INCLUDE_PATH=/usr/local/include/opus \
		-DTG_OWT_LIBVPX_INCLUDE_PATH=/usr/local/include \
		-DTG_OWT_FFMPEG_INCLUDE_PATH=/usr/local/include

WORKDIR tg_owt

FROM webrtc AS webrtc_release
RUN cmake --build out --config Release --parallel \
	&& find out -mindepth 1 -maxdepth 1 ! -name Release -exec rm -rf {} \;

FROM webrtc AS webrtc_debug
ARG DEBUG
RUN [ -z "$DEBUG" ] || (cmake --build out --config Debug --parallel \
	&& find out -mindepth 1 -maxdepth 1 ! -name Debug -exec rm -rf {} \;)
RUN [ -n "$DEBUG" ] || mkdir -p out/Debug

FROM builder
COPY --from=libffi ${LibrariesPath}/libffi-cache /
COPY --from=zlib ${LibrariesPath}/zlib-cache /
COPY --from=xz ${LibrariesPath}/xz-cache /
COPY --from=libproxy ${LibrariesPath}/libproxy-cache /
COPY --from=mozjpeg ${LibrariesPath}/mozjpeg-cache /
COPY --from=opus ${LibrariesPath}/opus-cache /
COPY --from=libvpx ${LibrariesPath}/libvpx-cache /
COPY --from=rnnoise ${LibrariesPath}/rnnoise-cache /
COPY --from=xcb ${LibrariesPath}/xcb-cache /
COPY --from=xcb-wm ${LibrariesPath}/xcb-wm-cache /
COPY --from=xcb-util ${LibrariesPath}/xcb-util-cache /
COPY --from=xcb-image ${LibrariesPath}/xcb-image-cache /
COPY --from=xcb-keysyms ${LibrariesPath}/xcb-keysyms-cache /
COPY --from=xcb-render-util ${LibrariesPath}/xcb-render-util-cache /
COPY --from=libXext ${LibrariesPath}/libXext-cache /
COPY --from=libXfixes ${LibrariesPath}/libXfixes-cache /
COPY --from=libXtst ${LibrariesPath}/libXtst-cache /
COPY --from=libXrandr ${LibrariesPath}/libXrandr-cache /
COPY --from=libXrender ${LibrariesPath}/libXrender-cache /
COPY --from=libXdamage ${LibrariesPath}/libXdamage-cache /
COPY --from=libXcomposite ${LibrariesPath}/libXcomposite-cache /
COPY --from=wayland ${LibrariesPath}/wayland-cache /
COPY --from=ffmpeg ${LibrariesPath}/ffmpeg-cache /
COPY --from=openal ${LibrariesPath}/openal-cache /
COPY --from=openssl ${LibrariesPath}/openssl-cache /
COPY --from=xkbcommon ${LibrariesPath}/xkbcommon-cache /
COPY --from=libsigcplusplus ${LibrariesPath}/libsigcplusplus-cache /
COPY --from=glibmm ${LibrariesPath}/glibmm-cache /
COPY --from=qt ${LibrariesPath}/qt-cache /
COPY --from=breakpad ${LibrariesPath}/breakpad-cache /
COPY --from=webrtc ${LibrariesPath}/tg_owt tg_owt
COPY --from=webrtc_release ${LibrariesPath}/tg_owt/out/Release tg_owt/out/Release
COPY --from=webrtc_debug ${LibrariesPath}/tg_owt/out/Debug tg_owt/out/Debug

WORKDIR ../tdesktop
VOLUME [ "/usr/src/tdesktop" ]
CMD [ "/usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh" ]
