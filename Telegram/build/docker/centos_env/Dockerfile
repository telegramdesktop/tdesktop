{%- set GIT = "https://github.com" -%}
{%- set GIT_FREEDESKTOP = GIT ~ "/gitlab-freedesktop-mirrors" -%}
{%- set QT = "6_4_0" -%}
{%- set QT_TAG = "v6.4.0" -%}
{%- set QT_PREFIX = "/usr/local/desktop-app/Qt-6.4.0" -%}
{%- set OPENSSL_VER = "1_1_1" -%}
{%- set OPENSSL_PREFIX = "/usr/local/desktop-app/openssl-1.1.1" -%}
{%- set CMAKE_VER = "3.21.3" -%}
{%- set CMAKE_FILE = "cmake-" ~ CMAKE_VER ~ "-Linux-x86_64.sh" -%}
{%- set HFLAGS_DEBUG = "-fstack-protector-all -fstack-clash-protection -fPIC" -%}
{%- set HFLAGS = HFLAGS_DEBUG ~ " -D_FORTIFY_SOURCE=2" -%}
{%- set LibrariesPath = "/usr/src/Libraries" -%}

# syntax=docker/dockerfile:1

FROM centos:7 AS builder
ENV PKG_CONFIG_PATH /usr/local/lib64/pkgconfig:/usr/local/lib/pkgconfig:/usr/local/share/pkgconfig

RUN yum -y install https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm \
	&& yum -y install https://packages.endpointdev.com/rhel/7/os/x86_64/endpoint-repo.x86_64.rpm \
	&& yum -y install centos-release-scl \
	&& yum -y install git python3-pip autoconf automake libtool patch \
		fontconfig-devel freetype-devel libX11-devel at-spi2-core-devel alsa-lib-devel \
		pulseaudio-libs-devel mesa-libGL-devel mesa-libEGL-devel mesa-libgbm-devel \
		libdrm-devel vulkan-devel gtk3-devel \
		perl-XML-Parser pkgconfig bison yasm file which xorg-x11-util-macros \
		devtoolset-10-make devtoolset-10-gcc devtoolset-10-gcc-c++ \
		devtoolset-10-binutils llvm-toolset-7.0 llvm-toolset-7.0-clang-devel \
		llvm-toolset-7.0-llvm-devel \
	&& yum clean all

RUN python3 -m pip install meson
RUN python3 -m pip install ninja

# Fix a bug with argument naming in CentOS 7 glibc
RUN sed -i 's/char \*__block/char */' /usr/include/unistd.h

SHELL [ "bash", "-c", ". /opt/rh/devtoolset-10/enable; exec bash -c \"$@\"", "-s"]

WORKDIR {{ LibrariesPath }}

RUN mkdir /opt/cmake \
	&& curl -sSLo {{ CMAKE_FILE }} {{ GIT }}/Kitware/CMake/releases/download/v{{ CMAKE_VER }}/{{ CMAKE_FILE }} \
	&& sh {{ CMAKE_FILE }} --prefix=/opt/cmake --skip-license \
	&& ln -s /opt/cmake/bin/cmake /usr/local/bin/cmake \
	&& rm {{ CMAKE_FILE }}

FROM builder AS patches
RUN git clone {{ GIT }}/desktop-app/patches.git \
	&& cd patches \
	&& git checkout 355c563138 \
	&& rm -rf .git

FROM builder AS nasm
RUN git clone -b nasm-2.15.05 --depth=1 {{ GIT }}/netwide-assembler/nasm.git \
	&& cd nasm \
	&& ./autogen.sh \
	&& ./configure \
	&& make -j$(nproc) \
	&& mkdir -p "{{ LibrariesPath }}/nasm-cache/usr/local/bin" \
	&& install -c nasm "{{ LibrariesPath }}/nasm-cache/usr/local/bin/nasm" \
	&& cd .. \
	&& rm -rf nasm

FROM builder AS libffi
RUN git clone -b v3.4.2 --depth=1 {{ GIT }}/libffi/libffi.git \
	&& cd libffi \
	&& ./autogen.sh \
	&& ./configure --enable-static --disable-docs \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/libffi-cache" install \
	&& cd .. \
	&& rm -rf libffi

FROM builder AS zlib
RUN git clone -b v1.2.11 --depth=1 {{ GIT }}/madler/zlib.git \
	&& cd zlib \
	&& CFLAGS="-O3 {{ HFLAGS }}" ./configure --static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/zlib-cache" install \
	&& cd .. \
	&& rm -rf zlib

FROM builder AS xz
RUN git clone -b v5.2.5 https://git.tukaani.org/xz.git \
	&& cd xz \
	&& CFLAGS="{{ HFLAGS }}" cmake -GNinja -B build . -DCMAKE_BUILD_TYPE=Release \
	&& cmake --build build --parallel \
	&& DESTDIR="{{ LibrariesPath }}/xz-cache" cmake --install build \
	&& cd .. \
	&& rm -rf xz

FROM patches AS libproxy
RUN git clone -b 0.4.17 --depth=1 {{ GIT }}/libproxy/libproxy.git \
	&& cd libproxy \
	&& git apply ../patches/libproxy.patch \
	&& CFLAGS="{{ HFLAGS }}" CXXFLAGS="{{ HFLAGS }}" cmake -GNinja -B build . \
		-DCMAKE_BUILD_TYPE=Release \
		-DWITH_DBUS=OFF \
		-DWITH_NM=OFF \
		-DWITH_NMold=OFF \
	&& cmake --build build --parallel \
	&& DESTDIR="{{ LibrariesPath }}/libproxy-cache" cmake --install build \
	&& cd .. \
	&& rm -rf libproxy

FROM builder AS lcms2
RUN git clone -b lcms2.13.1 --depth=1 {{ GIT }}/mm2/Little-CMS.git \
	&& cd Little-CMS \
	&& meson build \
		--buildtype=release \
		--default-library=both \
	&& meson compile -C build \
	&& DESTDIR="{{ LibrariesPath }}/lcms2-cache" meson install -C build \
	&& cd .. \
	&& rm -rf Little-CMS

FROM builder AS brotli
RUN git clone -b v1.0.9 --depth=1 {{ GIT }}/google/brotli.git \
	&& cd brotli \
	&& cmake -GNinja -B build . \
		-DCMAKE_BUILD_TYPE=Release \
		-DBROTLI_DISABLE_TESTS=ON \
	&& cmake --build build --parallel \
	&& DESTDIR="{{ LibrariesPath }}/brotli-cache" cmake --install build \
	&& cd .. \
	&& rm -rf brotli

FROM builder AS highway
RUN git clone -b 1.0.1 --depth=1 {{ GIT }}/google/highway.git \
	&& cd highway \
	&& cmake -GNinja -B build . \
		-DCMAKE_BUILD_TYPE=Release \
		-DBUILD_TESTING=OFF \
		-DHWY_ENABLE_CONTRIB=OFF \
		-DHWY_ENABLE_EXAMPLES=OFF \
	&& cmake --build build --parallel \
	&& DESTDIR="{{ LibrariesPath }}/highway-cache" cmake --install build \
	&& cd .. \
	&& rm -rf highway

FROM builder AS mozjpeg
RUN git clone -b v4.0.3 --depth=1 {{ GIT }}/mozilla/mozjpeg.git \
	&& cd mozjpeg \
	&& CFLAGS="{{ HFLAGS }}" cmake -GNinja -B build . \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX=/usr/local \
		-DWITH_JPEG8=ON \
		-DPNG_SUPPORTED=OFF \
	&& cmake --build build --parallel \
	&& DESTDIR="{{ LibrariesPath }}/mozjpeg-cache" cmake --install build \
	&& cd .. \
	&& rm -rf mozjpeg

FROM builder AS opus
RUN git clone -b v1.3.1 --depth=1 {{ GIT }}/xiph/opus.git \
	&& cd opus \
	&& ./autogen.sh \
	&& CFLAGS="-g -O2 {{ HFLAGS }}" ./configure \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/opus-cache" install \
	&& cd .. \
	&& rm -rf opus

FROM builder AS dav1d
COPY --link --from=nasm {{ LibrariesPath }}/nasm-cache /

RUN git clone -b 1.0.0 --depth=1 {{ GIT }}/videolan/dav1d.git \
	&& cd dav1d \
	&& meson build \
		--buildtype=release \
		--default-library=both \
		-Denable_tools=false \
		-Denable_tests=false \
	&& meson compile -C build \
	&& DESTDIR="{{ LibrariesPath }}/dav1d-cache" meson install -C build \
	&& cd .. \
	&& rm -rf dav1d

FROM builder AS libde265
RUN git clone -b v1.0.8 --depth=1 {{ GIT }}/strukturag/libde265.git \
	&& cd libde265 \
	&& cmake -GNinja . \
		-DCMAKE_BUILD_TYPE=Release \
		-DBUILD_SHARED_LIBS=OFF \
		-DENABLE_SDL=OFF \
	&& cmake --build . --parallel \
	&& DESTDIR="{{ LibrariesPath }}/libde265-cache" cmake --install . \
	&& cd .. \
	&& rm -rf libde265

FROM builder AS libvpx
RUN git clone -b v1.11.0 --depth=1 {{ GIT }}/webmproject/libvpx.git \
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
	&& make DESTDIR="{{ LibrariesPath }}/libvpx-cache" install \
	&& cd .. \
	&& rm -rf libvpx

FROM builder AS libavif
COPY --link --from=dav1d {{ LibrariesPath }}/dav1d-cache /

RUN git clone -b v0.10.1 --depth=1 {{ GIT }}/AOMediaCodec/libavif.git \
	&& cd libavif \
	&& cmake -GNinja -B build . \
		-DCMAKE_BUILD_TYPE=Release \
		-DBUILD_SHARED_LIBS=OFF \
		-DAVIF_CODEC_DAV1D=ON \
	&& cmake --build build --parallel \
	&& DESTDIR="{{ LibrariesPath }}/libavif-cache" cmake --install build \
	&& cd .. \
	&& rm -rf libavif

FROM builder AS libheif
COPY --link --from=libde265 {{ LibrariesPath }}/libde265-cache /

RUN git clone -b v1.13.0 --depth=1 {{ GIT }}/strukturag/libheif.git \
	&& cd libheif \
	&& cmake -GNinja -B build . \
		-DCMAKE_BUILD_TYPE=Release \
		-DBUILD_SHARED_LIBS=OFF \
		-DWITH_X265=OFF \
		-DWITH_AOM=OFF \
		-DWITH_RAV1E=OFF \
		-DWITH_DAV1D=OFF \
		-DWITH_EXAMPLES=OFF \
	&& cmake --build build --parallel \
	&& DESTDIR="{{ LibrariesPath }}/libheif-cache" cmake --install build \
	&& cd .. \
	&& rm -rf libheif

FROM builder AS libjxl
COPY --link --from=lcms2 {{ LibrariesPath }}/lcms2-cache /
COPY --link --from=brotli {{ LibrariesPath }}/brotli-cache /
COPY --link --from=highway {{ LibrariesPath }}/highway-cache /

RUN git clone -b v0.7.0 --depth=1 {{ GIT }}/libjxl/libjxl.git \
	&& cd libjxl \
	&& cmake -GNinja -B build . \
		-DCMAKE_BUILD_TYPE=Release \
		-DBUILD_SHARED_LIBS=OFF \
		-DBUILD_TESTING=OFF \
		-DJPEGXL_ENABLE_DEVTOOLS=OFF \
		-DJPEGXL_ENABLE_TOOLS=OFF \
		-DJPEGXL_ENABLE_DOXYGEN=OFF \
		-DJPEGXL_ENABLE_MANPAGES=OFF \
		-DJPEGXL_ENABLE_BENCHMARK=OFF \
		-DJPEGXL_ENABLE_EXAMPLES=OFF \
		-DJPEGXL_ENABLE_JNI=OFF \
		-DJPEGXL_ENABLE_SJPEG=OFF \
		-DJPEGXL_ENABLE_OPENEXR=OFF \
		-DJPEGXL_ENABLE_SKCMS=OFF \
	&& cmake --build build --parallel \
	&& DESTDIR="{{ LibrariesPath }}/libjxl-cache" cmake --install build \
	&& cd .. \
	&& rm -rf libjxl

FROM builder AS rnnoise
RUN git clone -b master --depth=1 {{ GIT }}/desktop-app/rnnoise \
	&& cd rnnoise \
	&& CFLAGS="{{ HFLAGS }}" cmake -GNinja -B build . -DCMAKE_BUILD_TYPE=Release \
	&& cmake --build build --parallel \
	&& mkdir -p "{{ LibrariesPath }}/rnnoise-cache/usr/local/include" \
	&& cp "include/rnnoise.h" "{{ LibrariesPath }}/rnnoise-cache/usr/local/include/" \
	&& mkdir -p "{{ LibrariesPath }}/rnnoise-cache/usr/local/lib" \
	&& cp "build/librnnoise.a" "{{ LibrariesPath }}/rnnoise-cache/usr/local/lib/" \
	&& cd .. \
	&& rm -rf rnnoise

FROM builder AS xcb-proto
RUN git clone -b xcb-proto-1.14.1 --depth=1 {{ GIT_FREEDESKTOP }}/xcbproto.git \
	&& cd xcbproto \
	&& ./autogen.sh \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/xcb-proto-cache" install \
	&& cd .. \
	&& rm -rf xcbproto

FROM builder AS xcb
COPY --link --from=xcb-proto {{ LibrariesPath }}/xcb-proto-cache /

RUN git clone -b libxcb-1.14 --depth=1 {{ GIT_FREEDESKTOP }}/libxcb.git \
	&& cd libxcb \
	&& CFLAGS="-g -O2 {{ HFLAGS }}" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/xcb-cache" install \
	&& cd .. \
	&& rm -rf libxcb

FROM builder AS xcb-wm
RUN git clone -b 0.4.1 --depth=1 --recursive {{ GIT_FREEDESKTOP }}/libxcb-wm.git \
	&& cd libxcb-wm \
	&& ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/xcb-wm-cache" install \
	&& cd .. \
	&& rm -rf libxcb-wm

FROM builder AS xcb-util
RUN git clone -b 0.4.0 --depth=1 --recursive {{ GIT_FREEDESKTOP }}/libxcb-util.git \
	&& cd libxcb-util \
	&& ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/xcb-util-cache" install \
	&& cd .. \
	&& rm -rf libxcb-util

FROM builder AS xcb-image
COPY --link --from=xcb-util {{ LibrariesPath }}/xcb-util-cache /

RUN git clone -b 0.4.0 --depth=1 --recursive {{ GIT_FREEDESKTOP }}/libxcb-image.git \
	&& cd libxcb-image \
	&& CFLAGS="-g -O2 {{ HFLAGS }}" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/xcb-image-cache" install \
	&& cd .. \
	&& rm -rf libxcb-image

FROM builder AS xcb-keysyms
RUN git clone -b 0.4.0 --depth=1 --recursive {{ GIT_FREEDESKTOP }}/libxcb-keysyms.git \
	&& cd libxcb-keysyms \
	&& CFLAGS="-g -O2 {{ HFLAGS }}" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/xcb-keysyms-cache" install \
	&& cd .. \
	&& rm -rf libxcb-keysyms

FROM builder AS xcb-render-util
RUN git clone -b 0.3.9 --depth=1 --recursive {{ GIT_FREEDESKTOP }}/libxcb-render-util.git \
	&& cd libxcb-render-util \
	&& CFLAGS="-g -O2 {{ HFLAGS }}" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/xcb-render-util-cache" install \
	&& cd .. \
	&& rm -rf libxcb-render-util

FROM builder AS libXext
RUN git clone -b libXext-1.3.4 --depth=1 {{ GIT_FREEDESKTOP }}/libxext.git \
	&& cd libxext \
	&& CFLAGS="-g -O2 {{ HFLAGS }}" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/libXext-cache" install \
	&& cd .. \
	&& rm -rf libxext

FROM builder AS libXtst
RUN git clone -b libXtst-1.2.3 --depth=1 {{ GIT_FREEDESKTOP }}/libxtst.git \
	&& cd libxtst \
	&& CFLAGS="-g -O2 {{ HFLAGS }}" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/libXtst-cache" install \
	&& cd .. \
	&& rm -rf libxtst

FROM builder AS libXfixes
RUN git clone -b libXfixes-5.0.3 --depth=1 {{ GIT_FREEDESKTOP }}/libxfixes.git \
	&& cd libxfixes \
	&& CFLAGS="-g -O2 {{ HFLAGS }}" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/libXfixes-cache" install \
	&& cd .. \
	&& rm -rf libxfixes

FROM builder AS libXrandr
RUN git clone -b libXrandr-1.5.2 --depth=1 {{ GIT_FREEDESKTOP }}/libxrandr.git \
	&& cd libxrandr \
	&& CFLAGS="-g -O2 {{ HFLAGS }}" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/libXrandr-cache" install \
	&& cd .. \
	&& rm -rf libxrandr

FROM builder AS libXrender
RUN git clone -b libXrender-0.9.10 --depth=1 {{ GIT_FREEDESKTOP }}/libxrender.git \
	&& cd libxrender \
	&& CFLAGS="-g -O2 {{ HFLAGS }}" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/libXrender-cache" install \
	&& cd .. \
	&& rm -rf libxrender

FROM builder AS libXdamage
RUN git clone -b libXdamage-1.1.5 --depth=1 {{ GIT_FREEDESKTOP }}/libxdamage.git \
	&& cd libxdamage \
	&& CFLAGS="-g -O2 {{ HFLAGS }}" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/libXdamage-cache" install \
	&& cd .. \
	&& rm -rf libxdamage

FROM builder AS libXcomposite
RUN git clone -b libXcomposite-0.4.5 --depth=1 {{ GIT_FREEDESKTOP }}/libxcomposite.git \
	&& cd libxcomposite \
	&& CFLAGS="-g -O2 {{ HFLAGS }}" ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/libXcomposite-cache" install \
	&& cd .. \
	&& rm -rf libxcomposite

FROM builder AS wayland
COPY --link --from=libffi {{ LibrariesPath }}/libffi-cache /

RUN git clone -b 1.21.0 --depth=1 {{ GIT_FREEDESKTOP }}/wayland.git \
	&& cd wayland \
	&& meson build \
		--buildtype=release \
		--default-library=both \
		-Dtests=false \
		-Ddocumentation=false \
		-Ddtd_validation=false \
		-Dicon_directory=/usr/share/icons \
	&& meson compile -C build \
	&& DESTDIR="{{ LibrariesPath }}/wayland-cache" meson install -C build \
	&& cd .. \
	&& rm -rf wayland

FROM builder AS nv-codec-headers
RUN git clone -b n11.1.5.1 --depth=1 {{ GIT }}/FFmpeg/nv-codec-headers.git \
	&& DESTDIR="{{ LibrariesPath }}/nv-codec-headers-cache" make -C nv-codec-headers install \
	&& rm -rf nv-codec-headers

FROM builder AS ffmpeg
COPY --link --from=opus {{ LibrariesPath }}/opus-cache /
COPY --link --from=libvpx {{ LibrariesPath }}/libvpx-cache /
COPY --link --from=nv-codec-headers {{ LibrariesPath }}/nv-codec-headers-cache /

RUN git init ffmpeg \
	&& cd ffmpeg \
	&& git remote add origin {{ GIT }}/FFmpeg/FFmpeg.git \
	&& git fetch --depth=1 origin cc33e73618a981de7fd96385ecb34719de031f16 \
	&& git reset --hard FETCH_HEAD \
	&& ./configure \
		--extra-cflags="-DCONFIG_SAFE_BITSTREAM_READER=1 {{ HFLAGS }}" \
		--extra-cxxflags="-DCONFIG_SAFE_BITSTREAM_READER=1 {{ HFLAGS }}" \
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
		--enable-decoder=ac3 \
		--enable-decoder=alac \
		--enable-decoder=av1 \
		--enable-decoder=av1_cuvid \
		--enable-decoder=eac3 \
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
	&& make DESTDIR="{{ LibrariesPath }}/ffmpeg-cache" install \
	&& cd .. \
	&& rm -rf ffmpeg

FROM builder AS pipewire
RUN git clone -b 0.3.25 --depth=1 {{ GIT }}/PipeWire/pipewire.git \
	&& cd pipewire \
	&& meson build \
		--buildtype=release \
		-Dtests=disabled \
		-Dexamples=disabled \
		-Dspa-plugins=disabled \
	&& meson compile -C build \
	&& DESTDIR="{{ LibrariesPath }}/pipewire-cache" meson install -C build \
	&& cd .. \
	&& rm -rf pipewire

FROM builder AS openal
COPY --link --from=pipewire {{ LibrariesPath }}/pipewire-cache /

RUN git clone -b 1.22.2 --depth=1 {{ GIT }}/kcat/openal-soft.git \
	&& cd openal-soft \
	&& CFLAGS="{{ HFLAGS }}" CXXFLAGS="{{ HFLAGS }}" cmake -GNinja -B build . \
		-DCMAKE_BUILD_TYPE=Release \
		-DLIBTYPE:STRING=STATIC \
		-DALSOFT_EXAMPLES=OFF \
		-DALSOFT_UTILS=OFF \
		-DALSOFT_INSTALL_CONFIG=OFF \
	&& cmake --build build --parallel \
	&& DESTDIR="{{ LibrariesPath }}/openal-cache" cmake --install build \
	&& cd .. \
	&& rm -rf openal-soft

FROM builder AS openssl
ENV opensslDir openssl_{{ OPENSSL_VER }}
RUN git clone -b OpenSSL_{{ OPENSSL_VER }}-stable --depth=1 {{ GIT }}/openssl/openssl.git $opensslDir \
	&& cd $opensslDir \
	&& ./config \
		--prefix="{{ OPENSSL_PREFIX }}" \
		--openssldir=/etc/ssl \
		no-tests \
		no-dso \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/openssl-cache" install_sw \
	&& cd .. \
	&& rm -rf $opensslDir

FROM builder AS xkbcommon
COPY --link --from=xcb {{ LibrariesPath }}/xcb-cache /

RUN git clone -b xkbcommon-1.3.1 --depth=1 {{ GIT }}/xkbcommon/libxkbcommon.git \
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
	&& DESTDIR="{{ LibrariesPath }}/xkbcommon-cache" meson install -C build \
	&& cd .. \
	&& rm -rf libxkbcommon

FROM builder AS mm-common
RUN git clone -b 1.0.3 --depth=1 {{ GIT }}/GNOME/mm-common.git \
	&& cd mm-common \
	&& NOCONFIGURE=1 ./autogen.sh \
	&& ./configure --enable-network \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/mm-common-cache" install \
	&& cd .. \
	&& rm -rf mm-common

FROM builder AS libsigcplusplus
COPY --link --from=mm-common {{ LibrariesPath }}/mm-common-cache /

RUN git clone -b 2.10.7 --depth=1 {{ GIT }}/libsigcplusplus/libsigcplusplus.git \
	&& cd libsigcplusplus \
	&& export ACLOCAL_PATH="/usr/local/share/aclocal" \
	&& NOCONFIGURE=1 ./autogen.sh \
	&& CFLAGS="-g -O2 {{ HFLAGS }}" CXXFLAGS="-g -O2 {{ HFLAGS }}" ./configure \
		--enable-maintainer-mode \
		--enable-static \
		--disable-documentation \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/libsigcplusplus-cache" install \
	&& cd .. \
	&& rm -rf libsigcplusplus

FROM patches AS glibmm
COPY --link --from=mm-common {{ LibrariesPath }}/mm-common-cache /
COPY --link --from=libsigcplusplus {{ LibrariesPath }}/libsigcplusplus-cache /

# equals to glib version of Ubuntu 14.04
RUN git clone -b 2.40.0 --depth=1 {{ GIT }}/GNOME/glibmm.git \
	&& cd glibmm \
	&& git apply ../patches/glibmm.patch \
	&& export ACLOCAL_PATH="/usr/local/share/aclocal" \
	&& NOCONFIGURE=1 ./autogen.sh \
	&& CC="gcc -flto {{ HFLAGS }}" CXX="g++ -flto {{ HFLAGS }}" AR=gcc-ar RANLIB=gcc-ranlib ./configure \
		--enable-maintainer-mode \
		--enable-static \
		--disable-documentation \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/glibmm-cache" install \
	&& cd .. \
	&& rm -rf glibmm

FROM patches AS qt
COPY --link --from=libffi {{ LibrariesPath }}/libffi-cache /
COPY --link --from=zlib {{ LibrariesPath }}/zlib-cache /
COPY --link --from=libproxy {{ LibrariesPath }}/libproxy-cache /
COPY --link --from=mozjpeg {{ LibrariesPath }}/mozjpeg-cache /
COPY --link --from=xcb {{ LibrariesPath }}/xcb-cache /
COPY --link --from=xcb-wm {{ LibrariesPath }}/xcb-wm-cache /
COPY --link --from=xcb-util {{ LibrariesPath }}/xcb-util-cache /
COPY --link --from=xcb-image {{ LibrariesPath }}/xcb-image-cache /
COPY --link --from=xcb-keysyms {{ LibrariesPath }}/xcb-keysyms-cache /
COPY --link --from=xcb-render-util {{ LibrariesPath }}/xcb-render-util-cache /
COPY --link --from=wayland {{ LibrariesPath }}/wayland-cache /
COPY --link --from=openssl {{ LibrariesPath }}/openssl-cache /
COPY --link --from=xkbcommon {{ LibrariesPath }}/xkbcommon-cache /

ENV OPENSSL_ROOT_DIR {{ OPENSSL_PREFIX }}

RUN git clone -b {{ QT_TAG }} --depth=1 git://code.qt.io/qt/qt5.git qt_{{ QT }} \
	&& cd qt_{{ QT }} \
	&& git submodule update --init --recursive --depth=1 qtbase qtwayland qtimageformats qtsvg qt5compat \
	&& cd qtbase \
	&& find ../../patches/qtbase_{{ QT }} -type f -print0 | sort -z | xargs -r0 git apply \
	&& cd .. \
	&& ./configure -prefix "{{ QT_PREFIX }}" \
		-release \
{%- if DEBUG %}
		-force-debug-info \
{%- endif %}
		-opensource \
		-confirm-license \
		-libproxy \
		-qt-libpng \
		-qt-harfbuzz \
		-qt-pcre \
		-no-icu \
		-no-feature-xcb-sm \
		-no-feature-egl-extension-platform-wayland \
		-no-feature-wayland-server \
		-no-feature-highdpiscaling \
		-static \
		-dbus-runtime \
		-openssl-linked \
		-nomake examples \
		-nomake tests \
	&& cmake --build . --parallel \
	&& DESTDIR="{{ LibrariesPath }}/qt-cache" cmake --install . \
	&& cd .. \
	&& rm -rf qt_{{ QT }}

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
	&& make DESTDIR="{{ LibrariesPath }}/breakpad-cache" install \
	&& cd .. \
	&& rm -rf breakpad

FROM builder AS webrtc
COPY --link --from=mozjpeg {{ LibrariesPath }}/mozjpeg-cache /
COPY --link --from=opus {{ LibrariesPath }}/opus-cache /
COPY --link --from=libvpx {{ LibrariesPath }}/libvpx-cache /
COPY --link --from=ffmpeg {{ LibrariesPath }}/ffmpeg-cache /
COPY --link --from=openssl {{ LibrariesPath }}/openssl-cache /
COPY --link --from=libXtst {{ LibrariesPath }}/libXtst-cache /
COPY --link --from=pipewire {{ LibrariesPath }}/pipewire-cache /

# Shallow clone on a specific commit.
RUN git init tg_owt \
	&& cd tg_owt \
	&& git remote add origin {{ GIT }}/desktop-app/tg_owt.git \
	&& git fetch --depth=1 origin 442d5bb593c0ae314960308d78f2016ad1f80c3e \
	&& git reset --hard FETCH_HEAD \
	&& git submodule update --init --recursive --depth=1 \
	&& rm -rf .git \
	&& cmake -G"Ninja Multi-Config" -B out . \
		-DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG {{ HFLAGS }}" \
		-DCMAKE_C_FLAGS_DEBUG="-g {{ HFLAGS_DEBUG }}" \
		-DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG {{ HFLAGS }}" \
		-DCMAKE_CXX_FLAGS_DEBUG="-g {{ HFLAGS_DEBUG }}" \
		-DTG_OWT_SPECIAL_TARGET=linux \
		-DTG_OWT_LIBJPEG_INCLUDE_PATH=/usr/local/include \
		-DTG_OWT_OPENSSL_INCLUDE_PATH={{ OPENSSL_PREFIX }}/include \
		-DTG_OWT_OPUS_INCLUDE_PATH=/usr/local/include/opus \
		-DTG_OWT_LIBVPX_INCLUDE_PATH=/usr/local/include \
		-DTG_OWT_FFMPEG_INCLUDE_PATH=/usr/local/include

WORKDIR tg_owt

FROM webrtc AS webrtc_release
RUN cmake --build out --config Release --parallel \
	&& find out -mindepth 1 -maxdepth 1 ! -name Release -exec rm -rf {} \;

{%- if DEBUG %}

FROM webrtc AS webrtc_debug
RUN cmake --build out --config Debug --parallel \
	&& find out -mindepth 1 -maxdepth 1 ! -name Debug -exec rm -rf {} \;
{%- endif %}

FROM builder
COPY --link --from=libffi {{ LibrariesPath }}/libffi-cache /
COPY --link --from=zlib {{ LibrariesPath }}/zlib-cache /
COPY --link --from=xz {{ LibrariesPath }}/xz-cache /
COPY --link --from=libproxy {{ LibrariesPath }}/libproxy-cache /
COPY --link --from=lcms2 {{ LibrariesPath }}/lcms2-cache /
COPY --link --from=brotli {{ LibrariesPath }}/brotli-cache /
COPY --link --from=highway {{ LibrariesPath }}/highway-cache /
COPY --link --from=mozjpeg {{ LibrariesPath }}/mozjpeg-cache /
COPY --link --from=opus {{ LibrariesPath }}/opus-cache /
COPY --link --from=dav1d {{ LibrariesPath }}/dav1d-cache /
COPY --link --from=libde265 {{ LibrariesPath }}/libde265-cache /
COPY --link --from=libvpx {{ LibrariesPath }}/libvpx-cache /
COPY --link --from=libavif {{ LibrariesPath }}/libavif-cache /
COPY --link --from=libheif {{ LibrariesPath }}/libheif-cache /
COPY --link --from=libjxl {{ LibrariesPath }}/libjxl-cache /
COPY --link --from=rnnoise {{ LibrariesPath }}/rnnoise-cache /
COPY --link --from=xcb {{ LibrariesPath }}/xcb-cache /
COPY --link --from=xcb-wm {{ LibrariesPath }}/xcb-wm-cache /
COPY --link --from=xcb-util {{ LibrariesPath }}/xcb-util-cache /
COPY --link --from=xcb-image {{ LibrariesPath }}/xcb-image-cache /
COPY --link --from=xcb-keysyms {{ LibrariesPath }}/xcb-keysyms-cache /
COPY --link --from=xcb-render-util {{ LibrariesPath }}/xcb-render-util-cache /
COPY --link --from=libXext {{ LibrariesPath }}/libXext-cache /
COPY --link --from=libXfixes {{ LibrariesPath }}/libXfixes-cache /
COPY --link --from=libXtst {{ LibrariesPath }}/libXtst-cache /
COPY --link --from=libXrandr {{ LibrariesPath }}/libXrandr-cache /
COPY --link --from=libXrender {{ LibrariesPath }}/libXrender-cache /
COPY --link --from=libXdamage {{ LibrariesPath }}/libXdamage-cache /
COPY --link --from=libXcomposite {{ LibrariesPath }}/libXcomposite-cache /
COPY --link --from=wayland {{ LibrariesPath }}/wayland-cache /
COPY --link --from=ffmpeg {{ LibrariesPath }}/ffmpeg-cache /
COPY --link --from=openal {{ LibrariesPath }}/openal-cache /
COPY --link --from=openssl {{ LibrariesPath }}/openssl-cache /
COPY --link --from=xkbcommon {{ LibrariesPath }}/xkbcommon-cache /
COPY --link --from=libsigcplusplus {{ LibrariesPath }}/libsigcplusplus-cache /
COPY --link --from=glibmm {{ LibrariesPath }}/glibmm-cache /
COPY --link --from=qt {{ LibrariesPath }}/qt-cache /
COPY --link --from=breakpad {{ LibrariesPath }}/breakpad-cache /
COPY --link --from=webrtc {{ LibrariesPath }}/tg_owt tg_owt
COPY --link --from=webrtc_release {{ LibrariesPath }}/tg_owt/out/Release tg_owt/out/Release

{%- if DEBUG %}
COPY --link --from=webrtc_debug {{ LibrariesPath }}/tg_owt/out/Debug tg_owt/out/Debug
{%- endif %}

WORKDIR ../tdesktop
VOLUME [ "/usr/src/tdesktop" ]
CMD [ "/usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh" ]
