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
	} else {
		stream.invalidDataPackets = 0;
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

bool GoodForRequest(
		const QImage &image,
		int rotation,
		const FrameRequest &request) {
	if (request.resize.isEmpty()) {
		return true;
	} else if (rotation != 0) {
		return false;
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

		sws_scale(
			stream.swscale.get(),
			frame->data,
			frame->linesize,
			0,
			frame->height,
			data,
			linesize);
	}

	FFmpeg::ClearFrameMemory(frame);
	return storage;
}

void PaintFrameOuter(QPainter &p, const QRect &inner, QSize outer) {
	const auto left = inner.x();
	const auto right = outer.width() - inner.width() - left;
	const auto top = inner.y();
	const auto bottom = outer.height() - inner.height() - top;
	if (left > 0) {
		p.fillRect(0, 0, left, outer.height(), st::imageBg);
	}
	if (right > 0) {
		p.fillRect(
			outer.width() - right,
			0,
			right,
			outer.height(),
			st::imageBg);
	}
	if (top > 0) {
		p.fillRect(left, 0, inner.width(), top, st::imageBg);
	}
	if (bottom > 0) {
		p.fillRect(
			left,
			outer.height() - bottom,
			inner.width(),
			bottom,
			st::imageBg);
	}
}

void PaintFrameInner(
		QPainter &p,
		QRect to,
		const QImage &original,
		bool alpha,
		int rotation) {
	const auto rotated = [](QRect rect, int rotation) {
		switch (rotation) {
		case 0: return rect;
		case 90: return QRect(
			rect.y(),
			-rect.x() - rect.width(),
			rect.height(),
			rect.width());
		case 180: return QRect(
			-rect.x() - rect.width(),
			-rect.y() - rect.height(),
			rect.width(),
			rect.height());
		case 270: return QRect(
			-rect.y() - rect.height(),
			rect.x(),
			rect.height(),
			rect.width());
		}
		Unexpected("Rotation in PaintFrameInner.");
	};

	PainterHighQualityEnabler hq(p);
	if (rotation) {
		p.rotate(rotation);
	}
	const auto rect = rotated(to, rotation);
	if (alpha) {
		p.fillRect(rect, Qt::white);
	}
	p.drawImage(rect, original);
}

void PaintFrameContent(
		QPainter &p,
		const QImage &original,
		bool alpha,
		int rotation,
		const FrameRequest &request) {
	const auto full = request.outer.isEmpty()
		? original.size()
		: request.outer;
	const auto size = request.resize.isEmpty()
		? original.size()
		: request.resize;
	const auto to = QRect(
		(full.width() - size.width()) / 2,
		(full.height() - size.height()) / 2,
		size.width(),
		size.height());
	PaintFrameOuter(p, to, full);
	PaintFrameInner(p, to, original, alpha, rotation);
}

void ApplyFrameRounding(QImage &storage, const FrameRequest &request) {
	if (!(request.corners & RectPart::AllCorners)
		|| (request.radius == ImageRoundRadius::None)) {
		return;
	}
	Images::prepareRound(storage, request.radius, request.corners);
}

QImage PrepareByRequest(
		const QImage &original,
		bool alpha,
		int rotation,
		const FrameRequest &request,
		QImage storage) {
	Expects(!request.outer.isEmpty() || alpha);

	const auto outer = request.outer.isEmpty()
		? original.size()
		: request.outer;
	if (!FFmpeg::GoodStorageForFrame(storage, outer)) {
		storage = FFmpeg::CreateFrameStorage(outer);
	}

	QPainter p(&storage);
	PaintFrameContent(p, original, alpha, rotation, request);
	p.end();

	ApplyFrameRounding(storage, request);
	return storage;
}

} // namespace Streaming
} // namespace Media
