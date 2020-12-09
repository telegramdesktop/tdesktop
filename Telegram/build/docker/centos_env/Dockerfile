FROM centos:7 AS builder

ENV GIT https://github.com
ENV PKG_CONFIG_PATH /usr/local/lib/pkgconfig
ENV QT 5_15_2
ENV QT_TAG v5.15.2
ENV QT_PREFIX /usr/local/desktop-app/Qt-5.15.2
ENV OPENSSL_VER 1_1_1
ENV OPENSSL_PREFIX /usr/local/desktop-app/openssl-1.1.1

RUN yum -y install https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm
RUN yum -y install https://packages.endpoint.com/rhel/7/os/x86_64/endpoint-repo-1.7-1.x86_64.rpm
RUN yum -y install centos-release-scl

RUN yum -y install git cmake3 zlib-devel gtk2-devel gtk3-devel libICE-devel \
	libSM-devel libdrm-devel autoconf automake libtool fontconfig-devel \
	freetype-devel libX11-devel at-spi2-core-devel alsa-lib-devel \
	pulseaudio-libs-devel mesa-libGL-devel mesa-libEGL-devel \
	pkgconfig bison yasm file which xorg-x11-util-macros \
	devtoolset-8-make devtoolset-8-gcc devtoolset-8-gcc-c++ \
	devtoolset-8-binutils

SHELL [ "scl", "enable", "devtoolset-8", "--", "bash", "-c" ]
RUN ln -s cmake3 /usr/bin/cmake

ENV LibrariesPath /usr/src/Libraries
WORKDIR $LibrariesPath

FROM builder AS patches
RUN git clone $GIT/desktop-app/patches.git
RUN cd patches && git checkout f22ccc5

FROM builder AS libffi
RUN git clone -b v3.3 --depth=1 $GIT/libffi/libffi.git

WORKDIR libffi
RUN ./autogen.sh
RUN ./configure --enable-static --disable-docs
RUN make -j$(nproc)
RUN make DESTDIR="$LibrariesPath/libffi-cache" install

WORKDIR ..
RUN rm -rf libffi

FROM builder AS xz
RUN git clone -b v5.2.5 https://git.tukaani.org/xz.git

WORKDIR xz
RUN cmake3 -B build . -DCMAKE_BUILD_TYPE=Release
RUN cmake3 --build build -j$(nproc)
RUN DESTDIR="$LibrariesPath/xz-cache" cmake3 --install build

WORKDIR ..
RUN rm -rf xz

FROM builder AS mozjpeg
RUN git clone -b v4.0.1-rc2 --depth=1 $GIT/mozilla/mozjpeg.git

WORKDIR mozjpeg
RUN cmake3 -B build . \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX=/usr/local \
	-DWITH_JPEG8=ON \
	-DPNG_SUPPORTED=OFF

RUN cmake3 --build build -j$(nproc)
RUN DESTDIR="$LibrariesPath/mozjpeg-cache" cmake3 --install build

WORKDIR ..
RUN rm -rf mozjpeg

FROM builder AS opus
RUN git clone -b v1.3 --depth=1 $GIT/xiph/opus.git

WORKDIR opus
RUN ./autogen.sh
RUN ./configure
RUN make -j$(nproc)
RUN make DESTDIR="$LibrariesPath/opus-cache" install

WORKDIR ..
RUN rm -rf opus

FROM builder AS xcb-proto
RUN git clone -b xcb-proto-1.14 --depth=1 https://gitlab.freedesktop.org/xorg/proto/xcbproto.git

WORKDIR xcbproto
RUN ./autogen.sh --enable-static
RUN make -j$(nproc)
RUN make DESTDIR="$LibrariesPath/xcb-proto-cache" install

WORKDIR ..
RUN rm -rf xcbproto

FROM builder AS xcb
COPY --from=xcb-proto ${LibrariesPath}/xcb-proto-cache /

RUN git clone -b libxcb-1.14 --depth=1 https://gitlab.freedesktop.org/xorg/lib/libxcb.git

WORKDIR libxcb
RUN ./autogen.sh --enable-static
RUN make -j$(nproc)
RUN make DESTDIR="$LibrariesPath/xcb-cache" install

WORKDIR ..
RUN rm -rf libxcb

FROM builder AS xcb-wm

RUN git clone --recursive https://gitlab.freedesktop.org/xorg/lib/libxcb-wm.git

WORKDIR libxcb-wm
RUN git checkout 0.4.1
RUN ./autogen.sh --enable-static
RUN make -j$(nproc)
RUN make DESTDIR="$LibrariesPath/xcb-wm-cache" install

FROM builder AS xcb-util

RUN git clone --recursive https://gitlab.freedesktop.org/xorg/lib/libxcb-util.git

WORKDIR libxcb-util
RUN git checkout 0.4.0
RUN ./autogen.sh --enable-static
RUN make -j$(nproc)
RUN make DESTDIR="$LibrariesPath/xcb-util-cache" install

FROM builder AS xcb-image
COPY --from=xcb-util ${LibrariesPath}/xcb-util-cache /

RUN git clone --recursive https://gitlab.freedesktop.org/xorg/lib/libxcb-image.git

WORKDIR libxcb-image
RUN git checkout 0.4.0
RUN ./autogen.sh --enable-static
RUN make -j$(nproc)
RUN make DESTDIR="$LibrariesPath/xcb-image-cache" install

FROM builder AS xcb-keysyms

RUN git clone --recursive https://gitlab.freedesktop.org/xorg/lib/libxcb-keysyms.git

WORKDIR libxcb-keysyms
RUN git checkout 0.4.0
RUN ./autogen.sh --enable-static
RUN make -j$(nproc)
RUN make DESTDIR="$LibrariesPath/xcb-keysyms-cache" install

FROM builder AS xcb-render-util

RUN git clone --recursive https://gitlab.freedesktop.org/xorg/lib/libxcb-render-util.git

WORKDIR libxcb-render-util
RUN git checkout 0.3.9
RUN ./autogen.sh --enable-static
RUN make -j$(nproc)
RUN make DESTDIR="$LibrariesPath/xcb-render-util-cache" install

FROM builder AS libXext
RUN git clone -b libXext-1.3.4 --depth=1 https://gitlab.freedesktop.org/xorg/lib/libxext.git

WORKDIR libxext
RUN ./autogen.sh --enable-static
RUN make -j$(nproc)
RUN make DESTDIR="$LibrariesPath/libXext-cache" install

WORKDIR ..
RUN rm -rf libxext

FROM builder AS libXfixes
RUN git clone -b libXfixes-5.0.3 --depth=1 https://gitlab.freedesktop.org/xorg/lib/libxfixes.git

WORKDIR libxfixes
RUN ./autogen.sh --enable-static
RUN make -j$(nproc)
RUN make DESTDIR="$LibrariesPath/libXfixes-cache" install

WORKDIR ..
RUN rm -rf libxfixes

FROM builder AS wayland
COPY --from=libffi ${LibrariesPath}/libffi-cache /

RUN git clone -b 1.18.0 --depth=1 https://gitlab.freedesktop.org/wayland/wayland.git

WORKDIR wayland
RUN ./autogen.sh \
	--enable-static \
	--disable-documentation \
	--disable-dtd-validation

RUN make -j$(nproc)
RUN make DESTDIR="$LibrariesPath/wayland-cache" install

WORKDIR ..
RUN rm -rf wayland

FROM builder AS libva

COPY --from=libffi ${LibrariesPath}/libffi-cache /
COPY --from=libXext ${LibrariesPath}/libXext-cache /
COPY --from=libXfixes ${LibrariesPath}/libXfixes-cache /
COPY --from=wayland ${LibrariesPath}/wayland-cache /

RUN git clone -b 2.9.0 --depth=1 $GIT/intel/libva.git

WORKDIR libva
RUN ./autogen.sh --enable-static
RUN make -j$(nproc)
RUN make DESTDIR="$LibrariesPath/libva-cache" install

WORKDIR ..
RUN rm -rf libva

FROM builder AS libvdpau
RUN git clone -b libvdpau-1.2 --depth=1 https://gitlab.freedesktop.org/vdpau/libvdpau.git

WORKDIR libvdpau
RUN ./autogen.sh --enable-static
RUN make -j$(nproc)
RUN make DESTDIR="$LibrariesPath/libvdpau-cache" install

WORKDIR ..
RUN rm -rf libvdpau

FROM builder AS ffmpeg

COPY --from=opus ${LibrariesPath}/opus-cache /
COPY --from=libva ${LibrariesPath}/libva-cache /
COPY --from=libvdpau ${LibrariesPath}/libvdpau-cache /

RUN git clone -b release/4.2 --depth=1 $GIT/FFmpeg/FFmpeg.git ffmpeg

WORKDIR ffmpeg
RUN ./configure \
	--disable-debug \
	--disable-programs \
	--disable-doc \
	--disable-network \
	--disable-autodetect \
	--disable-everything \
	--disable-alsa \
	--disable-iconv \
	--enable-libopus \
	--enable-vaapi \
	--enable-vdpau \
	--enable-protocol=file \
	--enable-hwaccel=h264_vaapi \
	--enable-hwaccel=h264_vdpau \
	--enable-hwaccel=mpeg4_vaapi \
	--enable-hwaccel=mpeg4_vdpau \
	--enable-decoder=aac \
	--enable-decoder=aac_fixed \
	--enable-decoder=aac_latm \
	--enable-decoder=aasc \
	--enable-decoder=alac \
	--enable-decoder=flac \
	--enable-decoder=gif \
	--enable-decoder=h264 \
	--enable-decoder=h264_vdpau \
	--enable-decoder=hevc \
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
	--enable-demuxer=m4v \
	--enable-demuxer=mov \
	--enable-demuxer=mp3 \
	--enable-demuxer=ogg \
	--enable-demuxer=wav \
	--enable-muxer=ogg \
	--enable-muxer=opus

RUN make -j$(nproc)
RUN make DESTDIR="$LibrariesPath/ffmpeg-cache" install

FROM builder AS openal
RUN git clone -b openal-soft-1.21.0 --depth=1 $GIT/kcat/openal-soft.git

WORKDIR openal-soft
RUN cmake3 -B build . \
	-DCMAKE_BUILD_TYPE=Release \
	-DLIBTYPE:STRING=STATIC \
	-DALSOFT_EXAMPLES=OFF \
	-DALSOFT_TESTS=OFF \
	-DALSOFT_UTILS=OFF \
	-DALSOFT_CONFIG=OFF

RUN cmake3 --build build -j$(nproc)
RUN DESTDIR="$LibrariesPath/openal-cache" cmake3 --install build

WORKDIR ..
RUN rm -rf openal

FROM builder AS openssl
ENV opensslDir openssl_${OPENSSL_VER}
RUN git clone -b OpenSSL_${OPENSSL_VER}-stable --depth=1 \
	$GIT/openssl/openssl.git $opensslDir

WORKDIR ${opensslDir}
RUN ./config --prefix="$OPENSSL_PREFIX" no-tests
RUN make -j$(nproc)
RUN make DESTDIR="$LibrariesPath/openssl-cache" install_sw

WORKDIR ..
RUN rm -rf $opensslDir

FROM builder AS xkbcommon
RUN git clone -b xkbcommon-0.8.4 --depth=1 $GIT/xkbcommon/libxkbcommon.git

WORKDIR libxkbcommon
RUN ./autogen.sh \
	--disable-docs \
	--disable-wayland \
	--with-xkb-config-root=/usr/share/X11/xkb \
	--with-x-locale-root=/usr/share/X11/locale

RUN make -j$(nproc)
RUN make DESTDIR="$LibrariesPath/xkbcommon-cache" install

WORKDIR ..
RUN rm -rf libxkbcommon

FROM patches AS qt

COPY --from=libffi ${LibrariesPath}/libffi-cache /
COPY --from=mozjpeg ${LibrariesPath}/mozjpeg-cache /
COPY --from=xcb ${LibrariesPath}/xcb-cache /
COPY --from=xcb-wm ${LibrariesPath}/xcb-wm-cache /
COPY --from=xcb-util ${LibrariesPath}/xcb-util-cache /
COPY --from=xcb-image ${LibrariesPath}/xcb-image-cache /
COPY --from=xcb-keysyms ${LibrariesPath}/xcb-keysyms-cache /
COPY --from=xcb-render-util ${LibrariesPath}/xcb-render-util-cache /
COPY --from=libXext ${LibrariesPath}/libXext-cache /
COPY --from=libXfixes ${LibrariesPath}/libXfixes-cache /
COPY --from=wayland ${LibrariesPath}/wayland-cache /
COPY --from=openssl ${LibrariesPath}/openssl-cache /
COPY --from=xkbcommon ${LibrariesPath}/xkbcommon-cache /

RUN git clone -b ${QT_TAG} --depth=1 git://code.qt.io/qt/qt5.git qt_${QT}
WORKDIR qt_${QT}
RUN perl init-repository --module-subset=qtbase,qtwayland,qtimageformats,qtsvg
RUN git submodule update qtbase qtwayland qtimageformats qtsvg

WORKDIR qtbase
RUN find ../../patches/qtbase_${QT} -type f -print0 | sort -z | xargs -r0 git apply
WORKDIR ..

# I couldn't make it work with direct ./configure call :(
RUN echo './configure -prefix '$'\"''$QT_PREFIX'$'\"'' \
	-release \
	-opensource \
	-confirm-license \
	-xcb \
	-qt-libpng \
	-qt-harfbuzz \
	-qt-pcre \
	-no-icu \
	-no-gtk \
	-no-feature-wayland-server \
	-static \
	-dbus-runtime \
	-openssl-linked \
	-I '$'\"''$OPENSSL_PREFIX/include'$'\"'' \
	OPENSSL_LIBS='$'\"''$OPENSSL_PREFIX/lib/libssl.a $OPENSSL_PREFIX/lib/libcrypto.a -lz -ldl -lpthread'$'\"'' \
	-nomake examples \
	-nomake tests \
	-L /usr/local/lib64' >> ./run_configure.sh
RUN cat ./run_configure.sh
RUN chmod a+x ./run_configure.sh
RUN ./run_configure.sh
RUN rm ./run_configure.sh

RUN make -j$(nproc)
RUN make INSTALL_ROOT="$LibrariesPath/qt-cache" install

WORKDIR ..
RUN rm -rf qt_${QT}

FROM patches AS breakpad
RUN git clone https://chromium.googlesource.com/breakpad/breakpad.git

WORKDIR breakpad
RUN git checkout bc8fb886
RUN git clone https://chromium.googlesource.com/linux-syscall-support.git src/third_party/lss

WORKDIR src/third_party/lss
RUN git checkout a91633d1
WORKDIR ${LibrariesPath}

ENV BreakpadCache ${LibrariesPath}/breakpad-cache
RUN git clone https://chromium.googlesource.com/external/gyp.git

WORKDIR gyp
RUN git checkout 9f2a7bb1
RUN git apply ../patches/gyp.diff

WORKDIR ../breakpad
RUN ./configure
RUN make -j$(nproc)
RUN make DESTDIR="$BreakpadCache" install

WORKDIR src
RUN rm -rf testing
RUN git clone $GIT/google/googletest.git testing

WORKDIR tools
RUN sed -i 's/minidump_upload.m/minidump_upload.cc/' linux/tools_linux.gypi
RUN ../../../gyp/gyp  --depth=. --generator-output=.. -Goutput_dir=../out tools.gyp --format=cmake

WORKDIR ../../out/Default
RUN cmake3 .
RUN cmake3 --build . --target dump_syms -j$(nproc)
RUN mv dump_syms $BreakpadCache

WORKDIR ..
RUN rm -rf gyp

FROM builder AS webrtc

COPY --from=mozjpeg ${LibrariesPath}/mozjpeg-cache /
COPY --from=opus ${LibrariesPath}/opus-cache /
COPY --from=ffmpeg ${LibrariesPath}/ffmpeg-cache /
COPY --from=openssl ${LibrariesPath}/openssl-cache /

RUN git clone --recursive $GIT/desktop-app/tg_owt.git

WORKDIR tg_owt
RUN git checkout 75ac669

RUN cmake3 -B out/Release . \
	-DCMAKE_BUILD_TYPE=Release \
	-DTG_OWT_SPECIAL_TARGET=linux \
	-DTG_OWT_LIBJPEG_INCLUDE_PATH=/usr/local/include \
	-DTG_OWT_OPENSSL_INCLUDE_PATH=$OPENSSL_PREFIX/include \
	-DTG_OWT_OPUS_INCLUDE_PATH=/usr/local/include/opus \
	-DTG_OWT_FFMPEG_INCLUDE_PATH=/usr/local/include

RUN cmake3 --build out/Release -- -j8

RUN cmake3 -B out/Debug . \
	-DCMAKE_BUILD_TYPE=Debug \
	-DTG_OWT_SPECIAL_TARGET=linux \
	-DTG_OWT_LIBJPEG_INCLUDE_PATH=/usr/local/include \
	-DTG_OWT_OPENSSL_INCLUDE_PATH=$OPENSSL_PREFIX/include \
	-DTG_OWT_OPUS_INCLUDE_PATH=/usr/local/include/opus \
	-DTG_OWT_FFMPEG_INCLUDE_PATH=/usr/local/include

RUN cmake3 --build out/Debug -- -j8

FROM builder

COPY --from=libffi ${LibrariesPath}/libffi-cache /
COPY --from=xz ${LibrariesPath}/xz-cache /
COPY --from=mozjpeg ${LibrariesPath}/mozjpeg-cache /
COPY --from=opus ${LibrariesPath}/opus-cache /
COPY --from=xcb ${LibrariesPath}/xcb-cache /
COPY --from=xcb-wm ${LibrariesPath}/xcb-wm-cache /
COPY --from=xcb-util ${LibrariesPath}/xcb-util-cache /
COPY --from=xcb-image ${LibrariesPath}/xcb-image-cache /
COPY --from=xcb-keysyms ${LibrariesPath}/xcb-keysyms-cache /
COPY --from=xcb-render-util ${LibrariesPath}/xcb-render-util-cache /
COPY --from=libXext ${LibrariesPath}/libXext-cache /
COPY --from=libXfixes ${LibrariesPath}/libXfixes-cache /
COPY --from=wayland ${LibrariesPath}/wayland-cache /
COPY --from=libva ${LibrariesPath}/libva-cache /
COPY --from=libvdpau ${LibrariesPath}/libvdpau-cache /
COPY --from=ffmpeg ${LibrariesPath}/ffmpeg ffmpeg
COPY --from=ffmpeg ${LibrariesPath}/ffmpeg-cache /
COPY --from=openal ${LibrariesPath}/openal-cache /
COPY --from=openssl ${LibrariesPath}/openssl-cache /
COPY --from=xkbcommon ${LibrariesPath}/xkbcommon-cache /
COPY --from=qt ${LibrariesPath}/qt-cache /
COPY --from=breakpad ${LibrariesPath}/breakpad breakpad
COPY --from=breakpad ${LibrariesPath}/breakpad-cache /
COPY --from=webrtc ${LibrariesPath}/tg_owt tg_owt

WORKDIR ../tdesktop
VOLUME [ "/usr/src/tdesktop" ]
CMD [ "/usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh" ]
