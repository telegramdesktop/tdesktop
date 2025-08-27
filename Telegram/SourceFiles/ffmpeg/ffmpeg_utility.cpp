/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ffmpeg/ffmpeg_utility.h"

#include "base/algorithm.h"
#include "logs.h"

#if !defined Q_OS_WIN && !defined Q_OS_MAC
#include "base/platform/linux/base_linux_library.h"
#include <deque>
#endif // !Q_OS_WIN && !Q_OS_MAC

#include <QImage>

#ifdef LIB_FFMPEG_USE_QT_PRIVATE_API
#include <private/qdrawhelper_p.h>
#endif // LIB_FFMPEG_USE_QT_PRIVATE_API

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/display.h>
} // extern "C"

#if !defined Q_OS_WIN && !defined Q_OS_MAC
extern "C" {
void _libvdpau_so_tramp_resolve_all(void) __attribute__((weak));
void _libva_drm_so_tramp_resolve_all(void) __attribute__((weak));
void _libva_x11_so_tramp_resolve_all(void) __attribute__((weak));
void _libva_so_tramp_resolve_all(void) __attribute__((weak));
void _libdrm_so_tramp_resolve_all(void) __attribute__((weak));
} // extern "C"
#endif // !Q_OS_WIN && !Q_OS_MAC

namespace FFmpeg {
namespace {

// See https://github.com/telegramdesktop/tdesktop/issues/7225
constexpr auto kAlignImageBy = 64;
constexpr auto kImageFormat = QImage::Format_ARGB32_Premultiplied;
constexpr auto kMaxScaleByAspectRatio = 16;
constexpr auto kAvioBlockSize = 4096;
constexpr auto kTimeUnknown = std::numeric_limits<crl::time>::min();
constexpr auto kDurationMax = crl::time(std::numeric_limits<int>::max());

using GetFormatMethod = enum AVPixelFormat(*)(
	struct AVCodecContext *s,
	const enum AVPixelFormat *fmt);

struct HwAccelDescriptor {
	GetFormatMethod getFormat = nullptr;
	AVPixelFormat format = AV_PIX_FMT_NONE;
};

void AlignedImageBufferCleanupHandler(void* data) {
	const auto buffer = static_cast<uchar*>(data);
	delete[] buffer;
}

[[nodiscard]] bool IsValidAspectRatio(AVRational aspect) {
	return (aspect.num > 0)
		&& (aspect.den > 0)
		&& (aspect.num <= aspect.den * kMaxScaleByAspectRatio)
		&& (aspect.den <= aspect.num * kMaxScaleByAspectRatio);
}

[[nodiscard]] bool IsAlignedImage(const QImage &image) {
	return !(reinterpret_cast<uintptr_t>(image.bits()) % kAlignImageBy)
		&& !(image.bytesPerLine() % kAlignImageBy);
}

void UnPremultiplyLine(uchar *dst, const uchar *src, int intsCount) {
	[[maybe_unused]] const auto udst = reinterpret_cast<uint*>(dst);
	const auto usrc = reinterpret_cast<const uint*>(src);

#ifndef LIB_FFMPEG_USE_QT_PRIVATE_API
	for (auto i = 0; i != intsCount; ++i) {
		udst[i] = qUnpremultiply(usrc[i]);
	}
#else // !LIB_FFMPEG_USE_QT_PRIVATE_API
	static const auto layout = &qPixelLayouts[QImage::Format_ARGB32];
	layout->storeFromARGB32PM(dst, usrc, 0, intsCount, nullptr, nullptr);
#endif // LIB_FFMPEG_USE_QT_PRIVATE_API
}

void PremultiplyLine(uchar *dst, const uchar *src, int intsCount) {
	const auto udst = reinterpret_cast<uint*>(dst);
	[[maybe_unused]] const auto usrc = reinterpret_cast<const uint*>(src);

#ifndef LIB_FFMPEG_USE_QT_PRIVATE_API
	for (auto i = 0; i != intsCount; ++i) {
		udst[i] = qPremultiply(usrc[i]);
	}
#else // !LIB_FFMPEG_USE_QT_PRIVATE_API
	static const auto layout = &qPixelLayouts[QImage::Format_ARGB32];
	layout->fetchToARGB32PM(udst, src, 0, intsCount, nullptr, nullptr);
#endif // LIB_FFMPEG_USE_QT_PRIVATE_API
}

#if !defined Q_OS_WIN && !defined Q_OS_MAC
[[nodiscard]] auto CheckHwLibs() {
	auto list = std::deque{
		AV_PIX_FMT_CUDA,
	};
	if (!_libvdpau_so_tramp_resolve_all
			|| base::Platform::LoadLibrary("libvdpau.so.1")) {
		list.push_front(AV_PIX_FMT_VDPAU);
	}
	if ([&] {
		const auto list = std::array{
			std::make_pair(_libva_drm_so_tramp_resolve_all, "libva-drm.so.2"),
			std::make_pair(_libva_x11_so_tramp_resolve_all, "libva-x11.so.2"),
			std::make_pair(_libva_so_tramp_resolve_all, "libva.so.2"),
			std::make_pair(_libdrm_so_tramp_resolve_all, "libdrm.so.2"),
		};
		for (const auto &lib : list) {
			if (lib.first && !base::Platform::LoadLibrary(lib.second)) {
				return false;
			}
		}
		return true;
	}()) {
		list.push_front(AV_PIX_FMT_VAAPI);
	}
	return list;
}
#endif // !Q_OS_WIN && !Q_OS_MAC

[[nodiscard]] bool InitHw(AVCodecContext *context, AVHWDeviceType type) {
	AVCodecContext *parent = static_cast<AVCodecContext*>(context->opaque);

	auto hwDeviceContext = (AVBufferRef*)nullptr;
	AvErrorWrap error = av_hwdevice_ctx_create(
		&hwDeviceContext,
		type,
		nullptr,
		nullptr,
		0);
	if (error || !hwDeviceContext) {
		LogError(u"av_hwdevice_ctx_create"_q, error);
		return false;
	}
	DEBUG_LOG(("Video Info: "
		"Trying \"%1\" hardware acceleration for \"%2\" decoder."
		).arg(
			av_hwdevice_get_type_name(type),
			context->codec->name));
	if (parent->hw_device_ctx) {
		av_buffer_unref(&parent->hw_device_ctx);
	}
	parent->hw_device_ctx = av_buffer_ref(hwDeviceContext);
	av_buffer_unref(&hwDeviceContext);

	context->hw_device_ctx = parent->hw_device_ctx;
	return true;
}

[[nodiscard]] enum AVPixelFormat GetHwFormat(
		AVCodecContext *context,
		const enum AVPixelFormat *formats) {
	const auto has = [&](enum AVPixelFormat format) {
		const enum AVPixelFormat *p = nullptr;
		for (p = formats; *p != AV_PIX_FMT_NONE; p++) {
			if (*p == format) {
				return true;
			}
		}
		return false;
	};
#if defined Q_OS_WIN || defined Q_OS_MAC
	const auto list = std::array{
#ifdef Q_OS_WIN
		AV_PIX_FMT_D3D11,
		AV_PIX_FMT_DXVA2_VLD,
		AV_PIX_FMT_CUDA,
#elif defined Q_OS_MAC // Q_OS_WIN
		AV_PIX_FMT_VIDEOTOOLBOX,
#endif // Q_OS_WIN || Q_OS_MAC
	};
#else // Q_OS_WIN || Q_OS_MAC
	static const auto list = CheckHwLibs();
#endif // !Q_OS_WIN && !Q_OS_MAC
	for (const auto format : list) {
		if (!has(format)) {
			continue;
		}
		const auto type = [&] {
			switch (format) {
#ifdef Q_OS_WIN
			case AV_PIX_FMT_D3D11: return AV_HWDEVICE_TYPE_D3D11VA;
			case AV_PIX_FMT_DXVA2_VLD: return AV_HWDEVICE_TYPE_DXVA2;
			case AV_PIX_FMT_CUDA: return AV_HWDEVICE_TYPE_CUDA;
#elif defined Q_OS_MAC // Q_OS_WIN
			case AV_PIX_FMT_VIDEOTOOLBOX:
				return AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
#else // Q_OS_WIN || Q_OS_MAC
			case AV_PIX_FMT_VAAPI: return AV_HWDEVICE_TYPE_VAAPI;
			case AV_PIX_FMT_VDPAU: return AV_HWDEVICE_TYPE_VDPAU;
			case AV_PIX_FMT_CUDA: return AV_HWDEVICE_TYPE_CUDA;
#endif // Q_OS_WIN || Q_OS_MAC
			}
			return AV_HWDEVICE_TYPE_NONE;
		}();
		if (type == AV_HWDEVICE_TYPE_NONE && context->hw_device_ctx) {
			av_buffer_unref(&context->hw_device_ctx);
		} else if (type != AV_HWDEVICE_TYPE_NONE && !InitHw(context, type)) {
			continue;
		}
		return format;
	}
	enum AVPixelFormat result = AV_PIX_FMT_NONE;
	for (const enum AVPixelFormat *p = formats; *p != AV_PIX_FMT_NONE; p++) {
		result = *p;
	}
	return result;
}

template <AVPixelFormat Required>
enum AVPixelFormat GetFormatImplementation(
		AVCodecContext *ctx,
		const enum AVPixelFormat *pix_fmts) {
	const enum AVPixelFormat *p = nullptr;
	for (p = pix_fmts; *p != -1; p++) {
		if (*p == Required) {
			return *p;
		}
	}
	return AV_PIX_FMT_NONE;
}

} // namespace

IOPointer MakeIOPointer(
		void *opaque,
		int(*read)(void *opaque, uint8_t *buffer, int bufferSize),
#if DA_FFMPEG_CONST_WRITE_CALLBACK
		int(*write)(void *opaque, const uint8_t *buffer, int bufferSize),
#else
		int(*write)(void *opaque, uint8_t *buffer, int bufferSize),
#endif
		int64_t(*seek)(void *opaque, int64_t offset, int whence)) {
	auto buffer = reinterpret_cast<uchar*>(av_malloc(kAvioBlockSize));
	if (!buffer) {
		LogError(u"av_malloc"_q);
		return {};
	}
	auto result = IOPointer(avio_alloc_context(
		buffer,
		kAvioBlockSize,
		write ? 1 : 0,
		opaque,
		read,
		write,
		seek));
	if (!result) {
		av_freep(&buffer);
		LogError(u"avio_alloc_context"_q);
		return {};
	}
	return result;
}

void IODeleter::operator()(AVIOContext *value) {
	if (value) {
		av_freep(&value->buffer);
		avio_context_free(&value);
	}
}

FormatPointer MakeFormatPointer(
		void *opaque,
		int(*read)(void *opaque, uint8_t *buffer, int bufferSize),
#if DA_FFMPEG_CONST_WRITE_CALLBACK
		int(*write)(void *opaque, const uint8_t *buffer, int bufferSize),
#else
		int(*write)(void *opaque, uint8_t *buffer, int bufferSize),
#endif
		int64_t(*seek)(void *opaque, int64_t offset, int whence)) {
	auto io = MakeIOPointer(opaque, read, write, seek);
	if (!io) {
		return {};
	}
	io->seekable = (seek != nullptr);
	auto result = avformat_alloc_context();
	if (!result) {
		LogError(u"avformat_alloc_context"_q);
		return {};
	}
	result->pb = io.get();
	result->flags |= AVFMT_FLAG_CUSTOM_IO;

	auto options = (AVDictionary*)nullptr;
	const auto guard = gsl::finally([&] { av_dict_free(&options); });
	av_dict_set(&options, "usetoc", "1", 0);

	const auto error = AvErrorWrap(avformat_open_input(
		&result,
		nullptr,
		nullptr,
		&options));
	if (error) {
		// avformat_open_input freed 'result' in case an error happened.
		LogError(u"avformat_open_input"_q, error);
		return {};
	}
	if (seek) {
		result->flags |= AVFMT_FLAG_FAST_SEEK;
	}

	// Now FormatPointer will own and free the IO context.
	io.release();
	return FormatPointer(result);
}

FormatPointer MakeWriteFormatPointer(
		void *opaque,
		int(*read)(void *opaque, uint8_t *buffer, int bufferSize),
#if DA_FFMPEG_CONST_WRITE_CALLBACK
		int(*write)(void *opaque, const uint8_t *buffer, int bufferSize),
#else
		int(*write)(void *opaque, uint8_t *buffer, int bufferSize),
#endif
		int64_t(*seek)(void *opaque, int64_t offset, int whence),
		const QByteArray &format) {
	const AVOutputFormat *found = nullptr;
	void *i = nullptr;
	while ((found = av_muxer_iterate(&i))) {
		if (found->name == format) {
			break;
		}
	}
	if (!found) {
		LogError(
			"av_muxer_iterate",
			u"Format %1 not found"_q.arg(QString::fromUtf8(format)));
		return {};
	}

	auto io = MakeIOPointer(opaque, read, write, seek);
	if (!io) {
		return {};
	}
	io->seekable = (seek != nullptr);

	auto result = (AVFormatContext*)nullptr;
	auto error = AvErrorWrap(avformat_alloc_output_context2(
		&result,
		(AVOutputFormat*)found,
		nullptr,
		nullptr));
	if (!result || error) {
		LogError("avformat_alloc_output_context2", error);
		return {};
	}
	result->pb = io.get();
	result->flags |= AVFMT_FLAG_CUSTOM_IO;

	// Now FormatPointer will own and free the IO context.
	io.release();
	return FormatPointer(result);
}

void FormatDeleter::operator()(AVFormatContext *value) {
	if (value) {
		const auto deleter = IOPointer(value->pb);
		avformat_close_input(&value);
	}
}

const AVCodec *FindDecoder(not_null<AVCodecContext*> context) {
	// Force libvpx-vp9, because we need alpha channel support.
	return (context->codec_id == AV_CODEC_ID_VP9)
		? avcodec_find_decoder_by_name("libvpx-vp9")
		: avcodec_find_decoder(context->codec_id);
}

CodecPointer MakeCodecPointer(CodecDescriptor descriptor) {
	auto error = AvErrorWrap();

	auto result = CodecPointer(avcodec_alloc_context3(nullptr));
	const auto context = result.get();
	if (!context) {
		LogError(u"avcodec_alloc_context3"_q);
		return {};
	}
	const auto stream = descriptor.stream;
	error = avcodec_parameters_to_context(context, stream->codecpar);
	if (error) {
		LogError(u"avcodec_parameters_to_context"_q, error);
		return {};
	}
	context->pkt_timebase = stream->time_base;
	av_opt_set(context, "threads", "auto", 0);
	av_opt_set_int(context, "refcounted_frames", 1, 0);

	const auto codec = FindDecoder(context);
	if (!codec) {
		LogError(u"avcodec_find_decoder"_q, context->codec_id);
		return {};
	}

	if (descriptor.hwAllowed) {
		context->get_format = GetHwFormat;
		context->opaque = context;
	} else {
		DEBUG_LOG(("Video Info: Using software \"%2\" decoder."
			).arg(codec->name));
	}

	if ((error = avcodec_open2(context, codec, nullptr))) {
		LogError(u"avcodec_open2"_q, error);
		return {};
	}
	return result;
}

void CodecDeleter::operator()(AVCodecContext *value) {
	if (value) {
		avcodec_free_context(&value);
	}
}

FramePointer MakeFramePointer() {
	return FramePointer(av_frame_alloc());
}

FramePointer DuplicateFramePointer(AVFrame *frame) {
	return frame
		? FramePointer(av_frame_clone(frame))
		: FramePointer();
}

bool FrameHasData(AVFrame *frame) {
	return (frame && frame->data[0] != nullptr);
}

void ClearFrameMemory(AVFrame *frame) {
	if (FrameHasData(frame)) {
		av_frame_unref(frame);
	}
}

void FrameDeleter::operator()(AVFrame *value) {
	av_frame_free(&value);
}

SwscalePointer MakeSwscalePointer(
		QSize srcSize,
		int srcFormat,
		QSize dstSize,
		int dstFormat,
		SwscalePointer *existing) {
	// We have to use custom caching for SwsContext, because
	// sws_getCachedContext checks passed flags with existing context flags,
	// and re-creates context if they're different, but in the process of
	// context creation the passed flags are modified before being written
	// to the resulting context, so the caching doesn't work.
	if (existing && (*existing) != nullptr) {
		const auto &deleter = existing->get_deleter();
		if (deleter.srcSize == srcSize
			&& deleter.srcFormat == srcFormat
			&& deleter.dstSize == dstSize
			&& deleter.dstFormat == dstFormat) {
			return std::move(*existing);
		}
	}
	if (srcFormat <= AV_PIX_FMT_NONE || srcFormat >= AV_PIX_FMT_NB) {
		LogError(u"frame->format"_q);
		return SwscalePointer();
	}

	const auto result = sws_getCachedContext(
		existing ? existing->release() : nullptr,
		srcSize.width(),
		srcSize.height(),
		AVPixelFormat(srcFormat),
		dstSize.width(),
		dstSize.height(),
		AVPixelFormat(dstFormat),
		0,
		nullptr,
		nullptr,
		nullptr);
	if (!result) {
		LogError(u"sws_getCachedContext"_q);
	}
	return SwscalePointer(
		result,
		{ srcSize, srcFormat, dstSize, dstFormat });
}

SwscalePointer MakeSwscalePointer(
		not_null<AVFrame*> frame,
		QSize resize,
		SwscalePointer *existing) {
	return MakeSwscalePointer(
		QSize(frame->width, frame->height),
		frame->format,
		resize,
		AV_PIX_FMT_BGRA,
		existing);
}

void SwresampleDeleter::operator()(SwrContext *value) {
	if (value) {
		swr_free(&value);
	}
}

SwresamplePointer MakeSwresamplePointer(
#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
		AVChannelLayout *srcLayout,
#else // DA_FFMPEG_NEW_CHANNEL_LAYOUT
		uint64_t srcLayout,
#endif // DA_FFMPEG_NEW_CHANNEL_LAYOUT
		AVSampleFormat srcFormat,
		int srcRate,
#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
		AVChannelLayout *dstLayout,
#else // DA_FFMPEG_NEW_CHANNEL_LAYOUT
		uint64_t dstLayout,
#endif // DA_FFMPEG_NEW_CHANNEL_LAYOUT
		AVSampleFormat dstFormat,
		int dstRate,
		SwresamplePointer *existing) {
	// We have to use custom caching for SwsContext, because
	// sws_getCachedContext checks passed flags with existing context flags,
	// and re-creates context if they're different, but in the process of
	// context creation the passed flags are modified before being written
	// to the resulting context, so the caching doesn't work.
	if (existing && (*existing) != nullptr) {
		const auto &deleter = existing->get_deleter();
		if (true
#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
			&& srcLayout->nb_channels == deleter.srcChannels
			&& dstLayout->nb_channels == deleter.dstChannels
#else // DA_FFMPEG_NEW_CHANNEL_LAYOUT
			&& (av_get_channel_layout_nb_channels(srcLayout)
				== deleter.srcChannels)
			&& (av_get_channel_layout_nb_channels(dstLayout)
				== deleter.dstChannels)
#endif // DA_FFMPEG_NEW_CHANNEL_LAYOUT
			&& srcFormat == deleter.srcFormat
			&& dstFormat == deleter.dstFormat
			&& srcRate == deleter.srcRate
			&& dstRate == deleter.dstRate) {
			return std::move(*existing);
		}
	}

	// Initialize audio resampler
	AvErrorWrap error;
#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
	auto result = (SwrContext*)nullptr;
	error = AvErrorWrap(swr_alloc_set_opts2(
		&result,
		dstLayout,
		dstFormat,
		dstRate,
		srcLayout,
		srcFormat,
		srcRate,
		0,
		nullptr));
	if (error || !result) {
		LogError(u"swr_alloc_set_opts2"_q, error);
		return SwresamplePointer();
	}
#else // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	auto result = swr_alloc_set_opts(
		existing ? existing->get() : nullptr,
		dstLayout,
		dstFormat,
		dstRate,
		srcLayout,
		srcFormat,
		srcRate,
		0,
		nullptr);
	if (!result) {
		LogError(u"swr_alloc_set_opts"_q);
	}
#endif // DA_FFMPEG_NEW_CHANNEL_LAYOUT

	error = AvErrorWrap(swr_init(result));
	if (error) {
		LogError(u"swr_init"_q, error);
		swr_free(&result);
		return SwresamplePointer();
	}

	return SwresamplePointer(
		result,
		{
			srcFormat,
			srcRate,
#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
			srcLayout->nb_channels,
#else // DA_FFMPEG_NEW_CHANNEL_LAYOUT
			av_get_channel_layout_nb_channels(srcLayout),
#endif // DA_FFMPEG_NEW_CHANNEL_LAYOUT
			dstFormat,
			dstRate,
#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
			dstLayout->nb_channels,
#else // DA_FFMPEG_NEW_CHANNEL_LAYOUT
			av_get_channel_layout_nb_channels(dstLayout),
#endif // DA_FFMPEG_NEW_CHANNEL_LAYOUT
		});
}

void SwscaleDeleter::operator()(SwsContext *value) {
	if (value) {
		sws_freeContext(value);
	}
}

void LogError(const QString &method, const QString &details) {
	LOG(("Streaming Error: Error in %1%2."
		).arg(method
		).arg(details.isEmpty() ? QString() : " - " + details));
}

void LogError(
		const QString &method,
		AvErrorWrap error,
		const QString &details) {
	LOG(("Streaming Error: Error in %1 (code: %2, text: %3)%4."
		).arg(method
		).arg(error.code()
		).arg(error.text()
		).arg(details.isEmpty() ? QString() : " - " + details));
}

crl::time PtsToTime(int64_t pts, AVRational timeBase) {
	return (pts == AV_NOPTS_VALUE || !timeBase.den)
		? kTimeUnknown
		: ((pts * 1000LL * timeBase.num) / timeBase.den);
}

crl::time PtsToTimeCeil(int64_t pts, AVRational timeBase) {
	return (pts == AV_NOPTS_VALUE || !timeBase.den)
		? kTimeUnknown
		: ((pts * 1000LL * timeBase.num + timeBase.den - 1) / timeBase.den);
}

int64_t TimeToPts(crl::time time, AVRational timeBase) {
	return (time == kTimeUnknown || !timeBase.num)
		? AV_NOPTS_VALUE
		: (time * timeBase.den) / (1000LL * timeBase.num);
}

crl::time PacketPosition(const Packet &packet, AVRational timeBase) {
	const auto &native = packet.fields();
	return PtsToTime(
		(native.pts == AV_NOPTS_VALUE) ? native.dts : native.pts,
		timeBase);
}

crl::time PacketDuration(const Packet &packet, AVRational timeBase) {
	return PtsToTime(packet.fields().duration, timeBase);
}

int DurationByPacket(const Packet &packet, AVRational timeBase) {
	const auto position = PacketPosition(packet, timeBase);
	const auto duration = std::max(
		PacketDuration(packet, timeBase),
		crl::time(1));
	const auto bad = [](crl::time time) {
		return (time < 0) || (time > kDurationMax);
	};
	if (bad(position) || bad(duration) || bad(position + duration + 1)) {
		LOG(("Streaming Error: Wrong duration by packet: %1 + %2"
			).arg(position
			).arg(duration));
		return -1;
	}
	return int(position + duration + 1);
}

int ReadRotationFromMetadata(not_null<AVStream*> stream) {
	const auto displaymatrix = av_stream_get_side_data(
		stream,
		AV_PKT_DATA_DISPLAYMATRIX,
		nullptr);
	auto theta = 0;
	if (displaymatrix) {
		theta = -round(av_display_rotation_get((int32_t*)displaymatrix));
	}
	theta -= 360 * floor(theta / 360 + 0.9 / 360);
	const auto result = int(base::SafeRound(theta));
	return (result == 90 || result == 180 || result == 270) ? result : 0;
}

AVRational ValidateAspectRatio(AVRational aspect) {
	return IsValidAspectRatio(aspect) ? aspect : kNormalAspect;
}

QSize CorrectByAspect(QSize size, AVRational aspect) {
	Expects(IsValidAspectRatio(aspect));

	return QSize(size.width() * av_q2d(aspect), size.height());
}

bool RotationSwapWidthHeight(int rotation) {
	return (rotation == 90 || rotation == 270);
}

QSize TransposeSizeByRotation(QSize size, int rotation) {
	return RotationSwapWidthHeight(rotation) ? size.transposed() : size;
}

bool GoodStorageForFrame(const QImage &storage, QSize size) {
	return !storage.isNull()
		&& (storage.format() == kImageFormat)
		&& (storage.size() == size)
		&& storage.isDetached()
		&& IsAlignedImage(storage);
}

// Create a QImage of desired size where all the data is properly aligned.
QImage CreateFrameStorage(QSize size) {
	const auto width = size.width();
	const auto height = size.height();
	const auto widthAlign = kAlignImageBy / kPixelBytesSize;
	const auto neededWidth = width + ((width % widthAlign)
		? (widthAlign - (width % widthAlign))
		: 0);
	const auto perLine = neededWidth * kPixelBytesSize;
	const auto buffer = new uchar[perLine * height + kAlignImageBy];
	const auto cleanupData = static_cast<void *>(buffer);
	const auto address = reinterpret_cast<uintptr_t>(buffer);
	const auto alignedBuffer = buffer + ((address % kAlignImageBy)
		? (kAlignImageBy - (address % kAlignImageBy))
		: 0);
	return QImage(
		alignedBuffer,
		width,
		height,
		perLine,
		kImageFormat,
		AlignedImageBufferCleanupHandler,
		cleanupData);
}

void UnPremultiply(QImage &dst, const QImage &src) {
	// This creates QImage::Format_ARGB32_Premultiplied, but we use it
	// as an image in QImage::Format_ARGB32 format.
	if (!GoodStorageForFrame(dst, src.size())) {
		dst = CreateFrameStorage(src.size());
	}
	const auto srcPerLine = src.bytesPerLine();
	const auto dstPerLine = dst.bytesPerLine();
	const auto width = src.width();
	const auto height = src.height();
	auto srcBytes = src.bits();
	auto dstBytes = dst.bits();
	if (srcPerLine != width * 4 || dstPerLine != width * 4) {
		for (auto i = 0; i != height; ++i) {
			UnPremultiplyLine(dstBytes, srcBytes, width);
			srcBytes += srcPerLine;
			dstBytes += dstPerLine;
		}
	} else {
		UnPremultiplyLine(dstBytes, srcBytes, width * height);
	}
}

void PremultiplyInplace(QImage &image) {
	const auto perLine = image.bytesPerLine();
	const auto width = image.width();
	const auto height = image.height();
	auto bytes = image.bits();
	if (perLine != width * 4) {
		for (auto i = 0; i != height; ++i) {
			PremultiplyLine(bytes, bytes, width);
			bytes += perLine;
		}
	} else {
		PremultiplyLine(bytes, bytes, width * height);
	}
}

} // namespace FFmpeg
