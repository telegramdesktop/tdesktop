/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Media {
namespace Streaming {

constexpr auto kTimeUnknown = crl::time(-1);

enum class Mode {
	Both,
	Audio,
	Video,
	Inspection,
};

struct Information {
	crl::time videoStarted = kTimeUnknown;
	crl::time videoDuration = kTimeUnknown;
	QSize videoSize;
	QImage videoCover;
	int videoCoverRotation = 0;

	crl::time audioStarted = kTimeUnknown;
	crl::time audioDuration = kTimeUnknown;
};

struct RepaintRequest {
	crl::time position;
};

struct WaitingForData {
};

struct MutedByOther {
};

struct Update {
	base::variant<
		Information,
		RepaintRequest,
		WaitingForData,
		MutedByOther> data;
};

struct Error {
};

} // namespace Streaming
} // namespace Media
