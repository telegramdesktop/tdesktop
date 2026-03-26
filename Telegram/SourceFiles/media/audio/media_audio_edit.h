/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/audio/media_audio.h"

namespace Media {

struct AudioEditResult {
	QByteArray content;
	VoiceWaveform waveform;
	crl::time duration = 0;
};

[[nodiscard]] AudioEditResult TrimAudioToRange(
	const QByteArray &content,
	crl::time from,
	crl::time till);

[[nodiscard]] AudioEditResult ConcatAudio(
	const QByteArray &first,
	const QByteArray &second);

} // namespace Media
