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

constexpr auto kFrameDisplayTimeAlreadyDone
	= std::numeric_limits<crl::time>::max();

class VideoTrackObject;
class Instance;

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
	void process(std::vector<FFmpeg::Packet> &&packets);
	void waitForData();

	// Called from the main thread.
	// Must be called after 'ready' was invoked.
	void pause(crl::time time);
	void resume(crl::time time);

	// Called from the main thread.
	void setSpeed(float64 speed);
	void setWaitForMarkAsShown(bool wait);

	// Called from the main thread.
	// Returns the position of the displayed frame.
	[[nodiscard]] crl::time markFrameDisplayed(crl::time now);
	void addTimelineDelay(crl::time delayed);
	bool markFrameShown();
	[[nodiscard]] crl::time nextFrameDisplayTime() const;
	[[nodiscard]] QImage frame(
		const FrameRequest &request,
		const Instance *instance);
	[[nodiscard]] FrameWithInfo frameWithInfo(const Instance *instance);
	[[nodiscard]] QImage currentFrameImage();
	void unregisterInstance(not_null<const Instance*> instance);
	[[nodiscard]] rpl::producer<> checkNextFrame() const;
	[[nodiscard]] rpl::producer<> waitingForData() const;

	// Called from the main thread.
	~VideoTrack();

private:
	friend class VideoTrackObject;

	struct Prepared {
		Prepared(const FrameRequest &request) : request(request) {
		}

		FrameRequest request = FrameRequest::NonStrict();
		QImage image;
	};
	struct Frame {
		FFmpeg::FramePointer decoded = FFmpeg::MakeFramePointer();
		QImage original;
		FrameYUV420 yuv420;
		crl::time position = kTimeUnknown;
		crl::time displayed = kTimeUnknown;
		crl::time display = kTimeUnknown;
		FrameFormat format = FrameFormat::None;

		base::flat_map<const Instance*, Prepared> prepared;

		bool alpha = false;
	};
	struct FrameWithIndex {
		not_null<Frame*> frame;
		int index = -1;
	};

	class Shared {
	public:
		using PrepareFrame = not_null<Frame*>;
		using PrepareNextCheck = crl::time;
		using PrepareState = std::variant<
			v::null_t,
			PrepareFrame,
			PrepareNextCheck>;
		struct PresentFrame {
			crl::time displayPosition = kTimeUnknown;
			crl::time nextCheckDelay = 0;
			crl::time addedWorldTimeDelay = 0;
		};

		// Called from the wrapped object queue.
		void init(QImage &&cover, crl::time position);
		[[nodiscard]] bool initialized() const;

		[[nodiscard]] PrepareState prepareState(
			crl::time trackTime,
			bool dropStaleFrames);

		[[nodiscard]] PresentFrame presentFrame(
			not_null<VideoTrackObject*> object,
			TimePoint trackTime,
			float64 playbackSpeed,
			bool dropStaleFrames);
		[[nodiscard]] bool firstPresentHappened() const;

		// Called from the main thread.
		// Returns the position of the displayed frame.
		[[nodiscard]] crl::time markFrameDisplayed(crl::time now);
		void addTimelineDelay(crl::time delayed);
		bool markFrameShown();
		[[nodiscard]] crl::time nextFrameDisplayTime() const;
		[[nodiscard]] not_null<Frame*> frameForPaint();
		[[nodiscard]] FrameWithIndex frameForPaintWithIndex();

	private:
		[[nodiscard]] not_null<Frame*> getFrame(int index);
		[[nodiscard]] not_null<const Frame*> getFrame(int index) const;
		[[nodiscard]] int counter() const;

		static constexpr auto kCounterUninitialized = -1;
		std::atomic<int> _counter = kCounterUninitialized;

		// Main thread.
		int _counterCycle = 0;

		static constexpr auto kFramesCount = 4;
		std::array<Frame, kFramesCount> _frames;

		// (_counter % 2) == 1 main thread can write _delay.
		// (_counter % 2) == 0 crl::queue can read _delay.
		crl::time _delay = kTimeUnknown;

	};

	static void PrepareFrameByRequests(not_null<Frame*> frame, int rotation);
	[[nodiscard]] static bool IsDecoded(not_null<const Frame*> frame);
	[[nodiscard]] static bool IsRasterized(not_null<const Frame*> frame);
	[[nodiscard]] static bool IsStale(
		not_null<const Frame*> frame,
		crl::time trackTime);

	const int _streamIndex = 0;
	const AVRational _streamTimeBase;
	const crl::time _streamDuration = 0;
	const int _streamRotation = 0;
	//AVRational _streamAspect = kNormalAspect;
	std::unique_ptr<Shared> _shared;

	using Implementation = VideoTrackObject;
	crl::object_on_queue<Implementation> _wrapped;

};

} // namespace Streaming
} // namespace Media
