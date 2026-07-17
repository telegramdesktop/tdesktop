/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Media::Encode {

struct AnimatedEntity {
	enum class Kind {
		Lottie,
		Webm,
	};
	Kind kind = Kind::Lottie;
	QByteArray bytes;
	QRectF geometry;
	float64 rotation = 0.;
	bool flipped = false;
};

using Layer = std::variant<QImage, AnimatedEntity>;

struct StillSource {
	QImage base;
	crl::time duration = 0;
	float64 fps = 30.;
};

struct VideoSource {
	QByteArray bytes;
	int targetShorterSide = 0;
};

struct Job {
	std::variant<VideoSource, StillSource> source;
	std::vector<Layer> overlay;
	std::vector<uint64> attachedStickerIds;
	int bitrate = 0;
	bool silentLoop = false;
};

struct Result {
	QByteArray bytes;
	QSize dimensions;
	crl::time duration = 0;

	[[nodiscard]] bool empty() const {
		return bytes.isEmpty();
	}
};

[[nodiscard]] Result Run(Job &&job, Fn<bool(float64)> progress = nullptr);

[[nodiscard]] int CompressedShorterSide(QSize original, int64 size);

[[nodiscard]] QSize DownscaledSize(QSize original, int targetShorterSide);

[[nodiscard]] QByteArray TranscodeVideoToMp4(
	const QByteArray &source,
	int targetShorterSide,
	Fn<bool(float64)> progress = nullptr);

} // namespace Media::Encode
