{%- set LibrariesPath = "/usr/src/Libraries" -%}

# syntax=docker/dockerfile:1

FROM rockylinux:8 AS builder
ENV LANG C.UTF-8
ENV TOOLSET gcc-toolset-14
ENV PATH /opt/rh/$TOOLSET/root/usr/bin:$PATH
ENV LIBRARY_PATH /opt/rh/$TOOLSET/root/usr/lib64:/opt/rh/$TOOLSET/root/usr/lib:/usr/local/lib64:/usr/local/lib:/lib64:/lib:/usr/lib64:/usr/lib
ENV LD_LIBRARY_PATH $LIBRARY_PATH
ENV PKG_CONFIG_PATH /opt/rh/$TOOLSET/root/usr/lib64/pkgconfig:/opt/rh/$TOOLSET/root/usr/lib/pkgconfig:/usr/local/lib64/pkgconfig:/usr/local/lib/pkgconfig:/usr/local/share/pkgconfig

RUN dnf -y install epel-release \
	&& dnf config-manager --set-enabled powertools \
	&& dnf -y install cmake autoconf automake libtool pkgconfig make patch git \
		python3.11-pip python3.11-devel gperf flex bison clang clang-tools-extra \
		lld nasm yasm file which perl-open perl-XML-Parser perl-IPC-Cmd \
		xorg-x11-util-macros $TOOLSET-gcc $TOOLSET-gcc-c++ $TOOLSET-binutils \
		$TOOLSET-gdb $TOOLSET-libasan-devel libffi-devel fontconfig-devel \
		freetype-devel libX11-devel wayland-devel alsa-lib-devel \
		pulseaudio-libs-devel mesa-libGL-devel mesa-libEGL-devel mesa-libgbm-devel \
		libdrm-devel vulkan-devel libva-devel libvdpau-devel glib2-devel \
		gobject-introspection-devel at-spi2-core-devel gtk3-devel boost1.78-devel \
	&& dnf clean all

RUN alternatives --set python3 /usr/bin/python3.11
RUN python3 -m pip install meson ninja
RUN sed -i '/Requires.private: valgrind/d' /usr/lib64/pkgconfig/libdrm.pc
RUN echo set debuginfod enabled on > /opt/rh/$TOOLSET/root/etc/gdbinit.d/00-debuginfod.gdb
RUN adduser user

WORKDIR {{ LibrariesPath }}
ENV AR gcc-ar
ENV RANLIB gcc-ranlib
ENV NM gcc-nm
ENV CFLAGS {% if DEBUG %}-g{% endif %} -O3 {% if LTO %}-flto=auto -ffat-lto-objects{% endif %} -pipe -fPIC -fno-strict-aliasing -fexceptions -fasynchronous-unwind-tables -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -fhardened -Wno-hardened
ENV CXXFLAGS $CFLAGS

ENV CMAKE_GENERATOR Ninja
ENV CMAKE_BUILD_TYPE None
ENV CMAKE_BUILD_PARALLEL_LEVEL ''

FROM builder AS patches
RUN git init patches \
	&& cd patches \
	&& git remote add origin https://github.com/desktop-app/patches.git \
	&& git fetch --depth=1 origin 22989737aea515bf6a94d74a65490d37409831bc \
	&& git reset --hard FETCH_HEAD \
	&& rm -rf .git

FROM builder AS zlib
RUN git clone -b v1.3.1 --depth=1 https://github.com/madler/zlib.git \
	&& cd zlib \
	&& cmake -B build . -DZLIB_BUILD_EXAMPLES=OFF \
	&& cmake --build build \
	&& DESTDIR="{{ LibrariesPath }}/zlib-cache" cmake --install build \
	&& cd .. \
	&& rm -rf zlib

FROM builder AS xz
RUN git clone -b v5.8.1 --depth=1 https://github.com/tukaani-project/xz.git \
	&& cd xz \
	&& cmake -B build . \
	&& cmake --build build \
	&& DESTDIR="{{ LibrariesPath }}/xz-cache" cmake --install build \
	&& cd .. \
	&& rm -rf xz

FROM builder AS protobuf
RUN git clone -b v30.2 --depth=1 --recursive --shallow-submodules https://github.com/protocolbuffers/protobuf.git \
	&& cd protobuf \
	&& cmake -B build . \
		-Dprotobuf_BUILD_TESTS=OFF \
		-Dprotobuf_BUILD_PROTOBUF_BINARIES=ON \
		-Dprotobuf_BUILD_LIBPROTOC=ON \
		-Dprotobuf_WITH_ZLIB=OFF \
	&& cmake --build build \
	&& DESTDIR="{{ LibrariesPath }}/protobuf-cache" cmake --install build \
	&& cd .. \
	&& rm -rf protobuf

FROM builder AS lcms2
RUN git clone -b lcms2.15 --depth=1 https://github.com/mm2/Little-CMS.git \
	&& cd Little-CMS \
	&& meson build \
		--buildtype=plain \
		--default-library=both \
	&& meson compile -C build \
	&& DESTDIR="{{ LibrariesPath }}/lcms2-cache" meson install -C build \
	&& cd .. \
	&& rm -rf Little-CMS

FROM builder AS brotli
RUN git clone -b v1.1.0 --depth=1 https://github.com/google/brotli.git \
	&& cd brotli \
	&& cmake -B build . \
		-DBUILD_SHARED_LIBS=OFF \
		-DBROTLI_DISABLE_TESTS=ON \
	&& cmake --build build \
	&& DESTDIR="{{ LibrariesPath }}/brotli-cache" cmake --install build \
	&& cd .. \
	&& rm -rf brotli

FROM builder AS highway
RUN git clone -b 1.0.7 --depth=1 https://github.com/google/highway.git \
	&& cd highway \
	&& cmake -B build . \
		-DBUILD_TESTING=OFF \
		-DHWY_ENABLE_CONTRIB=OFF \
		-DHWY_ENABLE_EXAMPLES=OFF \
	&& cmake --build build \
	&& DESTDIR="{{ LibrariesPath }}/highway-cache" cmake --install build \
	&& cd .. \
	&& rm -rf highway

FROM builder AS opus
RUN git clone -b v1.5.2 --depth=1 https://github.com/xiph/opus.git \
	&& cd opus \
	&& cmake -B build . \
	&& cmake --build build \
	&& DESTDIR="{{ LibrariesPath }}/opus-cache" cmake --install build \
	&& cd .. \
	&& rm -rf opus

FROM builder AS dav1d
RUN git clone -b 1.4.1 --depth=1 https://github.com/videolan/dav1d.git \
	&& cd dav1d \
	&& meson build \
		--buildtype=plain \
		--default-library=both \
		-Denable_tools=false \
		-Denable_tests=false \
	&& meson compile -C build \
	&& DESTDIR="{{ LibrariesPath }}/dav1d-cache" meson install -C build \
	&& cd .. \
	&& rm -rf dav1d

FROM builder AS openh264
RUN git clone -b v2.4.1 --depth=1 https://github.com/cisco/openh264.git \
	&& cd openh264 \
	&& meson build \
		--buildtype=plain \
		--default-library=both \
	&& meson compile -C build \
	&& DESTDIR="{{ LibrariesPath }}/openh264-cache" meson install -C build \
	&& cd .. \
	&& rm -rf openh264

FROM builder AS libde265
RUN git clone -b v1.0.15 --depth=1 https://github.com/strukturag/libde265.git \
	&& cd libde265 \
	&& cmake -B build . \
		-DCMAKE_BUILD_TYPE=None \
		-DBUILD_SHARED_LIBS=OFF \
		-DENABLE_DECODER=OFF \
		-DENABLE_SDL=OFF \
	&& cmake --build build \
	&& DESTDIR="{{ LibrariesPath }}/libde265-cache" cmake --install build \
	&& cd .. \
	&& rm -rf libde265

FROM builder AS libvpx
RUN git init libvpx \
	&& cd libvpx \
	&& git remote add origin https://github.com/webmproject/libvpx.git \
	&& git fetch --depth=1 origin 12f3a2ac603e8f10742105519e0cd03c3b8f71dd \
	&& git reset --hard FETCH_HEAD \
	&& CFLAGS="$CFLAGS -fno-lto" CXXFLAGS="$CXXFLAGS -fno-lto" ./configure \
		--disable-examples \
		--disable-unit-tests \
		--disable-tools \
		--disable-docs \
		--enable-vp8 \
		--enable-vp9 \
		--enable-webm-io \
		--size-limit=4096x4096 \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/libvpx-cache" install \
	&& cd .. \
	&& rm -rf libvpx

FROM builder AS libwebp
RUN git clone -b chrome-m116-5845 --depth=1 https://github.com/webmproject/libwebp.git \
	&& cd libwebp \
	&& cmake -B build . \
		-DWEBP_BUILD_ANIM_UTILS=OFF \
		-DWEBP_BUILD_CWEBP=OFF \
		-DWEBP_BUILD_DWEBP=OFF \
		-DWEBP_BUILD_GIF2WEBP=OFF \
		-DWEBP_BUILD_IMG2WEBP=OFF \
		-DWEBP_BUILD_VWEBP=OFF \
		-DWEBP_BUILD_WEBPMUX=OFF \
		-DWEBP_BUILD_WEBPINFO=OFF \
		-DWEBP_BUILD_EXTRAS=OFF \
	&& cmake --build build \
	&& DESTDIR="{{ LibrariesPath }}/libwebp-cache" cmake --install build \
	&& cd .. \
	&& rm -rf libwebp

FROM builder AS libavif
COPY --link --from=dav1d {{ LibrariesPath }}/dav1d-cache /

RUN git clone -b v1.0.4 --depth=1 https://github.com/AOMediaCodec/libavif.git \
	&& cd libavif \
	&& cmake -B build . \
		-DBUILD_SHARED_LIBS=OFF \
		-DAVIF_CODEC_DAV1D=ON \
	&& cmake --build build \
	&& DESTDIR="{{ LibrariesPath }}/libavif-cache" cmake --install build \
	&& cd .. \
	&& rm -rf libavif

FROM builder AS libheif
COPY --link --from=libde265 {{ LibrariesPath }}/libde265-cache /

RUN git clone -b v1.18.2 --depth=1 https://github.com/strukturag/libheif.git \
	&& cd libheif \
	&& cmake -B build . \
		-DBUILD_SHARED_LIBS=OFF \
		-DBUILD_TESTING=OFF \
		-DENABLE_PLUGIN_LOADING=OFF \
		-DWITH_X265=OFF \
		-DWITH_AOM_DECODER=OFF \
		-DWITH_AOM_ENCODER=OFF \
		-DWITH_RAV1E=OFF \
		-DWITH_RAV1E_PLUGIN=OFF \
		-DWITH_SvtEnc=OFF \
		-DWITH_SvtEnc_PLUGIN=OFF \
		-DWITH_DAV1D=OFF \
		-DWITH_EXAMPLES=OFF \
	&& cmake --build build \
	&& DESTDIR="{{ LibrariesPath }}/libheif-cache" cmake --install build \
	&& cd .. \
	&& rm -rf libheif

FROM builder AS libjxl
COPY --link --from=lcms2 {{ LibrariesPath }}/lcms2-cache /
COPY --link --from=brotli {{ LibrariesPath }}/brotli-cache /
COPY --link --from=highway {{ LibrariesPath }}/highway-cache /

RUN git clone -b v0.11.1 --depth=1 https://github.com/libjxl/libjxl.git \
	&& cd libjxl \
	&& git submodule update --init --recursive --depth=1 third_party/libjpeg-turbo \
	&& cmake -B build . \
		-DBUILD_SHARED_LIBS=OFF \
		-DBUILD_TESTING=OFF \
		-DJPEGXL_ENABLE_DEVTOOLS=OFF \
		-DJPEGXL_ENABLE_TOOLS=OFF \
		-DJPEGXL_INSTALL_JPEGLI_LIBJPEG=ON \
		-DJPEGXL_ENABLE_DOXYGEN=OFF \
		-DJPEGXL_ENABLE_MANPAGES=OFF \
		-DJPEGXL_ENABLE_BENCHMARK=OFF \
		-DJPEGXL_ENABLE_EXAMPLES=OFF \
		-DJPEGXL_ENABLE_JNI=OFF \
		-DJPEGXL_ENABLE_SJPEG=OFF \
		-DJPEGXL_ENABLE_OPENEXR=OFF \
		-DJPEGXL_ENABLE_SKCMS=OFF \
	&& cmake --build build \
	&& export DESTDIR="{{ LibrariesPath }}/libjxl-cache" \
	&& cmake --install build \
	&& cp build/lib/libjpegli-static.a $DESTDIR/usr/local/lib64/libjpeg.a \
	&& ar rcs $DESTDIR/usr/local/lib64/libjpeg.a build/lib/CMakeFiles/jpegli-libjpeg-obj.dir/jpegli/libjpeg_wrapper.cc.o \
	&& cd .. \
	&& rm -rf libjxl

FROM builder AS rnnoise
RUN git clone -b master --depth=1 https://github.com/desktop-app/rnnoise.git \
	&& cd rnnoise \
	&& cmake -B build . \
	&& cmake --build build \
	&& DESTDIR="{{ LibrariesPath }}/rnnoise-cache" cmake --install build \
	&& cd .. \
	&& rm -rf rnnoise

FROM builder AS xcb-proto
RUN git clone -b xcb-proto-1.16.0 --depth=1 https://github.com/gitlab-freedesktop-mirrors/xcbproto.git \
	&& cd xcbproto \
	&& ./autogen.sh \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/xcb-proto-cache" install \
	&& cd .. \
	&& rm -rf xcbproto

FROM builder AS xcb
COPY --link --from=xcb-proto {{ LibrariesPath }}/xcb-proto-cache /

RUN git clone -b libxcb-1.16 --depth=1 https://github.com/gitlab-freedesktop-mirrors/libxcb.git \
	&& cd libxcb \
	&& ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/xcb-cache" install \
	&& cd .. \
	&& rm -rf libxcb

FROM builder AS xcb-wm
RUN git clone -b xcb-util-wm-0.4.2 --depth=1 --recursive --shallow-submodules https://github.com/gitlab-freedesktop-mirrors/libxcb-wm.git \
	&& cd libxcb-wm \
	&& ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/xcb-wm-cache" install \
	&& cd .. \
	&& rm -rf libxcb-wm

FROM builder AS xcb-util
RUN git clone -b xcb-util-0.4.1 --depth=1 --recursive --shallow-submodules https://github.com/gitlab-freedesktop-mirrors/libxcb-util.git \
	&& cd libxcb-util \
	&& ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/xcb-util-cache" install \
	&& cd .. \
	&& rm -rf libxcb-util

FROM builder AS xcb-image
COPY --link --from=xcb-util {{ LibrariesPath }}/xcb-util-cache /

RUN git clone -b xcb-util-image-0.4.1 --depth=1 --recursive --shallow-submodules https://github.com/gitlab-freedesktop-mirrors/libxcb-image.git \
	&& cd libxcb-image \
	&& ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/xcb-image-cache" install \
	&& cd .. \
	&& rm -rf libxcb-image

FROM builder AS xcb-keysyms
RUN git clone -b xcb-util-keysyms-0.4.1 --depth=1 --recursive --shallow-submodules https://github.com/gitlab-freedesktop-mirrors/libxcb-keysyms.git \
	&& cd libxcb-keysyms \
	&& ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/xcb-keysyms-cache" install \
	&& cd .. \
	&& rm -rf libxcb-keysyms

FROM builder AS xcb-render-util
RUN git clone -b xcb-util-renderutil-0.3.10 --depth=1 --recursive --shallow-submodules https://github.com/gitlab-freedesktop-mirrors/libxcb-render-util.git \
	&& cd libxcb-render-util \
	&& ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/xcb-render-util-cache" install \
	&& cd .. \
	&& rm -rf libxcb-render-util

FROM builder AS xcb-cursor
COPY --link --from=xcb-util {{ LibrariesPath }}/xcb-util-cache /
COPY --link --from=xcb-image {{ LibrariesPath }}/xcb-image-cache /
COPY --link --from=xcb-render-util {{ LibrariesPath }}/xcb-render-util-cache /

RUN git clone -b xcb-util-cursor-0.1.4 --depth=1 --recursive --shallow-submodules https://github.com/gitlab-freedesktop-mirrors/libxcb-cursor.git \
	&& cd libxcb-cursor \
	&& ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/xcb-cursor-cache" install \
	&& cd .. \
	&& rm -rf libxcb-cursor

FROM builder AS libXext
RUN git clone -b libXext-1.3.5 --depth=1 https://github.com/gitlab-freedesktop-mirrors/libxext.git \
	&& cd libxext \
	&& ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/libXext-cache" install \
	&& cd .. \
	&& rm -rf libxext

FROM builder AS libXtst
RUN git clone -b libXtst-1.2.4 --depth=1 https://github.com/gitlab-freedesktop-mirrors/libxtst.git \
	&& cd libxtst \
	&& ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/libXtst-cache" install \
	&& cd .. \
	&& rm -rf libxtst

FROM builder AS libXfixes
RUN git clone -b libXfixes-5.0.3 --depth=1 https://github.com/gitlab-freedesktop-mirrors/libxfixes.git \
	&& cd libxfixes \
	&& ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/libXfixes-cache" install \
	&& cd .. \
	&& rm -rf libxfixes

FROM builder AS libXv
COPY --link --from=libXext {{ LibrariesPath }}/libXext-cache /

RUN git clone -b libXv-1.0.12 --depth=1 https://github.com/gitlab-freedesktop-mirrors/libxv.git \
	&& cd libxv \
	&& ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/libXv-cache" install \
	&& cd .. \
	&& rm -rf libxv

FROM builder AS libXrandr
RUN git clone -b libXrandr-1.5.3 --depth=1 https://github.com/gitlab-freedesktop-mirrors/libxrandr.git \
	&& cd libxrandr \
	&& ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/libXrandr-cache" install \
	&& cd .. \
	&& rm -rf libxrandr

FROM builder AS libXrender
RUN git clone -b libXrender-0.9.11 --depth=1 https://github.com/gitlab-freedesktop-mirrors/libxrender.git \
	&& cd libxrender \
	&& ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/libXrender-cache" install \
	&& cd .. \
	&& rm -rf libxrender

FROM builder AS libXdamage
RUN git clone -b libXdamage-1.1.6 --depth=1 https://github.com/gitlab-freedesktop-mirrors/libxdamage.git \
	&& cd libxdamage \
	&& ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/libXdamage-cache" install \
	&& cd .. \
	&& rm -rf libxdamage

FROM builder AS libXcomposite
RUN git clone -b libXcomposite-0.4.6 --depth=1 https://github.com/gitlab-freedesktop-mirrors/libxcomposite.git \
	&& cd libxcomposite \
	&& ./autogen.sh --enable-static \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/libXcomposite-cache" install \
	&& cd .. \
	&& rm -rf libxcomposite

FROM builder AS wayland
RUN git clone -b 1.19.0 --depth=1 https://github.com/gitlab-freedesktop-mirrors/wayland.git \
	&& cd wayland \
	&& sed -i "/subdir('tests')/d" meson.build \
	&& meson build \
		--buildtype=plain \
		--default-library=both \
		-Ddocumentation=false \
		-Ddtd_validation=false \
		-Dicon_directory=/usr/share/icons \
	&& meson compile -C build src/wayland-scanner \
	&& mkdir -p "{{ LibrariesPath }}/wayland-cache/usr/local/bin" "{{ LibrariesPath }}/wayland-cache/usr/local/lib64/pkgconfig" \
	&& cp build/src/wayland-scanner "{{ LibrariesPath }}/wayland-cache/usr/local/bin" \
	&& sed 's@bindir=${prefix}/bin@bindir=${prefix}/local/bin@;s/1.21.0/1.19.0/' /usr/lib64/pkgconfig/wayland-scanner.pc > "{{ LibrariesPath }}/wayland-cache/usr/local/lib64/pkgconfig/wayland-scanner.pc" \
	&& cd .. \
	&& rm -rf wayland

FROM builder AS nv-codec-headers
RUN git clone -b n12.1.14.0 --depth=1 https://github.com/FFmpeg/nv-codec-headers.git \
	&& DESTDIR="{{ LibrariesPath }}/nv-codec-headers-cache" make -C nv-codec-headers install \
	&& rm -rf nv-codec-headers

FROM builder AS ffmpeg
COPY --link --from=opus {{ LibrariesPath }}/opus-cache /
COPY --link --from=openh264 {{ LibrariesPath }}/openh264-cache /
COPY --link --from=dav1d {{ LibrariesPath }}/dav1d-cache /
COPY --link --from=libvpx {{ LibrariesPath }}/libvpx-cache /
COPY --link --from=libXext {{ LibrariesPath }}/libXext-cache /
COPY --link --from=libXv {{ LibrariesPath }}/libXv-cache /
COPY --link --from=nv-codec-headers {{ LibrariesPath }}/nv-codec-headers-cache /

RUN git clone -b n6.1.1 --depth=1 https://github.com/FFmpeg/FFmpeg.git \
	&& cd FFmpeg \
	&& ./configure \
		--extra-cflags="-fno-lto -DCONFIG_SAFE_BITSTREAM_READER=1" \
		--extra-cxxflags="-fno-lto -DCONFIG_SAFE_BITSTREAM_READER=1" \
		--pkg-config-flags=--static \
		--disable-debug \
		--disable-programs \
		--disable-doc \
		--disable-network \
		--disable-autodetect \
		--disable-everything \
		--enable-libdav1d \
		--enable-libopenh264 \
		--enable-libopus \
		--enable-libvpx \
		--enable-vaapi \
		--enable-vdpau \
		--enable-xlib \
		--enable-libdrm \
		--enable-ffnvcodec \
		--enable-nvdec \
		--enable-cuvid \
		--enable-protocol=file \
		--enable-hwaccel=av1_vaapi \
		--enable-hwaccel=av1_nvdec \
		--enable-hwaccel=h264_vaapi \
		--enable-hwaccel=h264_vdpau \
		--enable-hwaccel=h264_nvdec \
		--enable-hwaccel=hevc_vaapi \
		--enable-hwaccel=hevc_vdpau \
		--enable-hwaccel=hevc_nvdec \
		--enable-hwaccel=mpeg2_vaapi \
		--enable-hwaccel=mpeg2_vdpau \
		--enable-hwaccel=mpeg2_nvdec \
		--enable-hwaccel=mpeg4_vaapi \
		--enable-hwaccel=mpeg4_vdpau \
		--enable-hwaccel=mpeg4_nvdec \
		--enable-hwaccel=vp8_vaapi \
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
		--enable-decoder=libdav1d \
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
		--enable-encoder=aac \
		--enable-encoder=libopenh264 \
		--enable-encoder=libopus \
		--enable-encoder=pcm_s16le \
		--enable-filter=atempo \
		--enable-parser=aac \
		--enable-parser=aac_latm \
		--enable-parser=flac \
		--enable-parser=gif \
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
		--enable-muxer=mp4 \
		--enable-muxer=ogg \
		--enable-muxer=opus \
		--enable-muxer=wav \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/ffmpeg-cache" install \
	&& cd .. \
	&& rm -rf ffmpeg

FROM builder AS pipewire
RUN git clone -b 0.3.62 --depth=1 https://github.com/PipeWire/pipewire.git \
	&& cd pipewire \
	&& meson build \
		--buildtype=plain \
		-Dtests=disabled \
		-Dexamples=disabled \
		-Dsession-managers=media-session \
		-Dspa-plugins=disabled \
	&& meson compile -C build \
	&& DESTDIR="{{ LibrariesPath }}/pipewire-cache" meson install -C build \
	&& cd .. \
	&& rm -rf pipewire

FROM builder AS openal
COPY --link --from=pipewire {{ LibrariesPath }}/pipewire-cache /

RUN git clone -b 1.24.3 --depth=1 https://github.com/kcat/openal-soft.git \
	&& cd openal-soft \
	&& cmake -B build . \
		-DLIBTYPE:STRING=STATIC \
		-DALSOFT_EXAMPLES=OFF \
		-DALSOFT_UTILS=OFF \
		-DALSOFT_INSTALL_CONFIG=OFF \
	&& cmake --build build \
	&& DESTDIR="{{ LibrariesPath }}/openal-cache" cmake --install build \
	&& cd .. \
	&& rm -rf openal-soft

FROM builder AS openssl
RUN git clone -b openssl-3.2.1 --depth=1 https://github.com/openssl/openssl.git \
	&& cd openssl \
	&& ./config \
		--openssldir=/etc/ssl \
		no-tests \
		no-dso \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/openssl-cache" install_sw \
	&& cd .. \
	&& rm -rf openssl

FROM builder AS xkbcommon
COPY --link --from=xcb {{ LibrariesPath }}/xcb-cache /

RUN git clone -b xkbcommon-1.6.0 --depth=1 https://github.com/xkbcommon/libxkbcommon.git \
	&& cd libxkbcommon \
	&& meson build \
		--buildtype=plain \
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

FROM patches AS qt
COPY --link --from=zlib {{ LibrariesPath }}/zlib-cache /
COPY --link --from=lcms2 {{ LibrariesPath }}/lcms2-cache /
COPY --link --from=libwebp {{ LibrariesPath }}/libwebp-cache /
COPY --link --from=libjxl {{ LibrariesPath }}/libjxl-cache /
COPY --link --from=xcb {{ LibrariesPath }}/xcb-cache /
COPY --link --from=xcb-wm {{ LibrariesPath }}/xcb-wm-cache /
COPY --link --from=xcb-util {{ LibrariesPath }}/xcb-util-cache /
COPY --link --from=xcb-image {{ LibrariesPath }}/xcb-image-cache /
COPY --link --from=xcb-keysyms {{ LibrariesPath }}/xcb-keysyms-cache /
COPY --link --from=xcb-render-util {{ LibrariesPath }}/xcb-render-util-cache /
COPY --link --from=xcb-cursor {{ LibrariesPath }}/xcb-cursor-cache /
COPY --link --from=wayland {{ LibrariesPath }}/wayland-cache /
COPY --link --from=openssl {{ LibrariesPath }}/openssl-cache /
COPY --link --from=xkbcommon {{ LibrariesPath }}/xkbcommon-cache /

ENV QT 6.9.0
RUN git clone -b v$QT --depth=1 https://github.com/qt/qt5.git \
	&& cd qt5 \
	&& git submodule update --init --recursive --depth=1 qtbase qtdeclarative qtwayland qtimageformats qtsvg qtshadertools \
	&& cd qtbase \
	&& find ../../patches/qtbase_$QT -type f -print0 | sort -z | xargs -r0 git apply \
	&& cd ../qtwayland \
	&& find ../../patches/qtwayland_$QT -type f -print0 | sort -z | xargs -r0 git apply \
	&& cd .. \
	&& cmake -B build . \
		-DCMAKE_INSTALL_PREFIX=/usr/local \
		-DBUILD_SHARED_LIBS=OFF \
		-DQT_GENERATE_SBOM=OFF \
		-DINPUT_libpng=qt \
		-DINPUT_harfbuzz=qt \
		-DINPUT_pcre=qt \
		-DFEATURE_icu=OFF \
		-DFEATURE_xcb_sm=OFF \
		-DINPUT_dbus=runtime \
		-DINPUT_openssl=linked \
	&& cmake --build build \
	&& DESTDIR="{{ LibrariesPath }}/qt-cache" cmake --install build \
	&& cd .. \
	&& rm -rf qt5

FROM builder AS breakpad
RUN git clone -b v2024.02.16 --depth=1 https://chromium.googlesource.com/breakpad/breakpad.git \
	&& cd breakpad \
	&& git clone -b v2024.02.01 --depth=1 https://chromium.googlesource.com/linux-syscall-support.git src/third_party/lss \
	&& CFLAGS="$CFLAGS -fno-lto" CXXFLAGS="$CXXFLAGS -fno-lto" ./configure \
	&& make -j$(nproc) \
	&& make DESTDIR="{{ LibrariesPath }}/breakpad-cache" install \
	&& cd .. \
	&& rm -rf breakpad

FROM builder AS webrtc
COPY --link --from=opus {{ LibrariesPath }}/opus-cache /
COPY --link --from=openh264 {{ LibrariesPath }}/openh264-cache /
COPY --link --from=libvpx {{ LibrariesPath }}/libvpx-cache /
COPY --link --from=libjxl {{ LibrariesPath }}/libjxl-cache /
COPY --link --from=ffmpeg {{ LibrariesPath }}/ffmpeg-cache /
COPY --link --from=openssl {{ LibrariesPath }}/openssl-cache /
COPY --link --from=libXtst {{ LibrariesPath }}/libXtst-cache /
COPY --link --from=pipewire {{ LibrariesPath }}/pipewire-cache /

# Shallow clone on a specific commit.
RUN git init tg_owt \
	&& cd tg_owt \
	&& git remote add origin https://github.com/desktop-app/tg_owt.git \
	&& git fetch --depth=1 origin c4192e8e2e10ccb72704daa79fa108becfa57b01 \
	&& git reset --hard FETCH_HEAD \
	&& git submodule update --init --recursive --depth=1 \
	&& cmake -B build . -DTG_OWT_DLOPEN_PIPEWIRE=ON \
	&& cmake --build build \
	&& DESTDIR="{{ LibrariesPath }}/webrtc-cache" cmake --install build \
	&& cd .. \
	&& rm -rf tg_owt

FROM builder AS ada
RUN git clone -b v3.2.2 --depth=1 https://github.com/ada-url/ada.git \
	&& cd ada \
	&& cmake -B build . \
        -D ADA_TESTING=OFF \
        -D ADA_TOOLS=OFF \
        -D ADA_INCLUDE_URL_PATTERN=OFF \
	&& cmake --build build \
	&& DESTDIR="{{ LibrariesPath }}/ada-cache" cmake --install build \
	&& cd .. \
	&& rm -rf ada

FROM builder AS tde2e
COPY --link --from=zlib {{ LibrariesPath }}/zlib-cache /
COPY --link --from=openssl {{ LibrariesPath }}/openssl-cache /

# Shallow clone on a specific commit.
RUN git init tde2e \
	&& cd tde2e \
	&& git remote add origin https://github.com/tdlib/td.git \
	&& git fetch --depth=1 origin 51743dfd01dff6179e2d8f7095729caa4e2222e9 \
	&& git reset --hard FETCH_HEAD \
	&& cmake -B build . -DTD_E2E_ONLY=ON \
	&& cmake --build build \
	&& DESTDIR="{{ LibrariesPath }}/tde2e-cache" cmake --install build \
	&& cd .. \
	&& rm -rf tde2e

FROM builder
COPY --link --from=zlib {{ LibrariesPath }}/zlib-cache /
COPY --link --from=xz {{ LibrariesPath }}/xz-cache /
COPY --link --from=protobuf {{ LibrariesPath }}/protobuf-cache /
COPY --link --from=lcms2 {{ LibrariesPath }}/lcms2-cache /
COPY --link --from=brotli {{ LibrariesPath }}/brotli-cache /
COPY --link --from=highway {{ LibrariesPath }}/highway-cache /
COPY --link --from=opus {{ LibrariesPath }}/opus-cache /
COPY --link --from=dav1d {{ LibrariesPath }}/dav1d-cache /
COPY --link --from=openh264 {{ LibrariesPath }}/openh264-cache /
COPY --link --from=libde265 {{ LibrariesPath }}/libde265-cache /
COPY --link --from=libvpx {{ LibrariesPath }}/libvpx-cache /
COPY --link --from=libwebp {{ LibrariesPath }}/libwebp-cache /
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
COPY --link --from=xcb-cursor {{ LibrariesPath }}/xcb-cursor-cache /
COPY --link --from=libXext {{ LibrariesPath }}/libXext-cache /
COPY --link --from=libXfixes {{ LibrariesPath }}/libXfixes-cache /
COPY --link --from=libXv {{ LibrariesPath }}/libXv-cache /
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
COPY --link --from=qt {{ LibrariesPath }}/qt-cache /
COPY --link --from=breakpad {{ LibrariesPath }}/breakpad-cache /
COPY --link --from=webrtc {{ LibrariesPath }}/webrtc-cache /
COPY --link --from=ada {{ LibrariesPath }}/ada-cache /
COPY --link --from=tde2e {{ LibrariesPath }}/tde2e-cache /

COPY --link --from=patches {{ LibrariesPath }}/patches patches
RUN patch -p1 -d /usr/lib64/gobject-introspection -i $PWD/patches/gobject-introspection.patch && rm -rf patches

WORKDIR ../tdesktop
ENV BOOST_INCLUDEDIR /usr/include/boost1.78
ENV BOOST_LIBRARYDIR /usr/lib64/boost1.78

USER user
VOLUME [ "/usr/src/tdesktop" ]
CMD [ "/usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh" ]
