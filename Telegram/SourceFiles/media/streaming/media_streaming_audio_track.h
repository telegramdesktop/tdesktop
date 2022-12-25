/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/streaming/media_streaming_utility.h"

namespace Media {
namespace Streaming {

class AudioTrack final {
public:
	// Called from some unspecified thread.
	// Callbacks are assumed to be thread-safe.
	AudioTrack(
		const PlaybackOptions &options,
		Stream &&stream,
		AudioMsgId audioId,
		FnMut<void(const Information &)> ready,
		Fn<void(Error)> error);

	// Called from the main thread.
	// Must be called after 'ready' was invoked.
	void pause(crl::time time);
	void resume(crl::time time);

	// Allow to irreversibly stop only audio track.
	void stop();

	// Called from the main thread.
	void setSpeed(float64 speed);
	[[nodiscard]] rpl::producer<> waitingForData() const;

	// Called from the main thread.
	// Non-const, because we subscribe to changes on the first call.
	// Must be called after 'ready' was invoked.
	[[nodiscard]] rpl::producer<crl::time> playPosition();

	// Thread-safe.
	[[nodiscard]] int streamIndex() const;
	[[nodiscard]] AVRational streamTimeBase() const;
	[[nodiscard]] crl::time streamDuration() const;

	// Called from the same unspecified thread.
	void process(std::vector<FFmpeg::Packet> &&packets);
	void waitForData();

	// Called from the main thread.
	~AudioTrack();

private:
	// Called from the same unspecified thread.
	[[nodiscard]] bool initialized() const;
	[[nodiscard]] bool tryReadFirstFrame(FFmpeg::Packet &&packet);
	[[nodiscard]] bool fillStateFromFrame();
	[[nodiscard]] bool processFirstFrame();
	void mixerInit();
	void mixerEnqueue(gsl::span<FFmpeg::Packet> packets);
	void mixerForceToBuffer();
	void callReady();

	PlaybackOptions _options;

	// Accessed from the same unspecified thread.
	Stream _stream;
	const AudioMsgId _audioId;
	bool _readTillEnd = false;

	// Assumed to be thread-safe.
	FnMut<void(const Information &)> _ready;
	const Fn<void(Error)> _error;

	// First set from the same unspecified thread before _ready is called.
	// After that is immutable.
	crl::time _startedPosition = kTimeUnknown;

	// Accessed from the main thread.
	rpl::lifetime _subscription;
	rpl::event_stream<> _waitingForData;
	// First set from the same unspecified thread before _ready is called.
	// After that accessed from the main thread.
	rpl::variable<crl::time> _playPosition;

	// For initial frame skipping for an exact seek.
	FFmpeg::FramePointer _initialSkippingFrame;

};

} // namespace Streaming
} // namespace Media
