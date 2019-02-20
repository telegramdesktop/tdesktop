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

class VideoTrack;
class AudioTrack;

enum class Mode {
	Both,
	Audio,
	Video,
	Inspection,
};

struct TrackState {
	crl::time position = kTimeUnknown;
	crl::time receivedTill = kTimeUnknown;
	crl::time duration = kTimeUnknown;
};

struct VideoInformation {
	TrackState state;
	QSize size;
	QImage cover;
	int rotation = 0;
};

struct AudioInformation {
	TrackState state;
};

struct Information {
	VideoInformation video;
	AudioInformation audio;
};

template <typename Track>
struct PreloadedUpdate {
	crl::time till = kTimeUnknown;
};

template <typename Track>
struct PlaybackUpdate {
	crl::time position = kTimeUnknown;
};

using PreloadedVideo = PreloadedUpdate<VideoTrack>;
using UpdateVideo = PlaybackUpdate<VideoTrack>;
using PreloadedAudio = PreloadedUpdate<AudioTrack>;
using UpdateAudio = PlaybackUpdate<AudioTrack>;

struct WaitingForData {
};

struct MutedByOther {
};

struct Update {
	base::variant<
		Information,
		PreloadedVideo,
		UpdateVideo,
		PreloadedAudio,
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
