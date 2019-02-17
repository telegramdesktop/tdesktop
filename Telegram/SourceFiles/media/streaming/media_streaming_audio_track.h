/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/streaming/media_streaming_utility.h"

namespace Media {
namespace Player {
struct TrackState;
} // namespace Player

namespace Streaming {

class AudioTrack final {
public:
	// Called from some unspecified thread.
	// Callbacks are assumed to be thread-safe.
	AudioTrack(
		Stream &&stream,
		FnMut<void(const Information &)> ready,
		Fn<void()> error);

	// Called from the main thread.
	// Must be called after 'ready' was invoked.
	void start();

	// Called from the main thread.
	// Non-const, because we subscribe to changes on the first call.
	// Must be called after 'ready' was invoked.
	[[nodiscard]] rpl::producer<TrackState, Error> state();

	// Thread-safe.
	[[nodiscard]] int streamIndex() const;
	[[nodiscard]] AVRational streamTimeBase() const;

	// Called from the same unspecified thread.
	void process(Packet &&packet);

	// Called from the main thread.
	~AudioTrack();

private:
	// Called from the same unspecified thread.
	[[nodiscard]] bool tryReadFirstFrame(Packet &&packet);
	[[nodiscard]] bool fillStateFromFrame();
	void mixerInit();
	void mixerEnqueue(Packet &&packet);
	void callReady();

	// Accessed from the same unspecified thread.
	Stream _stream;

	// Assumed to be thread-safe.
	FnMut<void(const Information &)> _ready;
	const Fn<void()> _error;

	// First set from the same unspecified thread before _ready is called.
	// After that is immutable.
	AudioMsgId _audioMsgId;
	TrackState _state;

	// Accessed from the main thread.
	base::Subscription _subscription;
	rpl::event_stream<TrackState, Error> _stateChanges;

};

} // namespace Streaming
} // namespace Media
