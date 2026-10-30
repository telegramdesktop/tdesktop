/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ffmpeg/ffmpeg_utility.h"

namespace Media::Streaming {

struct FrameYUV;

struct FrameColor {
	enum class Transfer : uchar {
		SDR,
		PQ,
		HLG,
	};
	enum class Matrix : uchar {
		BT601,
		BT709,
		BT2020,
	};

	Transfer transfer = Transfer::SDR;
	Matrix matrix = Matrix::BT601;
	bool wideGamut = false;
	bool fullRange = false;
	int peak = 0;

	[[nodiscard]] bool hdr() const {
		return (transfer != Transfer::SDR);
	}
};

[[nodiscard]] bool WideGamutPrimaries(AVColorPrimaries primaries);
[[nodiscard]] FrameColor ReadFrameColor(
	not_null<const AVFrame*> frame,
	int &peak);
[[nodiscard]] FrameYUV ExtractYUV(not_null<const AVFrame*> frame);

struct ColorUniforms {
	std::array<float, 16> yuvToRgb = {};
	std::array<float, 4> hdr = {};
};
[[nodiscard]] ColorUniforms PrepareColorUniforms(
	const FrameColor &color,
	bool nv12,
	bool highBitDepth);

[[nodiscard]] bool NeedsToneMapping(
	AVColorTransferCharacteristic transfer,
	int format);
[[nodiscard]] bool ConvertFrameToARGB32(
	not_null<const AVFrame*> frame,
	int format,
	const FrameColor &color,
	QImage &storage,
	FFmpeg::SwscalePointer &swscale);
[[nodiscard]] QImage ConvertYUVToARGB32(
	const FrameYUV &yuv,
	bool nv12,
	const FrameColor &color);

} // namespace Media::Streaming
