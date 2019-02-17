/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Media {
namespace Streaming {

constexpr auto kTimeUnknown = std::numeric_limits<crl::time>::min();

enum class Mode {
	Both,
	Audio,
	Video,
	Inspection,
};

struct TrackState {
	crl::time position = kTimeUnknown;
	crl::time receivedTill = kTimeUnknown;
};

struct State {
	TrackState video;
	TrackState audio;
};

struct Information {
	State state;

	crl::time videoDuration = kTimeUnknown;
	QSize videoSize;
	QImage videoCover;
	int videoRotation = 0;

	crl::time audioDuration = kTimeUnknown;
};

struct UpdateVideo {
	crl::time position = 0;
};

struct UpdateAudio {
	crl::time position = 0;
};

struct WaitingForData {
};

struct MutedByOther {
};

struct Update {
	base::variant<
		Information,
		UpdateVideo,
		UpdateAudio,
		WaitingForData,
		MutedByOther> data;
};

struct Error {
};

struct FrameRequest {
	QSize resize;
	QSize outer;
	ImageRoundRadius radius = ImageRoundRadius();
	RectParts corners = RectPart::AllCorners;

	bool empty() const {
		return resize.isEmpty();
	}

	bool operator==(const FrameRequest &other) const {
		return (resize == other.resize)
			&& (outer == other.outer)
			&& (radius == other.radius)
			&& (corners == other.corners);
	}
	bool operator!=(const FrameRequest &other) const {
		return !(*this == other);
	}
};

} // namespace Streaming
} // namespace Media
