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
		Fn<void()> error);

	// Called from the main thread.
	// Must be called after 'ready' was invoked.
	void start(crl::time startTime);

	// Called from the main thread.
	// Non-const, because we subscribe to changes on the first call.
	// Must be called after 'ready' was invoked.
	[[nodiscard]] rpl::producer<crl::time> playPosition();

	// Thread-safe.
	[[nodiscard]] int streamIndex() const;
	[[nodiscard]] AVRational streamTimeBase() const;

	// Called from the same unspecified thread.
	void process(Packet &&packet);

	// Called from the main thread.
	~AudioTrack();

private:
	// Called from the same unspecified thread.
	[[nodiscard]] bool initialized() const;
	[[nodiscard]] bool tryReadFirstFrame(Packet &&packet);
	[[nodiscard]] bool fillStateFromFrame();
	void mixerInit();
	void mixerEnqueue(Packet &&packet);
	void callReady();

	const PlaybackOptions _options;

	// Accessed from the same unspecified thread.
	Stream _stream;
	const AudioMsgId _audioId;
	bool _noMoreData = false;

	// Assumed to be thread-safe.
	FnMut<void(const Information &)> _ready;
	const Fn<void()> _error;

	// First set from the same unspecified thread before _ready is called.
	// After that is immutable.
	crl::time _startedPosition = kTimeUnknown;

	// Accessed from the main thread.
	base::Subscription _subscription;
	rpl::variable<crl::time> _playPosition;

};

} // namespace Streaming
} // namespace Media
