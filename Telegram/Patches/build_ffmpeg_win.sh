set -e
FullExecPath=$PWD
pushd `dirname $0` > /dev/null
FullScriptPath=`pwd`
popd > /dev/null

pacman --noconfirm -Sy
pacman --noconfirm -S msys/make
pacman --noconfirm -S mingw64/mingw-w64-x86_64-opus
pacman --noconfirm -S diffutils
pacman --noconfirm -S pkg-config

PKG_CONFIG_PATH="/mingw64/lib/pkgconfig:$PKG_CONFIG_PATH"

./configure --toolchain=msvc \
--extra-cflags="-DCONFIG_SAFE_BITSTREAM_READER=1" \
--extra-cxxflags="-DCONFIG_SAFE_BITSTREAM_READER=1" \
--extra-ldflags="-libpath:$FullExecPath/../opus/win32/VS2015/Win32/Release" \
--disable-programs \
--disable-doc \
--disable-network \
--disable-everything \
--enable-hwaccel=h264_d3d11va \
--enable-hwaccel=h264_d3d11va2 \
--enable-hwaccel=h264_dxva2 \
--enable-hwaccel=hevc_d3d11va \
--enable-hwaccel=hevc_d3d11va2 \
--enable-hwaccel=hevc_dxva2 \
--enable-hwaccel=mpeg2_d3d11va \
--enable-hwaccel=mpeg2_d3d11va2 \
--enable-hwaccel=mpeg2_dxva2 \
--enable-protocol=file --enable-libopus \
--enable-decoder=aac \
--enable-decoder=aac_at \
--enable-decoder=aac_fixed \
--enable-decoder=aac_latm \
--enable-decoder=aasc \
--enable-decoder=alac \
--enable-decoder=alac_at \
--enable-decoder=flac \
--enable-decoder=gif \
--enable-decoder=h264 \
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
--enable-decoder=msmpeg4v2 \
--enable-decoder=msmpeg4v3 \
--enable-decoder=opus \
--enable-decoder=pcm_alaw \
--enable-decoder=pcm_alaw_at \
--enable-decoder=pcm_f32be \
--enable-decoder=pcm_f32le \
--enable-decoder=pcm_f64be \
--enable-decoder=pcm_f64le \
--enable-decoder=pcm_lxf \
--enable-decoder=pcm_mulaw \
--enable-decoder=pcm_mulaw_at \
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

make -j4
make -j4 install
