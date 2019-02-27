/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_video_track.h"

#include "media/audio/media_audio.h"
#include "base/concurrent_timer.h"

namespace Media {
namespace Streaming {
namespace {

constexpr auto kDisplaySkipped = crl::time(-1);
static_assert(kDisplaySkipped != kTimeUnknown);

} // namespace

class VideoTrackObject final {
public:
	using Frame = VideoTrack::Frame;
	using Shared = VideoTrack::Shared;

	VideoTrackObject(
		crl::weak_on_queue<VideoTrackObject> weak,
		const PlaybackOptions &options,
		not_null<Shared*> shared,
		Stream &&stream,
		const AudioMsgId &audioId,
		FnMut<void(const Information &)> ready,
		Fn<void()> error);

	void process(Packet &&packet);

	[[nodisacrd]] rpl::producer<crl::time> displayFrameAt() const;
	[[nodisacrd]] rpl::producer<> waitingForData() const;

	void pause(crl::time time);
	void resume(crl::time time);
	void setSpeed(float64 speed);
	void interrupt();
	void frameDisplayed();
	void updateFrameRequest(const FrameRequest &request);

private:
	[[nodiscard]] bool interrupted() const;
	[[nodiscard]] bool tryReadFirstFrame(Packet &&packet);
	[[nodiscard]] bool fillStateFromFrame();
	[[nodiscard]] bool processFirstFrame();
	void queueReadFrames(crl::time delay = 0);
	void readFrames();
	[[nodiscard]] bool readFrame(not_null<Frame*> frame);
	void presentFrameIfNeeded();
	void callReady();

	// Force frame position to be clamped to [0, duration] and monotonic.
	[[nodiscard]] crl::time currentFramePosition() const;

	[[nodiscard]] TimePoint trackTime() const;

	const crl::weak_on_queue<VideoTrackObject> _weak;
	PlaybackOptions _options;

	// Main thread wrapper destructor will set _shared back to nullptr.
	// All queued method calls after that should be discarded.
	Shared *_shared = nullptr;

	Stream _stream;
	AudioMsgId _audioId;
	bool _noMoreData = false;
	FnMut<void(const Information &)> _ready;
	Fn<void()> _error;
	crl::time _pausedTime = kTimeUnknown;
	crl::time _resumedTime = kTimeUnknown;
	mutable TimePoint _syncTimePoint;
	mutable crl::time _previousFramePosition = kTimeUnknown;
	crl::time _nextFrameDisplayTime = kTimeUnknown;
	rpl::event_stream<crl::time> _nextFrameTimeUpdates;
	rpl::event_stream<> _waitingForData;
	FrameRequest _request;

	bool _queued = false;
	base::ConcurrentTimer _readFramesTimer;

	// For initial frame skipping for an exact seek.
	FramePointer _initialSkippingFrame;

};

VideoTrackObject::VideoTrackObject(
	crl::weak_on_queue<VideoTrackObject> weak,
	const PlaybackOptions &options,
	not_null<Shared*> shared,
	Stream &&stream,
	const AudioMsgId &audioId,
	FnMut<void(const Information &)> ready,
	Fn<void()> error)
: _weak(std::move(weak))
, _options(options)
, _shared(shared)
, _stream(std::move(stream))
, _audioId(audioId)
, _ready(std::move(ready))
, _error(std::move(error))
, _readFramesTimer(_weak, [=] { readFrames(); }) {
	Expects(_ready != nullptr);
	Expects(_error != nullptr);
}

rpl::producer<crl::time> VideoTrackObject::displayFrameAt() const {
	return interrupted()
		? rpl::complete<crl::time>()
		: (_nextFrameDisplayTime == kTimeUnknown)
		? _nextFrameTimeUpdates.events()
		: _nextFrameTimeUpdates.events_starting_with_copy(
			_nextFrameDisplayTime);
}

rpl::producer<> VideoTrackObject::waitingForData() const {
	return interrupted() ? rpl::never() : _waitingForData.events();
}

void VideoTrackObject::process(Packet &&packet) {
	if (interrupted()) {
		return;
	}
	_noMoreData = packet.empty();
	if (_shared->initialized()) {
		_stream.queue.push_back(std::move(packet));
		queueReadFrames();
	} else if (!tryReadFirstFrame(std::move(packet))) {
		_error();
	}
}

void VideoTrackObject::queueReadFrames(crl::time delay) {
	if (delay > 0) {
		_readFramesTimer.callOnce(delay);
	} else if (!_queued) {
		_queued = true;
		_weak.with([](VideoTrackObject &that) {
			that._queued = false;
			that.readFrames();
		});
	}
}

void VideoTrackObject::readFrames() {
	if (interrupted()) {
		return;
	}
	const auto time = trackTime().trackTime;
	const auto dropStaleFrames = _options.dropStaleFrames;
	const auto state = _shared->prepareState(time, dropStaleFrames);
	state.match([&](Shared::PrepareFrame frame) {
		while (readFrame(frame)) {
			if (!dropStaleFrames || !VideoTrack::IsStale(frame, time)) {
				presentFrameIfNeeded();
				break;
			}
		}
	}, [&](Shared::PrepareNextCheck delay) {
		Expects(delay > 0);

		queueReadFrames(delay);
	}, [&](std::nullopt_t) {
		presentFrameIfNeeded();
	});
}

bool VideoTrackObject::readFrame(not_null<Frame*> frame) {
	if (const auto error = ReadNextFrame(_stream)) {
		if (error.code() == AVERROR_EOF) {
			interrupt();
			_nextFrameTimeUpdates = rpl::event_stream<crl::time>();
		} else if (error.code() != AVERROR(EAGAIN) || _noMoreData) {
			interrupt();
			_error();
		} else if (_stream.queue.empty()) {
			_waitingForData.fire({});
		}
		return false;
	}
	const auto position = currentFramePosition();
	if (position == kTimeUnknown) {
		interrupt();
		_error();
		return false;
	}
	frame->position = position;
	frame->displayed = kTimeUnknown;
	return true;
}

void VideoTrackObject::presentFrameIfNeeded() {
	if (_pausedTime != kTimeUnknown) {
		return;
	}
	const auto time = trackTime();
	const auto prepare = [&](not_null<Frame*> frame) {
		frame->request = _request;
		frame->original = ConvertFrame(
			_stream,
			frame->request.resize,
			std::move(frame->original));
		if (frame->original.isNull()) {
			frame->prepared = QImage();
			interrupt();
			_error();
			return;
		}

		VideoTrack::PrepareFrameByRequest(frame);

		Ensures(VideoTrack::IsPrepared(frame));
	};
	const auto presented = _shared->presentFrame(time.trackTime, prepare);
	if (presented.displayPosition != kTimeUnknown) {
		const auto trackLeft = presented.displayPosition - time.trackTime;

		// We don't use rpl::variable, because we want an event each time
		// we assign a new value, even if the value really didn't change.
		_nextFrameDisplayTime = time.worldTime
			+ crl::time(std::round(trackLeft / _options.speed));
		_nextFrameTimeUpdates.fire_copy(_nextFrameDisplayTime);
	}
	queueReadFrames(presented.nextCheckDelay);
}

void VideoTrackObject::pause(crl::time time) {
	Expects(_syncTimePoint.valid());

	if (interrupted()) {
		return;
	} else if (_pausedTime == kTimeUnknown) {
		_pausedTime = time;
	}
}

void VideoTrackObject::resume(crl::time time) {
	Expects(_syncTimePoint.trackTime != kTimeUnknown);

	if (interrupted()) {
		return;
	}

	// Resumed time used to validate sync to audio.
	_resumedTime = time;
	if (_pausedTime != kTimeUnknown) {
		Assert(_pausedTime <= time);
		_syncTimePoint.worldTime += (time - _pausedTime);
		_pausedTime = kTimeUnknown;
	} else {
		_syncTimePoint.worldTime = time;
	}
	queueReadFrames();

	Ensures(_syncTimePoint.valid());
	Ensures(_pausedTime == kTimeUnknown);
}

void VideoTrackObject::setSpeed(float64 speed) {
	if (interrupted()) {
		return;
	}
	if (_syncTimePoint.valid()) {
		_syncTimePoint = trackTime();
	}
	_options.speed = speed;
}

bool VideoTrackObject::interrupted() const {
	return (_shared == nullptr);
}

void VideoTrackObject::frameDisplayed() {
	if (interrupted()) {
		return;
	}
	queueReadFrames();
}

void VideoTrackObject::updateFrameRequest(const FrameRequest &request) {
	_request = request;
}

bool VideoTrackObject::tryReadFirstFrame(Packet &&packet) {
	if (ProcessPacket(_stream, std::move(packet)).failed()) {
		return false;
	}
	auto frame = QImage();
	if (const auto error = ReadNextFrame(_stream)) {
		if (error.code() == AVERROR_EOF) {
			if (!_initialSkippingFrame) {
				return false;
			}
			// Return the last valid frame if we seek too far.
			_stream.frame = std::move(_initialSkippingFrame);
			return processFirstFrame();
		} else if (error.code() != AVERROR(EAGAIN) || _noMoreData) {
			return false;
		} else {
			// Waiting for more packets.
			return true;
		}
	} else if (!fillStateFromFrame()) {
		return false;
	} else if (_syncTimePoint.trackTime < _options.position) {
		// Seek was with AVSEEK_FLAG_BACKWARD so first we get old frames.
		// Try skipping frames until one is after the requested position.
		std::swap(_initialSkippingFrame, _stream.frame);
		if (!_stream.frame) {
			_stream.frame = MakeFramePointer();
		}
		return true;
	} else {
		return processFirstFrame();
	}
}

bool VideoTrackObject::processFirstFrame() {
	auto frame = ConvertFrame(_stream, QSize(), QImage());
	if (frame.isNull()) {
		return false;
	}
	_shared->init(std::move(frame), _syncTimePoint.trackTime);
	callReady();
	if (!_stream.queue.empty()) {
		queueReadFrames();
	}
	return true;
}

crl::time VideoTrackObject::currentFramePosition() const {
	const auto position = std::min(
		FramePosition(_stream),
		_stream.duration);
	if (_previousFramePosition != kTimeUnknown
		&& position <= _previousFramePosition) {
		return kTimeUnknown;
	}
	_previousFramePosition = position;
	return position;
}

bool VideoTrackObject::fillStateFromFrame() {
	const auto position = currentFramePosition();
	if (position == kTimeUnknown) {
		return false;
	}
	_syncTimePoint.trackTime = position;
	return true;
}

void VideoTrackObject::callReady() {
	Expects(_ready != nullptr);

	const auto frame = _shared->frameForPaint();
	Assert(frame != nullptr);

	auto data = VideoInformation();
	data.size = frame->original.size();
	if (RotationSwapWidthHeight(_stream.rotation)) {
		data.size.transpose();
	}
	data.cover = frame->original;
	data.rotation = _stream.rotation;
	data.state.duration = _stream.duration;
	data.state.position = _syncTimePoint.trackTime;
	data.state.receivedTill = _noMoreData
		? _stream.duration
		: _syncTimePoint.trackTime;
	base::take(_ready)({ data });
}

TimePoint VideoTrackObject::trackTime() const {
	auto result = TimePoint();
	result.worldTime = (_pausedTime != kTimeUnknown)
		? _pausedTime
		: crl::now();
	if (!_syncTimePoint) {
		result.trackTime = _syncTimePoint.trackTime;
		return result;
	}

	Assert(_resumedTime != kTimeUnknown);
	if (_options.syncVideoByAudio && _audioId.playId()) {
		const auto mixer = Media::Player::mixer();
		const auto point = mixer->getVideoSyncTimePoint(_audioId);
		if (point && point.worldTime > _resumedTime) {
			_syncTimePoint = point;
		}
	}
	const auto adjust = (result.worldTime - _syncTimePoint.worldTime);
	result.trackTime = _syncTimePoint.trackTime
		+ crl::time(std::round(adjust * _options.speed));
	return result;
}

void VideoTrackObject::interrupt() {
	_shared = nullptr;
}

void VideoTrack::Shared::init(QImage &&cover, crl::time position) {
	Expects(!initialized());

	_frames[0].original = std::move(cover);
	_frames[0].position = position;

	// Usually main thread sets displayed time before _counter increment.
	// But in this case we update _counter, so we set a fake displayed time.
	_frames[0].displayed = kDisplaySkipped;

	_counter.store(0, std::memory_order_release);
}

int VideoTrack::Shared::counter() const {
	return _counter.load(std::memory_order_acquire);
}

bool VideoTrack::Shared::initialized() const {
	return (counter() != kCounterUninitialized);
}

not_null<VideoTrack::Frame*> VideoTrack::Shared::getFrame(int index) {
	Expects(index >= 0 && index < kFramesCount);

	return &_frames[index];
}

auto VideoTrack::Shared::prepareState(
	crl::time trackTime,
	bool dropStaleFrames)
-> PrepareState {
	const auto prepareNext = [&](int index) -> PrepareState {
		const auto frame = getFrame(index);
		const auto next = getFrame((index + 1) % kFramesCount);
		if (!IsDecoded(frame)) {
			return frame;
		} else if (dropStaleFrames && IsStale(frame, trackTime)) {
			std::swap(*frame, *next);
			next->displayed = kDisplaySkipped;
			return IsDecoded(frame) ? next : frame;
		} else if (!IsDecoded(next)) {
			return next;
		} else {
			return PrepareNextCheck(frame->position - trackTime + 1);
		}
	};
	const auto finishPrepare = [&](int index) {
		const auto frame = getFrame(index);
		// If player already awaits next frame - we ignore if it's stale.
		return IsDecoded(frame) ? std::nullopt : PrepareState(frame);
	};

	switch (counter()) {
	case 0: return finishPrepare(1);
	case 1: return prepareNext(2);
	case 2: return finishPrepare(2);
	case 3: return prepareNext(3);
	case 4: return finishPrepare(3);
	case 5: return prepareNext(0);
	case 6: return finishPrepare(0);
	case 7: return prepareNext(1);
	}
	Unexpected("Counter value in VideoTrack::Shared::prepareState.");
}

template <typename PrepareCallback>
auto VideoTrack::Shared::presentFrame(
	crl::time trackTime,
	PrepareCallback &&prepare)
-> PresentFrame {
	const auto present = [&](int counter, int index) -> PresentFrame {
		const auto frame = getFrame(index);
		const auto position = frame->position;
		prepare(frame);
		if (!IsPrepared(frame)) {
			return { kTimeUnknown, crl::time(0) };
		}

		// Release this frame to the main thread for rendering.
		_counter.store(
			(counter + 1) % (2 * kFramesCount),
			std::memory_order_release);
		return { position, crl::time(0) };
	};
	const auto nextCheckDelay = [&](int index) -> PresentFrame {
		const auto frame = getFrame(index);
		const auto next = getFrame((index + 1) % kFramesCount);
		if (!IsDecoded(frame)
			|| !IsDecoded(next)
			|| IsStale(frame, trackTime)) {
			return { kTimeUnknown, crl::time(0) };
		}
		return { kTimeUnknown, (trackTime - frame->position + 1) };
	};

	switch (counter()) {
	case 0: return present(0, 1);
	case 1: return nextCheckDelay(2);
	case 2: return present(2, 2);
	case 3: return nextCheckDelay(3);
	case 4: return present(4, 3);
	case 5: return nextCheckDelay(0);
	case 6: return present(6, 0);
	case 7: return nextCheckDelay(1);
	}
	Unexpected("Counter value in VideoTrack::Shared::prepareState.");
}

crl::time VideoTrack::Shared::markFrameDisplayed(crl::time now) {
	const auto markAndJump = [&](int counter, int index) {
		const auto frame = getFrame(index);
		Assert(frame->displayed == kTimeUnknown);

		frame->displayed = now;
		_counter.store(
			(counter + 1) % (2 * kFramesCount),
			std::memory_order_release);
		return frame->position;
	};


	switch (counter()) {
	case 0: return kTimeUnknown;
	case 1: return markAndJump(1, 1);
	case 2: return kTimeUnknown;
	case 3: return markAndJump(3, 2);
	case 4: return kTimeUnknown;
	case 5: return markAndJump(5, 3);
	case 6: return kTimeUnknown;
	case 7: return markAndJump(7, 0);
	}
	Unexpected("Counter value in VideoTrack::Shared::markFrameDisplayed.");
}

not_null<VideoTrack::Frame*> VideoTrack::Shared::frameForPaint() {
	// #TODO streaming optimize mark as displayed if possible
	return getFrame(counter() / 2);
}

VideoTrack::VideoTrack(
	const PlaybackOptions &options,
	Stream &&stream,
	const AudioMsgId &audioId,
	FnMut<void(const Information &)> ready,
	Fn<void()> error)
: _streamIndex(stream.index)
, _streamTimeBase(stream.timeBase)
//, _streamRotation(stream.rotation)
, _shared(std::make_unique<Shared>())
, _wrapped(
	options,
	_shared.get(),
	std::move(stream),
	audioId,
	std::move(ready),
	std::move(error)) {
}

int VideoTrack::streamIndex() const {
	return _streamIndex;
}

AVRational VideoTrack::streamTimeBase() const {
	return _streamTimeBase;
}

void VideoTrack::process(Packet &&packet) {
	_wrapped.with([
		packet = std::move(packet)
	](Implementation &unwrapped) mutable {
		unwrapped.process(std::move(packet));
	});
}

void VideoTrack::waitForData() {
}

void VideoTrack::pause(crl::time time) {
	_wrapped.with([=](Implementation &unwrapped) {
		unwrapped.pause(time);
	});
}

void VideoTrack::resume(crl::time time) {
	_wrapped.with([=](Implementation &unwrapped) {
		unwrapped.resume(time);
	});
}

void VideoTrack::setSpeed(float64 speed) {
	_wrapped.with([=](Implementation &unwrapped) {
		unwrapped.setSpeed(speed);
	});
}

crl::time VideoTrack::markFrameDisplayed(crl::time now) {
	const auto position = _shared->markFrameDisplayed(now);
	if (position != kTimeUnknown) {
		_wrapped.with([](Implementation &unwrapped) {
			unwrapped.frameDisplayed();
		});
	}
	return position;
}

QImage VideoTrack::frame(const FrameRequest &request) {
	const auto frame = _shared->frameForPaint();
	const auto changed = (frame->request != request);
	if (changed) {
		frame->request = request;
		_wrapped.with([=](Implementation &unwrapped) {
			unwrapped.updateFrameRequest(request);
		});
	}
	return PrepareFrameByRequest(frame, !changed);
}

QImage VideoTrack::PrepareFrameByRequest(
		not_null<Frame*> frame,
		bool useExistingPrepared) {
	Expects(!frame->original.isNull());

	if (GoodForRequest(frame->original, frame->request)) {
		return frame->original;
	} else if (frame->prepared.isNull() || !useExistingPrepared) {
		frame->prepared = PrepareByRequest(
			frame->original,
			frame->request,
			std::move(frame->prepared));
	}
	return frame->prepared;
}

bool VideoTrack::IsDecoded(not_null<Frame*> frame) {
	return (frame->position != kTimeUnknown)
		&& (frame->displayed == kTimeUnknown);
}

bool VideoTrack::IsPrepared(not_null<Frame*> frame) {
	return IsDecoded(frame)
		&& !frame->original.isNull();
}

bool VideoTrack::IsStale(not_null<Frame*> frame, crl::time trackTime) {
	Expects(IsDecoded(frame));

	return (frame->position < trackTime);
}

rpl::producer<crl::time> VideoTrack::renderNextFrame() const {
	return _wrapped.producer_on_main([](const Implementation &unwrapped) {
		return unwrapped.displayFrameAt();
	});
}

rpl::producer<> VideoTrack::waitingForData() const {
	return _wrapped.producer_on_main([](const Implementation &unwrapped) {
		return unwrapped.waitingForData();
	});
}

VideoTrack::~VideoTrack() {
	_wrapped.with([shared = std::move(_shared)](Implementation &unwrapped) {
		unwrapped.interrupt();
	});
}

} // namespace Streaming
} // namespace Media
