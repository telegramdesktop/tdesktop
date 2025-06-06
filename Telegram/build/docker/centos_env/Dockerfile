# syntax=docker/dockerfile:1

FROM rockylinux:8 AS builder
ENV LANG=C.UTF-8
ENV TOOLSET=gcc-toolset-14
ENV PATH=/opt/rh/$TOOLSET/root/usr/bin:$PATH
ENV LIBRARY_PATH=/opt/rh/$TOOLSET/root/usr/lib64:/opt/rh/$TOOLSET/root/usr/lib:/usr/local/lib64:/usr/local/lib:/lib64:/lib:/usr/lib64:/usr/lib
ENV LD_LIBRARY_PATH=$LIBRARY_PATH
ENV PKG_CONFIG_PATH=/opt/rh/$TOOLSET/root/usr/lib64/pkgconfig:/opt/rh/$TOOLSET/root/usr/lib/pkgconfig:/usr/local/lib64/pkgconfig:/usr/local/lib/pkgconfig:/usr/local/share/pkgconfig

RUN dnf -y install epel-release \
	&& dnf config-manager --set-enabled powertools \
	&& dnf -y install cmake autoconf automake libtool pkgconfig make patch git \
		python3.11-pip python3.11-devel gperf flex bison clang clang-tools-extra \
		lld nasm yasm file which wget perl-open perl-XML-Parser perl-IPC-Cmd \
		xorg-x11-util-macros $TOOLSET-gcc $TOOLSET-gcc-c++ $TOOLSET-binutils \
		$TOOLSET-gdb $TOOLSET-libasan-devel libffi-devel fontconfig-devel \
		freetype-devel libX11-devel wayland-devel alsa-lib-devel \
		pulseaudio-libs-devel mesa-libGL-devel mesa-libEGL-devel mesa-libgbm-devel \
		libdrm-devel vulkan-devel libva-devel libvdpau-devel libselinux-devel \
		libmount-devel systemd-devel glib2-devel gobject-introspection-devel \
		at-spi2-core-devel gtk3-devel boost1.78-devel \
	&& dnf clean all

RUN alternatives --set python3 /usr/bin/python3.11
RUN python3 -m pip install meson ninja
RUN cat <<EOF > /usr/local/bin/pkg-config && chmod +x /usr/local/bin/pkg-config
#!/bin/sh
for i in "\$@"; do
	[ "\$i" = "--version" ] && exec /usr/bin/pkg-config "\$i"
done
exec /usr/bin/pkg-config --static "\$@"
EOF
RUN sed -i '/CMAKE_${lang}_FLAGS_DEBUG_INIT/s/")/ -O0 {% if LTO %}-fno-lto -fno-use-linker-plugin -fuse-ld=lld{% endif %}")/' /usr/share/cmake/Modules/Compiler/GNU.cmake
RUN sed -i 's/NO_DEFAULT_PATH//g; s/PKG_CONFIG_ALLOW_SYSTEM_LIBS/PKG_CONFIG_IS_DUMB/g' /usr/share/cmake/Modules/FindPkgConfig.cmake
RUN sed -i 's/set(OpenGL_GL_PREFERENCE "")/set(OpenGL_GL_PREFERENCE "LEGACY")/' /usr/share/cmake/Modules/FindOpenGL.cmake
RUN sed -i '/Requires.private: valgrind/d' /usr/lib64/pkgconfig/libdrm.pc
RUN echo set debuginfod enabled on > /opt/rh/$TOOLSET/root/etc/gdbinit.d/00-debuginfod.gdb
RUN adduser user

WORKDIR /usr/src
ENV AR=gcc-ar
ENV RANLIB=gcc-ranlib
ENV NM=gcc-nm
ENV CFLAGS='{% if DEBUG %}-g{% endif %} -O3 {% if LTO %}-flto=auto -ffat-lto-objects{% endif %} -pipe -fPIC -fno-strict-aliasing -fexceptions -fasynchronous-unwind-tables -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -fhardened -Wno-hardened'
ENV CXXFLAGS=$CFLAGS
ENV LDFLAGS='{% if not LTO %}-fuse-ld=lld{% endif %} -static-libstdc++ -static-libgcc -static-libasan -pthread -ldl -Wl,--as-needed -Wl,-z,muldefs'

ENV CMAKE_GENERATOR=Ninja
ENV CMAKE_BUILD_TYPE=None
ENV CMAKE_BUILD_PARALLEL_LEVEL=

RUN git init Implib.so \
	&& cd Implib.so \
	&& git remote add origin https://github.com/yugr/Implib.so.git \
	&& git fetch --depth=1 origin ecf7bb51a92a0fb16834c5b698570ab25f9f1d21 \
	&& git reset --hard FETCH_HEAD \
	&& mkdir build \
	&& cd build \
	&& implib() { \
		LIBFILE=$(basename $1); \
		LIBNAME=$(basename $1 .so); \
		../implib-gen.py -q $1; \
		gcc $CFLAGS -c -o $LIBFILE.tramp.o $LIBFILE.tramp.S; \
		gcc $CFLAGS -c -o $LIBFILE.init.o $LIBFILE.init.c; \
		ar rcs /usr/local/lib64/$LIBNAME.a $LIBFILE.tramp.o $LIBFILE.init.o; \
	} \
	&& implib /usr/lib64/libgtk-3.so \
	&& implib /usr/lib64/libgdk-3.so \
	&& implib /usr/lib64/libgdk_pixbuf-2.0.so \
	&& implib /usr/lib64/libpango-1.0.so \
	&& implib /usr/lib64/libvdpau.so \
	&& implib /usr/lib64/libva-x11.so \
	&& implib /usr/lib64/libva-drm.so \
	&& implib /usr/lib64/libva.so \
	&& implib /usr/lib64/libEGL.so \
	&& implib /usr/lib64/libGL.so \
	&& implib /usr/lib64/libdrm.so \
	&& implib /usr/lib64/libwayland-egl.so \
	&& implib /usr/lib64/libwayland-cursor.so \
	&& implib /usr/lib64/libwayland-client.so \
	&& implib /usr/lib64/libwayland-server.so \
	&& implib /usr/lib64/libX11-xcb.so \
	&& implib /usr/lib64/libxcb.so \
	&& cd ../.. \
	&& rm -rf Implib.so

FROM builder AS patches
RUN git init patches \
	&& cd patches \
	&& git remote add origin https://github.com/desktop-app/patches.git \
	&& git fetch --depth=1 origin a405719f0963abf7cb93354a390617c0f0d90f17 \
	&& git reset --hard FETCH_HEAD \
	&& rm -rf .git

FROM builder AS zlib
RUN git clone -b v1.3.1 --depth=1 https://github.com/madler/zlib.git \
	&& cd zlib \
	&& cmake -B build . -DZLIB_BUILD_EXAMPLES=OFF \
	&& cmake --build build \
	&& export DESTDIR=/usr/src/zlib-cache \
	&& cmake --install build \
	&& rm $DESTDIR/usr/local/lib/libz.so* \
	&& cd .. \
	&& rm -rf zlib

FROM builder AS xz
RUN git clone -b v5.8.1 --depth=1 https://github.com/tukaani-project/xz.git \
	&& cd xz \
	&& cmake -B build . \
	&& cmake --build build \
	&& DESTDIR=/usr/src/xz-cache cmake --install build \
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
	&& DESTDIR=/usr/src/protobuf-cache cmake --install build \
	&& cd .. \
	&& rm -rf protobuf

FROM builder AS lcms2
RUN git clone -b lcms2.15 --depth=1 https://github.com/mm2/Little-CMS.git \
	&& cd Little-CMS \
	&& meson build \
		--buildtype=plain \
		--default-library=static \
	&& meson compile -C build \
	&& DESTDIR=/usr/src/lcms2-cache meson install -C build \
	&& cd .. \
	&& rm -rf Little-CMS

FROM builder AS brotli
RUN git clone -b v1.1.0 --depth=1 https://github.com/google/brotli.git \
	&& cd brotli \
	&& cmake -B build . \
		-DBUILD_SHARED_LIBS=OFF \
		-DBROTLI_DISABLE_TESTS=ON \
	&& cmake --build build \
	&& DESTDIR=/usr/src/brotli-cache cmake --install build \
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
	&& DESTDIR=/usr/src/highway-cache cmake --install build \
	&& cd .. \
	&& rm -rf highway

FROM builder AS opus
RUN git clone -b v1.5.2 --depth=1 https://github.com/xiph/opus.git \
	&& cd opus \
	&& cmake -B build . \
	&& cmake --build build \
	&& DESTDIR=/usr/src/opus-cache cmake --install build \
	&& cd .. \
	&& rm -rf opus

FROM builder AS dav1d
RUN git clone -b 1.4.1 --depth=1 https://github.com/videolan/dav1d.git \
	&& cd dav1d \
	&& meson build \
		--buildtype=plain \
		--default-library=static \
		-Denable_tools=false \
		-Denable_tests=false \
	&& meson compile -C build \
	&& DESTDIR=/usr/src/dav1d-cache meson install -C build \
	&& cd .. \
	&& rm -rf dav1d

FROM builder AS openh264
RUN git clone -b v2.4.1 --depth=1 https://github.com/cisco/openh264.git \
	&& cd openh264 \
	&& meson build \
		--buildtype=plain \
		--default-library=static \
	&& meson compile -C build \
	&& DESTDIR=/usr/src/openh264-cache meson install -C build \
	&& cd .. \
	&& rm -rf openh264

FROM builder AS de265
RUN git clone -b v1.0.15 --depth=1 https://github.com/strukturag/libde265.git \
	&& cd libde265 \
	&& cmake -B build . \
		-DCMAKE_BUILD_TYPE=None \
		-DBUILD_SHARED_LIBS=OFF \
		-DENABLE_DECODER=OFF \
		-DENABLE_SDL=OFF \
	&& cmake --build build \
	&& DESTDIR=/usr/src/de265-cache cmake --install build \
	&& cd .. \
	&& rm -rf libde265

FROM builder AS vpx
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
	&& make DESTDIR=/usr/src/vpx-cache install \
	&& cd .. \
	&& rm -rf libvpx

FROM builder AS webp
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
	&& DESTDIR=/usr/src/webp-cache cmake --install build \
	&& cd .. \
	&& rm -rf libwebp

FROM builder AS avif
COPY --link --from=dav1d /usr/src/dav1d-cache /

RUN git clone -b v1.0.4 --depth=1 https://github.com/AOMediaCodec/libavif.git \
	&& cd libavif \
	&& sed -i 's/BUILD_SHARED_LIBS OR VCPKG_TARGET_TRIPLET/TRUE/' CMakeLists.txt \
	&& cmake -B build . \
		-DBUILD_SHARED_LIBS=OFF \
		-DAVIF_CODEC_DAV1D=ON \
	&& cmake --build build \
	&& DESTDIR=/usr/src/avif-cache cmake --install build \
	&& cd .. \
	&& rm -rf libavif

FROM builder AS heif
COPY --link --from=de265 /usr/src/de265-cache /

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
	&& DESTDIR=/usr/src/heif-cache cmake --install build \
	&& cd .. \
	&& rm -rf libheif

FROM builder AS jxl
COPY --link --from=lcms2 /usr/src/lcms2-cache /
COPY --link --from=brotli /usr/src/brotli-cache /
COPY --link --from=highway /usr/src/highway-cache /

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
	&& export DESTDIR=/usr/src/jxl-cache \
	&& cmake --install build \
	&& sed -i 's/-lstdc++//' $DESTDIR/usr/local/lib64/pkgconfig/libjxl*.pc \
	&& rm $DESTDIR/usr/local/lib64/libjpeg.so* \
	&& cp build/lib/libjpegli-static.a $DESTDIR/usr/local/lib64/libjpeg.a \
	&& mkdir build/hwy \
	&& ar --output=build/hwy x /usr/local/lib64/libhwy.a \
	&& ar rcs $DESTDIR/usr/local/lib64/libjpeg.a build/lib/CMakeFiles/jpegli-libjpeg-obj.dir/jpegli/libjpeg_wrapper.cc.o build/hwy/* \
	&& cd .. \
	&& rm -rf libjxl

FROM builder AS rnnoise
RUN git clone -b v0.2 --depth=1 https://github.com/xiph/rnnoise.git \
	&& cd rnnoise \
	&& ./autogen.sh \
	&& ./configure --enable-static --disable-shared \
	&& make -j$(nproc) \
	&& make DESTDIR=/usr/src/rnnoise-cache install \
	&& cd .. \
	&& rm -rf rnnoise

FROM builder AS xcb-proto
RUN git clone -b xcb-proto-1.16.0 --depth=1 https://github.com/gitlab-freedesktop-mirrors/xcbproto.git \
	&& cd xcbproto \
	&& ./autogen.sh \
	&& make -j$(nproc) \
	&& make DESTDIR=/usr/src/xcb-proto-cache install \
	&& cd .. \
	&& rm -rf xcbproto

FROM builder AS xcb
COPY --link --from=xcb-proto /usr/src/xcb-proto-cache /

RUN git clone -b libxcb-1.16 --depth=1 https://github.com/gitlab-freedesktop-mirrors/libxcb.git \
	&& cd libxcb \
	&& ./autogen.sh --enable-static --disable-shared \
	&& make -j$(nproc) \
	&& export DESTDIR=/usr/src/xcb-cache \
	&& make install \
	&& rm $DESTDIR/usr/local/lib/{libxcb.{,l}a,pkgconfig/xcb.pc} \
	&& cd .. \
	&& rm -rf libxcb

FROM builder AS xcb-wm
RUN git clone -b xcb-util-wm-0.4.2 --depth=1 --recursive --shallow-submodules https://github.com/gitlab-freedesktop-mirrors/libxcb-wm.git \
	&& cd libxcb-wm \
	&& ./autogen.sh --enable-static --disable-shared \
	&& make -j$(nproc) \
	&& make DESTDIR=/usr/src/xcb-wm-cache install \
	&& cd .. \
	&& rm -rf libxcb-wm

FROM builder AS xcb-util
RUN git clone -b xcb-util-0.4.1-gitlab --depth=1 --recursive --shallow-submodules https://github.com/gitlab-freedesktop-mirrors/libxcb-util.git \
	&& cd libxcb-util \
	&& ./autogen.sh --enable-static --disable-shared \
	&& make -j$(nproc) \
	&& make DESTDIR=/usr/src/xcb-util-cache install \
	&& cd .. \
	&& rm -rf libxcb-util

FROM builder AS xcb-image
COPY --link --from=xcb-util /usr/src/xcb-util-cache /

RUN git clone -b xcb-util-image-0.4.1-gitlab --depth=1 --recursive --shallow-submodules https://github.com/gitlab-freedesktop-mirrors/libxcb-image.git \
	&& cd libxcb-image \
	&& ./autogen.sh --enable-static --disable-shared \
	&& make -j$(nproc) \
	&& make DESTDIR=/usr/src/xcb-image-cache install \
	&& cd .. \
	&& rm -rf libxcb-image

FROM builder AS xcb-keysyms
RUN git init libxcb-keysyms \
	&& cd libxcb-keysyms \
	&& git remote add origin https://github.com/gitlab-freedesktop-mirrors/libxcb-keysyms.git \
	&& git fetch --depth=1 origin ef5cb393d27511ba511c68a54f8ff7b9aab4a384 \
	&& git reset --hard FETCH_HEAD \
	&& git submodule update --init --recursive --depth=1 \
	&& ./autogen.sh --enable-static --disable-shared \
	&& make -j$(nproc) \
	&& make DESTDIR=/usr/src/xcb-keysyms-cache install \
	&& cd .. \
	&& rm -rf libxcb-keysyms

FROM builder AS xcb-render-util
RUN git init libxcb-render-util \
	&& cd libxcb-render-util \
	&& git remote add origin https://github.com/gitlab-freedesktop-mirrors/libxcb-render-util.git \
	&& git fetch --depth=1 origin 5ad9853d6ddcac394d42dd2d4e34436b5db9da39 \
	&& git reset --hard FETCH_HEAD \
	&& git submodule update --init --recursive --depth=1 \
	&& ./autogen.sh --enable-static --disable-shared \
	&& make -j$(nproc) \
	&& make DESTDIR=/usr/src/xcb-render-util-cache install \
	&& cd .. \
	&& rm -rf libxcb-render-util

FROM builder AS xcb-cursor
COPY --link --from=xcb-util /usr/src/xcb-util-cache /
COPY --link --from=xcb-image /usr/src/xcb-image-cache /
COPY --link --from=xcb-render-util /usr/src/xcb-render-util-cache /

RUN git init libxcb-cursor \
	&& cd libxcb-cursor \
	&& git remote add origin https://github.com/gitlab-freedesktop-mirrors/libxcb-cursor.git \
	&& git fetch --depth=1 origin 4929f6051658ba5424b41703a1fb63f9db896065 \
	&& git reset --hard FETCH_HEAD \
	&& git submodule update --init --recursive --depth=1 \
	&& ./autogen.sh --enable-static --disable-shared \
	&& make -j$(nproc) \
	&& make DESTDIR=/usr/src/xcb-cursor-cache install \
	&& cd .. \
	&& rm -rf libxcb-cursor

FROM builder AS xext
RUN git clone -b libXext-1.3.5 --depth=1 https://github.com/gitlab-freedesktop-mirrors/libxext.git \
	&& cd libxext \
	&& ./autogen.sh --enable-static --disable-shared \
	&& make -j$(nproc) \
	&& make DESTDIR=/usr/src/xext-cache install \
	&& cd .. \
	&& rm -rf libxext

FROM builder AS xtst
RUN git clone -b libXtst-1.2.4 --depth=1 https://github.com/gitlab-freedesktop-mirrors/libxtst.git \
	&& cd libxtst \
	&& ./autogen.sh --enable-static --disable-shared \
	&& make -j$(nproc) \
	&& make DESTDIR=/usr/src/xtst-cache install \
	&& cd .. \
	&& rm -rf libxtst

FROM builder AS xfixes
RUN git clone -b libXfixes-5.0.3 --depth=1 https://github.com/gitlab-freedesktop-mirrors/libxfixes.git \
	&& cd libxfixes \
	&& ./autogen.sh --enable-static --disable-shared \
	&& make -j$(nproc) \
	&& make DESTDIR=/usr/src/xfixes-cache install \
	&& cd .. \
	&& rm -rf libxfixes

FROM builder AS xv
COPY --link --from=xext /usr/src/xext-cache /

RUN git clone -b libXv-1.0.12 --depth=1 https://github.com/gitlab-freedesktop-mirrors/libxv.git \
	&& cd libxv \
	&& ./autogen.sh --enable-static --disable-shared \
	&& make -j$(nproc) \
	&& make DESTDIR=/usr/src/xv-cache install \
	&& cd .. \
	&& rm -rf libxv

FROM builder AS xrandr
RUN git clone -b libXrandr-1.5.3 --depth=1 https://github.com/gitlab-freedesktop-mirrors/libxrandr.git \
	&& cd libxrandr \
	&& ./autogen.sh --enable-static --disable-shared \
	&& make -j$(nproc) \
	&& make DESTDIR=/usr/src/xrandr-cache install \
	&& cd .. \
	&& rm -rf libxrandr

FROM builder AS xrender
RUN git clone -b libXrender-0.9.11 --depth=1 https://github.com/gitlab-freedesktop-mirrors/libxrender.git \
	&& cd libxrender \
	&& ./autogen.sh --enable-static --disable-shared \
	&& make -j$(nproc) \
	&& make DESTDIR=/usr/src/xrender-cache install \
	&& cd .. \
	&& rm -rf libxrender

FROM builder AS xdamage
RUN git clone -b libXdamage-1.1.6 --depth=1 https://github.com/gitlab-freedesktop-mirrors/libxdamage.git \
	&& cd libxdamage \
	&& ./autogen.sh --enable-static --disable-shared \
	&& make -j$(nproc) \
	&& make DESTDIR=/usr/src/xdamage-cache install \
	&& cd .. \
	&& rm -rf libxdamage

FROM builder AS xcomposite
RUN git clone -b libXcomposite-0.4.6 --depth=1 https://github.com/gitlab-freedesktop-mirrors/libxcomposite.git \
	&& cd libxcomposite \
	&& ./autogen.sh --enable-static --disable-shared \
	&& make -j$(nproc) \
	&& make DESTDIR=/usr/src/xcomposite-cache install \
	&& cd .. \
	&& rm -rf libxcomposite

FROM builder AS wayland
RUN git clone -b 1.19.0 --depth=1 https://github.com/gitlab-freedesktop-mirrors/wayland.git \
	&& cd wayland \
	&& sed -i "/subdir('tests')/d" meson.build \
	&& meson build \
		--buildtype=plain \
		--default-library=static \
		-Ddocumentation=false \
		-Ddtd_validation=false \
		-Dicon_directory=/usr/share/icons \
	&& meson compile -C build src/wayland-scanner \
	&& mkdir -p "/usr/src/wayland-cache/usr/local/bin" "/usr/src/wayland-cache/usr/local/lib64/pkgconfig" \
	&& cp build/src/wayland-scanner "/usr/src/wayland-cache/usr/local/bin" \
	&& sed 's@bindir=${prefix}/bin@bindir=${prefix}/local/bin@;s/1.21.0/1.19.0/' /usr/lib64/pkgconfig/wayland-scanner.pc > "/usr/src/wayland-cache/usr/local/lib64/pkgconfig/wayland-scanner.pc" \
	&& cd .. \
	&& rm -rf wayland

FROM builder AS nv-codec-headers
RUN git clone -b n12.1.14.0 --depth=1 https://github.com/FFmpeg/nv-codec-headers.git \
	&& DESTDIR=/usr/src/nv-codec-headers-cache make -C nv-codec-headers install \
	&& rm -rf nv-codec-headers

FROM builder AS ffmpeg
COPY --link --from=opus /usr/src/opus-cache /
COPY --link --from=openh264 /usr/src/openh264-cache /
COPY --link --from=dav1d /usr/src/dav1d-cache /
COPY --link --from=vpx /usr/src/vpx-cache /
COPY --link --from=xext /usr/src/xext-cache /
COPY --link --from=xv /usr/src/xv-cache /
COPY --link --from=nv-codec-headers /usr/src/nv-codec-headers-cache /

RUN git clone -b n6.1.1 --depth=1 https://github.com/FFmpeg/FFmpeg.git \
	&& cd FFmpeg \
	&& ./configure \
		--extra-cflags="-fno-lto -DCONFIG_SAFE_BITSTREAM_READER=1" \
		--extra-cxxflags="-fno-lto -DCONFIG_SAFE_BITSTREAM_READER=1" \
		--extra-ldflags="-lstdc++" \
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
	&& make DESTDIR=/usr/src/ffmpeg-cache install \
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
	&& DESTDIR=/usr/src/pipewire-cache meson install -C build \
	&& cd .. \
	&& rm -rf pipewire

FROM builder AS openal
COPY --link --from=pipewire /usr/src/pipewire-cache /

RUN git clone -b 1.24.3 --depth=1 https://github.com/kcat/openal-soft.git \
	&& cd openal-soft \
	&& cmake -B build . \
		-DLIBTYPE:STRING=STATIC \
		-DALSOFT_EXAMPLES=OFF \
		-DALSOFT_UTILS=OFF \
		-DALSOFT_INSTALL_CONFIG=OFF \
	&& cmake --build build \
	&& DESTDIR=/usr/src/openal-cache cmake --install build \
	&& cd .. \
	&& rm -rf openal-soft

FROM builder AS openssl
RUN git clone -b openssl-3.2.1 --depth=1 https://github.com/openssl/openssl.git \
	&& cd openssl \
	&& ./config \
		--openssldir=/etc/ssl \
		no-shared \
		no-tests \
		no-dso \
	&& make -j$(nproc) \
	&& make DESTDIR=/usr/src/openssl-cache install_sw \
	&& cd .. \
	&& rm -rf openssl

FROM builder AS xkbcommon
COPY --link --from=xcb /usr/src/xcb-cache /

RUN git clone -b xkbcommon-1.6.0 --depth=1 https://github.com/xkbcommon/libxkbcommon.git \
	&& cd libxkbcommon \
	&& meson build \
		--buildtype=plain \
		--default-library=static \
		-Denable-docs=false \
		-Denable-wayland=false \
		-Denable-xkbregistry=false \
		-Dxkb-config-root=/usr/share/X11/xkb \
		-Dxkb-config-extra-path=/etc/xkb \
		-Dx-locale-root=/usr/share/X11/locale \
	&& meson compile -C build \
	&& DESTDIR=/usr/src/xkbcommon-cache meson install -C build \
	&& cd .. \
	&& rm -rf libxkbcommon

FROM patches AS qt
COPY --link --from=zlib /usr/src/zlib-cache /
COPY --link --from=lcms2 /usr/src/lcms2-cache /
COPY --link --from=webp /usr/src/webp-cache /
COPY --link --from=jxl /usr/src/jxl-cache /
COPY --link --from=xcb /usr/src/xcb-cache /
COPY --link --from=xcb-wm /usr/src/xcb-wm-cache /
COPY --link --from=xcb-util /usr/src/xcb-util-cache /
COPY --link --from=xcb-image /usr/src/xcb-image-cache /
COPY --link --from=xcb-keysyms /usr/src/xcb-keysyms-cache /
COPY --link --from=xcb-render-util /usr/src/xcb-render-util-cache /
COPY --link --from=xcb-cursor /usr/src/xcb-cursor-cache /
COPY --link --from=wayland /usr/src/wayland-cache /
COPY --link --from=openssl /usr/src/openssl-cache /
COPY --link --from=xkbcommon /usr/src/xkbcommon-cache /

ENV QT=6.9.1
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
		-DQT_QPA_PLATFORMS="wayland;xcb" \
		-DINPUT_libpng=qt \
		-DINPUT_harfbuzz=qt \
		-DINPUT_pcre=qt \
		-DFEATURE_icu=OFF \
		-DFEATURE_xcb_sm=OFF \
		-DFEATURE_eglfs=OFF \
		-DINPUT_dbus=runtime \
		-DINPUT_openssl=linked \
	&& cmake --build build \
	&& DESTDIR=/usr/src/qt-cache cmake --install build \
	&& cd .. \
	&& rm -rf qt5

FROM builder AS breakpad
RUN git clone -b v2024.02.16 --depth=1 https://chromium.googlesource.com/breakpad/breakpad.git \
	&& cd breakpad \
	&& git clone -b v2024.02.01 --depth=1 https://chromium.googlesource.com/linux-syscall-support.git src/third_party/lss \
	&& CFLAGS="$CFLAGS -fno-lto" CXXFLAGS="$CXXFLAGS -fno-lto" ./configure \
	&& make -j$(nproc) \
	&& make DESTDIR=/usr/src/breakpad-cache install \
	&& cd .. \
	&& rm -rf breakpad

FROM builder AS webrtc
COPY --link --from=zlib /usr/src/zlib-cache /
COPY --link --from=opus /usr/src/opus-cache /
COPY --link --from=openh264 /usr/src/openh264-cache /
COPY --link --from=dav1d /usr/src/dav1d-cache /
COPY --link --from=vpx /usr/src/vpx-cache /
COPY --link --from=jxl /usr/src/jxl-cache /
COPY --link --from=ffmpeg /usr/src/ffmpeg-cache /
COPY --link --from=openssl /usr/src/openssl-cache /
COPY --link --from=xext /usr/src/xext-cache /
COPY --link --from=xfixes /usr/src/xfixes-cache /
COPY --link --from=xtst /usr/src/xtst-cache /
COPY --link --from=xrandr /usr/src/xrandr-cache /
COPY --link --from=xrender /usr/src/xrender-cache /
COPY --link --from=xdamage /usr/src/xdamage-cache /
COPY --link --from=xcomposite /usr/src/xcomposite-cache /
COPY --link --from=pipewire /usr/src/pipewire-cache /

# Shallow clone on a specific commit.
RUN git init tg_owt \
	&& cd tg_owt \
	&& git remote add origin https://github.com/desktop-app/tg_owt.git \
	&& git fetch --depth=1 origin 62321fd7128ab2650b459d4195781af8185e46b5 \
	&& git reset --hard FETCH_HEAD \
	&& git submodule update --init --recursive --depth=1 \
	&& cmake -B build . -DTG_OWT_DLOPEN_PIPEWIRE=ON \
	&& cmake --build build \
	&& DESTDIR=/usr/src/webrtc-cache cmake --install build \
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
	&& DESTDIR=/usr/src/ada-cache cmake --install build \
	&& cd .. \
	&& rm -rf ada

FROM builder AS tde2e
COPY --link --from=zlib /usr/src/zlib-cache /
COPY --link --from=openssl /usr/src/openssl-cache /

# Shallow clone on a specific commit.
RUN git init tde2e \
	&& cd tde2e \
	&& git remote add origin https://github.com/tdlib/td.git \
	&& git fetch --depth=1 origin 51743dfd01dff6179e2d8f7095729caa4e2222e9 \
	&& git reset --hard FETCH_HEAD \
	&& cmake -B build . -DTD_E2E_ONLY=ON \
	&& cmake --build build \
	&& DESTDIR=/usr/src/tde2e-cache cmake --install build \
	&& cd .. \
	&& rm -rf tde2e

FROM builder
COPY --link --from=zlib /usr/src/zlib-cache /
COPY --link --from=xz /usr/src/xz-cache /
COPY --link --from=protobuf /usr/src/protobuf-cache /
COPY --link --from=lcms2 /usr/src/lcms2-cache /
COPY --link --from=brotli /usr/src/brotli-cache /
COPY --link --from=highway /usr/src/highway-cache /
COPY --link --from=opus /usr/src/opus-cache /
COPY --link --from=dav1d /usr/src/dav1d-cache /
COPY --link --from=openh264 /usr/src/openh264-cache /
COPY --link --from=de265 /usr/src/de265-cache /
COPY --link --from=vpx /usr/src/vpx-cache /
COPY --link --from=webp /usr/src/webp-cache /
COPY --link --from=avif /usr/src/avif-cache /
COPY --link --from=heif /usr/src/heif-cache /
COPY --link --from=jxl /usr/src/jxl-cache /
COPY --link --from=rnnoise /usr/src/rnnoise-cache /
COPY --link --from=xcb /usr/src/xcb-cache /
COPY --link --from=xcb-wm /usr/src/xcb-wm-cache /
COPY --link --from=xcb-util /usr/src/xcb-util-cache /
COPY --link --from=xcb-image /usr/src/xcb-image-cache /
COPY --link --from=xcb-keysyms /usr/src/xcb-keysyms-cache /
COPY --link --from=xcb-render-util /usr/src/xcb-render-util-cache /
COPY --link --from=xcb-cursor /usr/src/xcb-cursor-cache /
COPY --link --from=xext /usr/src/xext-cache /
COPY --link --from=xfixes /usr/src/xfixes-cache /
COPY --link --from=xv /usr/src/xv-cache /
COPY --link --from=xtst /usr/src/xtst-cache /
COPY --link --from=xrandr /usr/src/xrandr-cache /
COPY --link --from=xrender /usr/src/xrender-cache /
COPY --link --from=xdamage /usr/src/xdamage-cache /
COPY --link --from=xcomposite /usr/src/xcomposite-cache /
COPY --link --from=wayland /usr/src/wayland-cache /
COPY --link --from=ffmpeg /usr/src/ffmpeg-cache /
COPY --link --from=openal /usr/src/openal-cache /
COPY --link --from=openssl /usr/src/openssl-cache /
COPY --link --from=xkbcommon /usr/src/xkbcommon-cache /
COPY --link --from=qt /usr/src/qt-cache /
COPY --link --from=breakpad /usr/src/breakpad-cache /
COPY --link --from=webrtc /usr/src/webrtc-cache /
COPY --link --from=ada /usr/src/ada-cache /
COPY --link --from=tde2e /usr/src/tde2e-cache /

COPY --link --from=patches /usr/src/patches patches
RUN patch -p1 -d /usr/lib64/gobject-introspection -i $PWD/patches/gobject-introspection.patch && rm -rf patches

WORKDIR /usr/src/tdesktop
ENV BOOST_INCLUDEDIR=/usr/include/boost1.78
ENV BOOST_LIBRARYDIR=/usr/lib64/boost1.78

USER user
VOLUME [ "/usr/src/tdesktop" ]
CMD [ "/usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh" ]
