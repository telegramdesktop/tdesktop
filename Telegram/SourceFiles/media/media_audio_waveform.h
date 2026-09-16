/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Media::Audio {

struct Waveform {
	std::vector<uint16> bars;
	uint16 peak = 0;

	[[nodiscard]] bool empty() const {
		return bars.empty();
	}
};

[[nodiscard]] Waveform LoadWaveform(
	const QString &path,
	const QByteArray &bytes,
	int count,
	std::shared_ptr<std::atomic<bool>> cancel = nullptr);

} // namespace Media::Audio
