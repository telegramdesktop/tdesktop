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
	FFmpeg::FramePointer frame;
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
	int rotation,
	const FrameRequest &request);
[[nodiscard]] QImage ConvertFrame(
	Stream &stream,
	AVFrame *frame,
	QSize resize,
	QImage storage);
[[nodiscard]] FrameYUV420 ExtractYUV420(Stream &stream, AVFrame *frame);
[[nodiscard]] QImage PrepareByRequest(
	const QImage &original,
	bool alpha,
	int rotation,
	const FrameRequest &request,
	QImage storage);

} // namespace Streaming
} // namespace Media
