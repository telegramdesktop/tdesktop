/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/streaming/media_streaming_common.h"
#include "ffmpeg/ffmpeg_utility.h"

namespace Media {
namespace Streaming {

struct TimePoint {
	crl::time trackTime = kTimeUnknown;
	crl::time worldTime = kTimeUnknown;

	bool valid() const {
		return (trackTime != kTimeUnknown) && (worldTime != kTimeUnknown);
	}
	explicit operator bool() const {
		return valid();
	}
};

struct Stream {
	int index = -1;
	crl::time duration = kTimeUnknown;
	AVRational timeBase = FFmpeg::kUniversalTimeBase;
	FFmpeg::CodecPointer codec;
	FFmpeg::FramePointer decodedFrame;
	FFmpeg::FramePointer transferredFrame;
	std::deque<FFmpeg::Packet> queue;
	int invalidDataPackets = 0;

	// Audio only.
	int frequency = 0;

	// Video only.
	int rotation = 0;
	AVRational aspect = FFmpeg::kNormalAspect;
	FFmpeg::SwscalePointer swscale;
};

[[nodiscard]] crl::time FramePosition(const Stream &stream);
[[nodiscard]] FFmpeg::AvErrorWrap ProcessPacket(
	Stream &stream,
	FFmpeg::Packet &&packet);
[[nodiscard]] FFmpeg::AvErrorWrap ReadNextFrame(Stream &stream);

[[nodiscard]] bool GoodForRequest(
	const QImage &image,
	bool hasAlpha,
	int rotation,
	const FrameRequest &request);
[[nodiscard]] bool TransferFrame(
	Stream &stream,
	not_null<AVFrame*> decodedFrame,
	not_null<AVFrame*> transferredFrame);
[[nodiscard]] QImage ConvertFrame(
	Stream &stream,
	not_null<AVFrame*> frame,
	QSize resize,
	QImage storage);
[[nodiscard]] FrameYUV ExtractYUV(Stream &stream, AVFrame *frame);

struct ExpandDecision {
	QSize result;
	bool expanding = false;
};
[[nodiscard]] ExpandDecision DecideFrameResize(
	QSize outer,
	QSize original,
	int minVisibleNominator = 3, // If we cut out no more than 0.25 of
	int minVisibleDenominator = 4); // the original, let's expand.
[[nodiscard]] ExpandDecision DecideVideoFrameResize(
	QSize outer,
	QSize original);
[[nodiscard]] QSize CalculateResizeFromOuter(QSize outer, QSize original);
[[nodiscard]] QImage PrepareBlurredBackground(QSize outer, QImage frame);
void FillBlurredBackground(QPainter &p, QSize outer, QImage bg);
[[nodiscard]] QImage PrepareByRequest(
	const QImage &original,
	bool hasAlpha,
	const AVRational &aspect,
	int rotation,
	const FrameRequest &request,
	QImage storage);

} // namespace Streaming
} // namespace Media
