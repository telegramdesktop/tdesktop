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
		const PlaybackOptions &options,
		Stream &&stream,
		const AudioMsgId &audioId,
		FnMut<void(const Information &)> ready,
		Fn<void(Error)> error);

	// Thread-safe.
	[[nodiscard]] int streamIndex() const;
	[[nodiscard]] AVRational streamTimeBase() const;
	[[nodiscard]] crl::time streamDuration() const;

	// Called from the same unspecified thread.
	void process(Packet &&packet);
	void waitForData();

	// Called from the main thread.
	// Must be called after 'ready' was invoked.
	void pause(crl::time time);
	void resume(crl::time time);

	// Called from the main thread.
	void setSpeed(float64 speed);

	// Called from the main thread.
	// Returns the position of the displayed frame.
	[[nodiscard]] crl::time markFrameDisplayed(crl::time now);
	[[nodiscard]] crl::time nextFrameDisplayTime() const;
	[[nodiscard]] QImage frame(const FrameRequest &request);
	[[nodiscard]] rpl::producer<> checkNextFrame() const;
	[[nodiscard]] rpl::producer<> waitingForData() const;

	// Called from the main thread.
	~VideoTrack();

private:
	friend class VideoTrackObject;

	struct Frame {
		FramePointer decoded = MakeFramePointer();
		QImage original;
		crl::time position = kTimeUnknown;
		crl::time displayed = kTimeUnknown;
		crl::time display = kTimeUnknown;

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

		[[nodiscard]] PrepareState prepareState(
			crl::time trackTime,
			bool dropStaleFrames);

		// RasterizeCallback(not_null<Frame*>).
		template <typename RasterizeCallback>
		[[nodiscard]] PresentFrame presentFrame(
			TimePoint trackTime,
			float64 playbackSpeed,
			bool dropStaleFrames,
			RasterizeCallback &&rasterize);
		[[nodiscard]] bool firstPresentHappened() const;

		// Called from the main thread.
		// Returns the position of the displayed frame.
		[[nodiscard]] crl::time markFrameDisplayed(crl::time now);
		[[nodiscard]] crl::time nextFrameDisplayTime() const;
		[[nodiscard]] not_null<Frame*> frameForPaint();

	private:
		[[nodiscard]] not_null<Frame*> getFrame(int index);
		[[nodiscard]] not_null<const Frame*> getFrame(int index) const;
		[[nodiscard]] int counter() const;

		static constexpr auto kCounterUninitialized = -1;
		std::atomic<int> _counter = kCounterUninitialized;

		static constexpr auto kFramesCount = 4;
		std::array<Frame, kFramesCount> _frames;

	};

	static QImage PrepareFrameByRequest(
		not_null<Frame*> frame,
		bool useExistingPrepared = false);
	[[nodiscard]] static bool IsDecoded(not_null<const Frame*> frame);
	[[nodiscard]] static bool IsRasterized(not_null<const Frame*> frame);
	[[nodiscard]] static bool IsStale(
		not_null<const Frame*> frame,
		crl::time trackTime);

	const int _streamIndex = 0;
	const AVRational _streamTimeBase;
	const crl::time _streamDuration = 0;
	//const int _streamRotation = 0;
	//AVRational _streamAspect = kNormalAspect;
	std::unique_ptr<Shared> _shared;

	using Implementation = VideoTrackObject;
	crl::object_on_queue<Implementation> _wrapped;

};

} // namespace Streaming
} // namespace Media
