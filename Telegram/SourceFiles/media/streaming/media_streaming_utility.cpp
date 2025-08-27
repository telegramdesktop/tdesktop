/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_utility.h"

#include "media/streaming/media_streaming_common.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ffmpeg/ffmpeg_utility.h"

namespace Media {
namespace Streaming {
namespace {

constexpr auto kSkipInvalidDataPackets = 10;

} // namespace

crl::time FramePosition(const Stream &stream) {
	const auto pts = !stream.decodedFrame
		? AV_NOPTS_VALUE
		: (stream.decodedFrame->best_effort_timestamp != AV_NOPTS_VALUE)
		? stream.decodedFrame->best_effort_timestamp
		: (stream.decodedFrame->pts != AV_NOPTS_VALUE)
		? stream.decodedFrame->pts
		: stream.decodedFrame->pkt_dts;
	const auto result = FFmpeg::PtsToTime(pts, stream.timeBase);

	// Sometimes the result here may be larger than the stream duration.
	return (stream.duration == kDurationUnavailable)
		? result
		: std::min(result, stream.duration);
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
		LogError(u"avcodec_send_packet"_q, error);
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
	Expects(stream.decodedFrame != nullptr);

	auto error = FFmpeg::AvErrorWrap();

	do {
		error = avcodec_receive_frame(
			stream.codec.get(),
			stream.decodedFrame.get());
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
		bool hasAlpha,
		int rotation,
		const FrameRequest &request) {
	if (image.isNull()
		|| (hasAlpha && !request.keepAlpha)
		|| request.colored.alpha() != 0) {
		return false;
	} else if (!request.blurredBackground && request.resize.isEmpty()) {
		return true;
	} else if (rotation != 0) {
		return false;
	} else if (!request.rounding.empty() || !request.mask.isNull()) {
		return false;
	}
	const auto size = request.blurredBackground
		? request.outer
		: request.resize;
	return (size == request.outer) && (size == image.size());
}

bool TransferFrame(
		Stream &stream,
		not_null<AVFrame*> decodedFrame,
		not_null<AVFrame*> transferredFrame) {
	Expects(decodedFrame->hw_frames_ctx != nullptr);

	const auto error = FFmpeg::AvErrorWrap(
		av_hwframe_transfer_data(transferredFrame, decodedFrame, 0));
	if (error) {
		LogError(u"av_hwframe_transfer_data"_q, error);
		return false;
	}
	FFmpeg::ClearFrameMemory(decodedFrame);
	return true;
}

QImage ConvertFrame(
		Stream &stream,
		not_null<AVFrame*> frame,
		QSize resize,
		QImage storage) {
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
		for ([[maybe_unused]] const auto y : ranges::views::ints(0, frame->height)) {
			for ([[maybe_unused]] const auto x : ranges::views::ints(0, frame->width)) {
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
		int linesize[AV_NUM_DATA_POINTERS] = { int(storage.bytesPerLine()), 0 };

		sws_scale(
			stream.swscale.get(),
			frame->data,
			frame->linesize,
			0,
			frame->height,
			data,
			linesize);

		if (frame->format == AV_PIX_FMT_YUVA420P) {
			FFmpeg::PremultiplyInplace(storage);
		}
	}

	FFmpeg::ClearFrameMemory(frame);
	return storage;
}

FrameYUV ExtractYUV(Stream &stream, AVFrame *frame) {
	return {
		.size = { frame->width, frame->height },
		.chromaSize = {
			AV_CEIL_RSHIFT(frame->width, 1), // SWScale does that.
			AV_CEIL_RSHIFT(frame->height, 1)
		},
		.y = { .data = frame->data[0], .stride = frame->linesize[0] },
		.u = { .data = frame->data[1], .stride = frame->linesize[1] },
		.v = { .data = frame->data[2], .stride = frame->linesize[2] },
	};
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

QImage PrepareBlurredBackground(QSize outer, QImage frame) {
	const auto bsize = frame.size();
	const auto copyw = std::min(
		bsize.width(),
		std::max(outer.width() * bsize.height() / outer.height(), 1));
	const auto copyh = std::min(
		bsize.height(),
		std::max(outer.height() * bsize.width() / outer.width(), 1));
	auto copy = (bsize == QSize(copyw, copyh))
		? std::move(frame)
		: frame.copy(
			(bsize.width() - copyw) / 2,
			(bsize.height() - copyh) / 2,
			copyw,
			copyh);
	auto scaled = (copy.width() <= 100 && copy.height() <= 100)
		? std::move(copy)
		: copy.scaled(40, 40, Qt::KeepAspectRatio, Qt::FastTransformation);
	return Images::Blur(std::move(scaled), true);
}

void FillBlurredBackground(QPainter &p, QSize outer, QImage bg) {
	auto hq = PainterHighQualityEnabler(p);
	const auto rect = QRect(QPoint(), outer);
	const auto ratio = p.device()->devicePixelRatio();
	p.drawImage(
		rect,
		PrepareBlurredBackground(outer * ratio, std::move(bg)));
	p.fillRect(rect, QColor(0, 0, 0, 48));
}

void PaintFrameContent(
		QPainter &p,
		const QImage &original,
		bool hasAlpha,
		const AVRational &aspect,
		int rotation,
		const FrameRequest &request) {
	const auto outer = request.outer;
	const auto full = request.outer.isEmpty() ? original.size() : outer;
	const auto deAlpha = hasAlpha && !request.keepAlpha;
	const auto resize = request.blurredBackground
		? DecideVideoFrameResize(
			outer,
			FFmpeg::TransposeSizeByRotation(
				FFmpeg::CorrectByAspect(original.size(), aspect), rotation))
		: ExpandDecision{ request.resize.isEmpty()
			? original.size()
			: request.resize };
	const auto size = resize.result;
	const auto target = QRect(
		(full.width() - size.width()) / 2,
		(full.height() - size.height()) / 2,
		size.width(),
		size.height());
	if (request.blurredBackground) {
		if (!resize.expanding) {
			FillBlurredBackground(p, full, original);
		}
	} else if (!hasAlpha || !request.keepAlpha) {
		PaintFrameOuter(p, target, full);
	}
	PaintFrameInner(p, target, original, deAlpha, rotation);
}

void ApplyFrameRounding(QImage &storage, const FrameRequest &request) {
	if (!request.mask.isNull()) {
		auto p = QPainter(&storage);
		p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		p.drawImage(
			QRect(QPoint(), storage.size() / storage.devicePixelRatio()),
			request.mask);
	} else if (!request.rounding.empty()) {
		storage = Images::Round(std::move(storage), request.rounding);
	}
}

ExpandDecision DecideFrameResize(
		QSize outer,
		QSize original,
		int minVisibleNominator,
		int minVisibleDenominator) {
	if (outer.isEmpty()) {
		// Often "expanding" means that we don't need to fill the background.
		return { .result = original, .expanding = true };
	}
	const auto big = original.scaled(outer, Qt::KeepAspectRatioByExpanding);
	if ((big.width() <= outer.width())
		&& (big.height() * minVisibleNominator
			<= outer.height() * minVisibleDenominator)) {
		return { .result = big, .expanding = true };
	}
	return { .result = original.scaled(outer, Qt::KeepAspectRatio) };
}

bool FrameResizeMayExpand(
		QSize outer,
		QSize original,
		int minVisibleNominator,
		int minVisibleDenominator) {
	const auto min = std::min({
		outer.width(),
		outer.height(),
		original.width(),
		original.height(),
	});
	// Count for: (nominator / denominator) - (1 / min).
	// In case the result is less than 1 / 2, just return.
	if (2 * minVisibleNominator * min
		< 2 * minVisibleDenominator + minVisibleDenominator * min) {
		return false;
	}
	return DecideFrameResize(
		outer,
		original,
		minVisibleNominator * min - minVisibleDenominator,
		minVisibleDenominator * min).expanding;
}

ExpandDecision DecideVideoFrameResize(QSize outer, QSize original) {
	return DecideFrameResize(outer, original, 1, 2);
}

QSize CalculateResizeFromOuter(QSize outer, QSize original) {
	return DecideVideoFrameResize(outer, original).result;
}

QImage PrepareByRequest(
		const QImage &original,
		bool hasAlpha,
		const AVRational &aspect,
		int rotation,
		const FrameRequest &request,
		QImage storage) {
	Expects(!request.outer.isEmpty() || hasAlpha);

	const auto outer = request.outer.isEmpty()
		? original.size()
		: request.outer;
	if (!FFmpeg::GoodStorageForFrame(storage, outer)) {
		storage = FFmpeg::CreateFrameStorage(outer);
	}

	if (hasAlpha && request.keepAlpha) {
		storage.fill(Qt::transparent);
	}

	QPainter p(&storage);
	PaintFrameContent(p, original, hasAlpha, aspect, rotation, request);
	p.end();

	ApplyFrameRounding(storage, request);
	if (request.colored.alpha() != 0) {
		storage = Images::Colored(std::move(storage), request.colored);
	}
	return storage;
}

} // namespace Streaming
} // namespace Media
