/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Media::Encode {

[[nodiscard]] int CompressedShorterSide(QSize original, int64 size);

[[nodiscard]] QSize DownscaledSize(QSize original, int targetShorterSide);

[[nodiscard]] QByteArray TranscodeVideoToMp4(
	const QByteArray &source,
	int targetShorterSide,
	Fn<bool(float64)> progress = nullptr);

} // namespace Media::Encode
