/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_audio_msg_id.h"
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
	bool hwAllow = false;
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
	bool alpha = false;
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
	QColor colored = QColor(0, 0, 0, 0);
	bool requireARGB32 = true;
	bool keepAlpha = false;
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
			&& (colored == other.colored)
			&& (keepAlpha == other.keepAlpha)
			&& (requireARGB32 == other.requireARGB32);
	}
	[[nodiscard]] bool operator!=(const FrameRequest &other) const {
		return !(*this == other);
	}

	[[nodiscard]] bool goodFor(const FrameRequest &other) const {
		return (requireARGB32 == other.requireARGB32)
			&& (keepAlpha == other.keepAlpha)
			&& (colored == other.colored)
			&& ((strict && !other.strict) || (*this == other));
	}
};

enum class FrameFormat {
	None,
	ARGB32,
	YUV420,
	NV12,
};

struct FrameChannel {
	const void *data = nullptr;
	int stride = 0;
};

struct FrameYUV {
	QSize size;
	QSize chromaSize;
	FrameChannel y;
	FrameChannel u;
	FrameChannel v;
};

struct FrameWithInfo {
	QImage image;
	FrameYUV *yuv = nullptr;
	FrameFormat format = FrameFormat::None;
	int index = -1;
	bool alpha = false;
};

} // namespace Streaming
} // namespace Media
