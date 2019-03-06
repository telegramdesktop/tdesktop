/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_utility.h"

#include "media/streaming/media_streaming_common.h"
#include "ui/image/image_prepare.h"

extern "C" {
#include <libavutil/opt.h>
} // extern "C"

namespace Media {
namespace Streaming {
namespace {

constexpr auto kSkipInvalidDataPackets = 10;
constexpr auto kAlignImageBy = 16;
constexpr auto kPixelBytesSize = 4;
constexpr auto kImageFormat = QImage::Format_ARGB32_Premultiplied;
constexpr auto kAvioBlockSize = 4096;
constexpr auto kMaxScaleByAspectRatio = 16;

void AlignedImageBufferCleanupHandler(void* data) {
	const auto buffer = static_cast<uchar*>(data);
	delete[] buffer;
}

[[nodiscard]] bool IsAlignedImage(const QImage &image) {
	return !(reinterpret_cast<uintptr_t>(image.bits()) % kAlignImageBy)
		&& !(image.bytesPerLine() % kAlignImageBy);
}

[[nodiscard]] bool IsValidAspectRatio(AVRational aspect) {
	return (aspect.num > 0)
		&& (aspect.den > 0)
		&& (aspect.num <= aspect.den * kMaxScaleByAspectRatio)
		&& (aspect.den <= aspect.num * kMaxScaleByAspectRatio);
}

} // namespace

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

IOPointer MakeIOPointer(
		void *opaque,
		int(*read)(void *opaque, uint8_t *buffer, int bufferSize),
		int(*write)(void *opaque, uint8_t *buffer, int bufferSize),
		int64_t(*seek)(void *opaque, int64_t offset, int whence)) {
	auto buffer = reinterpret_cast<uchar*>(av_malloc(kAvioBlockSize));
	if (!buffer) {
		LogError(qstr("av_malloc"));
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
		LogError(qstr("avio_alloc_context"));
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
		int(*write)(void *opaque, uint8_t *buffer, int bufferSize),
		int64_t(*seek)(void *opaque, int64_t offset, int whence)) {
	auto io = MakeIOPointer(opaque, read, write, seek);
	if (!io) {
		return {};
	}
	auto result = avformat_alloc_context();
	if (!result) {
		LogError(qstr("avformat_alloc_context"));
		return {};
	}
	result->pb = io.get();

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
		LogError(qstr("avformat_open_input"), error);
		return {};
	}
	result->flags |= AVFMT_FLAG_FAST_SEEK;

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

CodecPointer MakeCodecPointer(not_null<AVStream*> stream) {
	auto error = AvErrorWrap();

	auto result = CodecPointer(avcodec_alloc_context3(nullptr));
	const auto context = result.get();
	if (!context) {
		LogError(qstr("avcodec_alloc_context3"));
		return {};
	}
	error = avcodec_parameters_to_context(context, stream->codecpar);
	if (error) {
		LogError(qstr("avcodec_parameters_to_context"), error);
		return {};
	}
	av_codec_set_pkt_timebase(context, stream->time_base);
	av_opt_set_int(context, "refcounted_frames", 1, 0);

	const auto codec = avcodec_find_decoder(context->codec_id);
	if (!codec) {
		LogError(qstr("avcodec_find_decoder"), context->codec_id);
		return {};
	} else if ((error = avcodec_open2(context, codec, nullptr))) {
		LogError(qstr("avcodec_open2"), error);
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
		not_null<AVFrame*> frame,
		QSize resize,
		SwscalePointer *existing) {
	// We have to use custom caching for SwsContext, because
	// sws_getCachedContext checks passed flags with existing context flags,
	// and re-creates context if they're different, but in the process of
	// context creation the passed flags are modified before being written
	// to the resulting context, so the caching doesn't work.
	if (existing && (*existing) != nullptr) {
		const auto &deleter = existing->get_deleter();
		if (deleter.resize == resize
			&& deleter.frameSize == QSize(frame->width, frame->height)
			&& deleter.frameFormat == frame->format) {
			return std::move(*existing);
		}
	}
	if (frame->format <= AV_PIX_FMT_NONE || frame->format >= AV_PIX_FMT_NB) {
		LogError(qstr("frame->format"));
		return SwscalePointer();
	}

	const auto result = sws_getCachedContext(
		existing ? existing->release() : nullptr,
		frame->width,
		frame->height,
		AVPixelFormat(frame->format),
		resize.width(),
		resize.height(),
		AV_PIX_FMT_BGRA,
		0,
		nullptr,
		nullptr,
		nullptr);
	if (!result) {
		LogError(qstr("sws_getCachedContext"));
	}
	return SwscalePointer(
		result,
		{ resize, QSize{ frame->width, frame->height }, frame->format });
}

void SwscaleDeleter::operator()(SwsContext *value) {
	if (value) {
		sws_freeContext(value);
	}
}

void LogError(QLatin1String method) {
	LOG(("Streaming Error: Error in %1.").arg(method));
}

void LogError(QLatin1String method, AvErrorWrap error) {
	LOG(("Streaming Error: Error in %1 (code: %2, text: %3)."
		).arg(method
		).arg(error.code()
		).arg(error.text()));
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

crl::time FramePosition(const Stream &stream) {
	const auto pts = !stream.frame
		? AV_NOPTS_VALUE
		: (stream.frame->best_effort_timestamp != AV_NOPTS_VALUE)
		? stream.frame->best_effort_timestamp
		: (stream.frame->pts != AV_NOPTS_VALUE)
		? stream.frame->pts
		: stream.frame->pkt_dts;
	return PtsToTime(pts, stream.timeBase);
}

int ReadRotationFromMetadata(not_null<AVStream*> stream) {
	const auto tag = av_dict_get(stream->metadata, "rotate", nullptr, 0);
	if (tag && *tag->value) {
		const auto string = QString::fromUtf8(tag->value);
		auto ok = false;
		const auto degrees = string.toInt(&ok);
		if (ok && (degrees == 90 || degrees == 180 || degrees == 270)) {
			return degrees;
		}
	}
	return 0;
}

AVRational ValidateAspectRatio(AVRational aspect) {
	return IsValidAspectRatio(aspect) ? aspect : kNormalAspect;
}

QSize CorrectByAspect(QSize size, AVRational aspect) {
	Expects(IsValidAspectRatio(aspect));

	return QSize(size.width() * aspect.num / aspect.den, size.height());
}

bool RotationSwapWidthHeight(int rotation) {
	return (rotation == 90 || rotation == 270);
}

AvErrorWrap ProcessPacket(Stream &stream, Packet &&packet) {
	Expects(stream.codec != nullptr);

	auto error = AvErrorWrap();

	const auto native = &packet.fields();
	const auto guard = gsl::finally([
		&,
		size = native->size,
		data = native->data
	] {
		native->size = size;
		native->data = data;
		packet = Packet();
	});

	error = avcodec_send_packet(
		stream.codec.get(),
		native->data ? native : nullptr); // Drain on eof.
	if (error) {
		LogError(qstr("avcodec_send_packet"), error);
		if (error.code() == AVERROR_INVALIDDATA
			// There is a sample voice message where skipping such packet
			// results in a crash (read_access to nullptr) in swr_convert().
			&& stream.codec->codec_id != AV_CODEC_ID_OPUS) {
			if (++stream.invalidDataPackets < kSkipInvalidDataPackets) {
				return AvErrorWrap(); // Try to skip a bad packet.
			}
		}
	}
	return error;
}

AvErrorWrap ReadNextFrame(Stream &stream) {
	Expects(stream.frame != nullptr);

	auto error = AvErrorWrap();

	do {
		error = avcodec_receive_frame(
			stream.codec.get(),
			stream.frame.get());
		if (!error
			|| error.code() != AVERROR(EAGAIN)
			|| stream.queue.empty()) {
			return error;
		}

		error = ProcessPacket(stream, std::move(stream.queue.front()));
		stream.queue.pop_front();
	} while (!error);

	return error;
}

bool GoodForRequest(const QImage &image, const FrameRequest &request) {
	if (request.resize.isEmpty()) {
		return true;
	} else if ((request.radius != ImageRoundRadius::None)
		&& ((request.corners & RectPart::AllCorners) != 0)) {
		return false;
	}
	return (request.resize == request.outer)
		&& (request.resize == image.size());
}

QImage ConvertFrame(
		Stream &stream,
		AVFrame *frame,
		QSize resize,
		QImage storage) {
	Expects(frame != nullptr);

	const auto frameSize = QSize(frame->width, frame->height);
	if (frameSize.isEmpty()) {
		LOG(("Streaming Error: Bad frame size %1,%2"
			).arg(frameSize.width()
			).arg(frameSize.height()));
		return QImage();
	} else if (!FrameHasData(frame)) {
		LOG(("Streaming Error: Bad frame data."));
		return QImage();
	}
	if (resize.isEmpty()) {
		resize = frameSize;
	} else if (RotationSwapWidthHeight(stream.rotation)) {
		resize.transpose();
	}

	if (!GoodStorageForFrame(storage, resize)) {
		storage = CreateFrameStorage(resize);
	}
	const auto format = AV_PIX_FMT_BGRA;
	const auto hasDesiredFormat = (frame->format == format);
	if (frameSize == storage.size() && hasDesiredFormat) {
		static_assert(sizeof(uint32) == kPixelBytesSize);
		auto to = reinterpret_cast<uint32*>(storage.bits());
		auto from = reinterpret_cast<const uint32*>(frame->data[0]);
		const auto deltaTo = (storage.bytesPerLine() / kPixelBytesSize)
			- storage.width();
		const auto deltaFrom = (frame->linesize[0] / kPixelBytesSize)
			- frame->width;
		for (const auto y : ranges::view::ints(0, frame->height)) {
			for (const auto x : ranges::view::ints(0, frame->width)) {
				// Wipe out possible alpha values.
				*to++ = 0x000000FFU | *from++;
			}
			to += deltaTo;
			from += deltaFrom;
		}
	} else {
		stream.swscale = MakeSwscalePointer(
			frame,
			resize,
			&stream.swscale);
		if (!stream.swscale) {
			return QImage();
		}

		// AV_NUM_DATA_POINTERS defined in AVFrame struct
		uint8_t *data[AV_NUM_DATA_POINTERS] = { storage.bits(), nullptr };
		int linesize[AV_NUM_DATA_POINTERS] = { storage.bytesPerLine(), 0 };

		const auto lines = sws_scale(
			stream.swscale.get(),
			frame->data,
			frame->linesize,
			0,
			frame->height,
			data,
			linesize);
		if (lines != resize.height()) {
			LOG(("Streaming Error: "
				"Unable to sws_scale to good size %1, got %2."
				).arg(resize.height()
				).arg(lines));
			return QImage();
		}
	}

	ClearFrameMemory(frame);
	return storage;
}

QImage PrepareByRequest(
		const QImage &original,
		const FrameRequest &request,
		QImage storage) {
	Expects(!request.outer.isEmpty());

	if (!GoodStorageForFrame(storage, request.outer)) {
		storage = CreateFrameStorage(request.outer);
	}
	{
		Painter p(&storage);
		PainterHighQualityEnabler hq(p);
		p.drawImage(QRect(QPoint(), request.outer), original);
	}
	// #TODO streaming later full prepare support.
	return storage;
}

} // namespace Streaming
} // namespace Media
