/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/streaming/media_streaming_utility.h"

#include <crl/crl_object_on_queue.h>

namespace Media {
namespace Streaming {

class VideoTrackObject;

class VideoTrack final {
public:
	// Called from some unspecified thread.
	// Callbacks are assumed to be thread-safe.
	VideoTrack(
		Stream &&stream,
		FnMut<void(const Information &)> ready,
		Fn<void()> error);

	// Thread-safe.
	[[nodiscard]] int streamIndex() const;
	[[nodiscard]] AVRational streamTimeBase() const;

	// Called from the same unspecified thread.
	void process(Packet &&packet);

	// Called from the main thread.
	void start();
	// Returns the position of the displayed frame.
	[[nodiscard]] crl::time markFrameDisplayed(crl::time now);
	[[nodiscard]] QImage frame(const FrameRequest &request) const;
	[[nodiscard]] rpl::producer<crl::time> renderNextFrame() const;

	// Called from the main thread.
	~VideoTrack();

private:
	friend class VideoTrackObject;

	struct Frame {
		QImage original;
		crl::time position = kTimeUnknown;
		crl::time displayPosition = kTimeUnknown;
		crl::time displayed = kTimeUnknown;

		FrameRequest request;
		QImage prepared;
	};

	class Shared {
	public:
		using PrepareFrame = not_null<Frame*>;
		using PrepareNextCheck = crl::time;
		using PrepareState = base::optional_variant<
			PrepareFrame,
			PrepareNextCheck>;
		struct PresentFrame {
			crl::time displayPosition = kTimeUnknown;
			crl::time nextCheckDelay = 0;
		};

		// Called from the wrapped object queue.
		void init(QImage &&cover, crl::time position);
		[[nodiscard]] bool initialized() const;

		[[nodiscard]] PrepareState prepareState(crl::time trackTime);
		[[nodiscard]] PresentFrame presentFrame(crl::time trackTime);

		// Called from the main thread.
		// Returns the position of the displayed frame.
		[[nodiscard]] crl::time markFrameDisplayed(crl::time now);
		[[nodiscard]] not_null<Frame*> frameForPaint();

	private:
		[[nodiscard]] not_null<Frame*> getFrame(int index);
		[[nodiscard]] static bool IsPrepared(not_null<Frame*> frame);
		[[nodiscard]] static bool IsStale(
			not_null<Frame*> frame,
			crl::time trackTime);
		[[nodiscard]] int counter() const;

		static constexpr auto kCounterUninitialized = -1;
		std::atomic<int> _counter = kCounterUninitialized;

		static constexpr auto kFramesCount = 4;
		std::array<Frame, kFramesCount> _frames;

	};

	const int _streamIndex = 0;
	const AVRational _streamTimeBase;
	//const int _streamRotation = 0;
	std::unique_ptr<Shared> _shared;

	using Implementation = VideoTrackObject;
	crl::object_on_queue<Implementation> _wrapped;

};

} // namespace Streaming
} // namespace Media
