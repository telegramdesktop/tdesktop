/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rect_part.h"

enum class ImageRoundRadius;

namespace Media {

inline constexpr auto kTimeUnknown = std::numeric_limits<crl::time>::min();
inline constexpr auto kDurationMax = crl::time(std::numeric_limits<int>::max());
inline constexpr auto kDurationUnavailable = std::numeric_limits<crl::time>::max();

namespace Audio {
bool SupportsSpeedControl();
} // namespace Audio

namespace Streaming {

inline bool SupportsSpeedControl() {
	return Media::Audio::SupportsSpeedControl();
}

class VideoTrack;
class AudioTrack;

enum class Mode {
	Both,
	Audio,
	Video,
	Inspection,
};

struct PlaybackOptions {
	Mode mode = Mode::Both;
	crl::time position = 0;
	float64 speed = 1.; // Valid values between 0.5 and 2.
	AudioMsgId audioId;
	bool syncVideoByAudio = true;
	bool waitForMarkAsShown = false;
	bool loop = false;
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
	int headerSize = 0;
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
	bool waiting = false;
};

struct MutedByOther {
};

struct Finished {
};

struct Update {
	std::variant<
		Information,
		PreloadedVideo,
		UpdateVideo,
		PreloadedAudio,
		UpdateAudio,
		WaitingForData,
		MutedByOther,
		Finished> data;
};

enum class Error {
	OpenFailed,
	LoadFailed,
	InvalidData,
	NotStreamable,
};

struct FrameRequest {
	QSize resize;
	QSize outer;
	ImageRoundRadius radius = ImageRoundRadius();
	RectParts corners = RectPart::AllCorners;
	bool requireARGB32 = true;
	bool strict = true;

	static FrameRequest NonStrict() {
		auto result = FrameRequest();
		result.strict = false;
		return result;
	}

	[[nodiscard]] bool empty() const {
		return resize.isEmpty();
	}

	[[nodiscard]] bool operator==(const FrameRequest &other) const {
		return (resize == other.resize)
			&& (outer == other.outer)
			&& (radius == other.radius)
			&& (corners == other.corners)
			&& (requireARGB32 == other.requireARGB32);
	}
	[[nodiscard]] bool operator!=(const FrameRequest &other) const {
		return !(*this == other);
	}

	[[nodiscard]] bool goodFor(const FrameRequest &other) const {
		return (requireARGB32 == other.requireARGB32)
			&& ((*this == other) || (strict && !other.strict));
	}
};

enum class FrameFormat {
	None,
	ARGB32,
	YUV420,
};

struct FrameChannel {
	const void *data = nullptr;
	int stride = 0;
};

struct FrameYUV420 {
	QSize size;
	QSize chromaSize;
	FrameChannel y;
	FrameChannel u;
	FrameChannel v;
};

struct FrameWithInfo {
	QImage original;
	FrameYUV420 *yuv420 = nullptr;
	FrameFormat format = FrameFormat::None;
	int index = -1;
};

} // namespace Streaming
} // namespace Media
