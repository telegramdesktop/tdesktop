/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_utility.h"

#include "media/streaming/media_streaming_common.h"
#include "ui/image/image_prepare.h"
#include "ffmpeg/ffmpeg_utility.h"

namespace Media {
namespace Streaming {
namespace {

constexpr auto kSkipInvalidDataPackets = 10;

} // namespace

crl::time FramePosition(const Stream &stream) {
	const auto pts = !stream.frame
		? AV_NOPTS_VALUE
		: (stream.frame->best_effort_timestamp != AV_NOPTS_VALUE)
		? stream.frame->best_effort_timestamp
		: (stream.frame->pts != AV_NOPTS_VALUE)
		? stream.frame->pts
		: stream.frame->pkt_dts;
	return FFmpeg::PtsToTime(pts, stream.timeBase);
}

FFmpeg::AvErrorWrap ProcessPacket(Stream &stream, FFmpeg::Packet &&packet) {
	Expects(stream.codec != nullptr);

	auto error = FFmpeg::AvErrorWrap();

	const auto native = &packet.fields();
	const auto guard = gsl::finally([
		&,
		size = native->size,
		data = native->data
	] {
		native->size = size;
		native->data = data;
		packet = FFmpeg::Packet();
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
				return FFmpeg::AvErrorWrap(); // Try to skip a bad packet.
			}
		}
	}
	return error;
}

FFmpeg::AvErrorWrap ReadNextFrame(Stream &stream) {
	Expects(stream.frame != nullptr);

	auto error = FFmpeg::AvErrorWrap();

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
	} else if (!FFmpeg::FrameHasData(frame)) {
		LOG(("Streaming Error: Bad frame data."));
		return QImage();
	}
	if (resize.isEmpty()) {
		resize = frameSize;
	} else if (FFmpeg::RotationSwapWidthHeight(stream.rotation)) {
		resize.transpose();
	}

	if (!FFmpeg::GoodStorageForFrame(storage, resize)) {
		storage = FFmpeg::CreateFrameStorage(resize);
	}
	const auto format = AV_PIX_FMT_BGRA;
	const auto hasDesiredFormat = (frame->format == format);
	if (frameSize == storage.size() && hasDesiredFormat) {
		static_assert(sizeof(uint32) == FFmpeg::kPixelBytesSize);
		auto to = reinterpret_cast<uint32*>(storage.bits());
		auto from = reinterpret_cast<const uint32*>(frame->data[0]);
		const auto deltaTo = (storage.bytesPerLine() / sizeof(uint32))
			- storage.width();
		const auto deltaFrom = (frame->linesize[0] / sizeof(uint32))
			- frame->width;
		for (const auto y : ranges::view::ints(0, frame->height)) {
			for (const auto x : ranges::view::ints(0, frame->width)) {
				// Wipe out possible alpha values.
				*to++ = 0xFF000000U | *from++;
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

	FFmpeg::ClearFrameMemory(frame);
	return storage;
}

QImage PrepareByRequest(
		const QImage &original,
		const FrameRequest &request,
		QImage storage) {
	Expects(!request.outer.isEmpty());

	if (!FFmpeg::GoodStorageForFrame(storage, request.outer)) {
		storage = FFmpeg::CreateFrameStorage(request.outer);
	}
	{
		Painter p(&storage);
		PainterHighQualityEnabler hq(p);
		p.drawImage(QRect(QPoint(), request.outer), original);
	}
	if ((request.corners & RectPart::AllCorners)
		&& (request.radius != ImageRoundRadius::None)) {
		Images::prepareRound(storage, request.radius, request.corners);
	}
	// #TODO streaming later full prepare support.
	return storage;
}

} // namespace Streaming
} // namespace Media
