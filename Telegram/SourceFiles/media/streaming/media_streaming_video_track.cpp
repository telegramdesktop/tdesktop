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

	void start(crl::time startTime);
	void interrupt();
	void frameDisplayed();

private:
	[[nodiscard]] bool interrupted() const;
	[[nodiscard]] bool tryReadFirstFrame(Packet &&packet);
	[[nodiscard]] bool fillStateFromFrame();
	void queueReadFrames(crl::time delay = 0);
	void readFrames();
	[[nodiscard]] bool readFrame(not_null<Frame*> frame);
	void presentFrameIfNeeded();
	void callReady();

	// Force frame position to be clamped to [0, duration] and monotonic.
	[[nodiscard]] crl::time currentFramePosition() const;

	struct TrackTime {
		crl::time worldNow = kTimeUnknown;
		crl::time trackNow = kTimeUnknown;
	};
	[[nodiscard]] TrackTime trackTime() const;

	const crl::weak_on_queue<VideoTrackObject> _weak;
	const PlaybackOptions _options;

	// Main thread wrapper destructor will set _shared back to nullptr.
	// All queued method calls after that should be discarded.
	Shared *_shared = nullptr;

	Stream _stream;
	AudioMsgId _audioId;
	bool _noMoreData = false;
	FnMut<void(const Information &)> _ready;
	Fn<void()> _error;
	crl::time _startedTime = kTimeUnknown;
	crl::time _startedPosition = kTimeUnknown;
	mutable crl::time _previousFramePosition = kTimeUnknown;
	rpl::variable<crl::time> _nextFrameDisplayTime = kTimeUnknown;

	bool _queued = false;
	base::ConcurrentTimer _readFramesTimer;

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
	return _nextFrameDisplayTime.value();
}

void VideoTrackObject::process(Packet &&packet) {
	_noMoreData = packet.empty();
	if (interrupted()) {
		return;
	} else if (_shared->initialized()) {
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
	const auto state = _shared->prepareState(trackTime().trackNow);
	state.match([&](Shared::PrepareFrame frame) {
		if (readFrame(frame)) {
			presentFrameIfNeeded();
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
			// read till end
		} else if (error.code() != AVERROR(EAGAIN) || _noMoreData) {
			interrupt();
			_error();
		}
		return false;
	}
	const auto position = currentFramePosition();
	if (position == kTimeUnknown) {
		interrupt();
		_error();
		return false;
	}
	frame->original = ConvertFrame(
		_stream,
		QSize(),
		std::move(frame->original));
	frame->position = position;
	frame->displayPosition = position; // #TODO streaming adjust / sync
	frame->displayed = kTimeUnknown;

	//frame->request
	//frame->prepared

	return true;
}

void VideoTrackObject::presentFrameIfNeeded() {
	const auto time = trackTime();
	const auto presented = _shared->presentFrame(time.trackNow);
	if (presented.displayPosition != kTimeUnknown) {
		const auto trackLeft = presented.displayPosition - time.trackNow;
		_nextFrameDisplayTime = time.worldNow
			+ crl::time(std::round(trackLeft / _options.speed));
	}
	queueReadFrames(presented.nextCheckDelay);
}

void VideoTrackObject::start(crl::time startTime) {
	_startedTime = startTime;
	queueReadFrames();
}

bool VideoTrackObject::interrupted() const {
	return (_shared == nullptr);
}

void VideoTrackObject::frameDisplayed() {
	queueReadFrames();
}

bool VideoTrackObject::tryReadFirstFrame(Packet &&packet) {
	if (ProcessPacket(_stream, std::move(packet)).failed()) {
		return false;
	}
	if (const auto error = ReadNextFrame(_stream)) {
		if (error.code() == AVERROR_EOF) {
			// #TODO streaming fix seek to the end.
			return false;
		} else if (error.code() != AVERROR(EAGAIN) || _noMoreData) {
			return false;
		}
		return true;
	} else if (!fillStateFromFrame()) {
		return false;
	}
	auto frame = ConvertFrame(_stream, QSize(), QImage());
	if (frame.isNull()) {
		return false;
	}
	_shared->init(std::move(frame), _startedPosition);
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
	_startedPosition = currentFramePosition();
	_nextFrameDisplayTime = _startedTime;
	return (_startedPosition != kTimeUnknown);
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
	data.state.position = _startedPosition;
	data.state.receivedTill = _noMoreData
		? _stream.duration
		: _startedPosition;
	base::take(_ready)({ data });
}

VideoTrackObject::TrackTime VideoTrackObject::trackTime() const {
	auto result = TrackTime();
	const auto started = (_startedTime != kTimeUnknown);
	if (!started) {
		result.worldNow = crl::now();
		result.trackNow = _startedPosition;
		return result;
	}

	const auto correction = _audioId.playId()
		? Media::Player::mixer()->getVideoTimeCorrection(_audioId)
		: Media::Player::Mixer::TimeCorrection();
	const auto knownValue = correction
		? correction.audioPositionValue
		: _startedPosition;
	const auto knownTime = correction
		? correction.audioPositionTime
		: _startedTime;
	const auto worldNow = crl::now();
	const auto sinceKnown = (worldNow - knownTime);

	result.worldNow = worldNow;
	result.trackNow = knownValue
		+ crl::time(std::round(sinceKnown * _options.speed));
	return result;
}

void VideoTrackObject::interrupt() {
	_shared = nullptr;
}

void VideoTrack::Shared::init(QImage &&cover, crl::time position) {
	Expects(!initialized());

	_frames[0].original = std::move(cover);
	_frames[0].position = position;
	_frames[0].displayPosition = position;

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

bool VideoTrack::Shared::IsPrepared(not_null<Frame*> frame) {
	return (frame->displayPosition != kTimeUnknown)
		&& (frame->displayed == kTimeUnknown)
		&& !frame->original.isNull();
}

bool VideoTrack::Shared::IsStale(
		not_null<Frame*> frame,
		crl::time trackTime) {
	Expects(IsPrepared(frame));

	return (frame->displayPosition < trackTime);
}

auto VideoTrack::Shared::prepareState(crl::time trackTime) -> PrepareState {
	const auto prepareNext = [&](int index) -> PrepareState {
		const auto frame = getFrame(index);
		const auto next = getFrame((index + 1) % kFramesCount);
		if (!IsPrepared(frame)) {
			return frame;
		} else if (IsStale(frame, trackTime)) {
			std::swap(*frame, *next);
			next->displayed = kDisplaySkipped;
			return IsPrepared(frame) ? next : frame;
		} else if (!IsPrepared(next)) {
			return next;
		} else {
			return PrepareNextCheck(frame->displayPosition - trackTime + 1);
		}
	};
	const auto finishPrepare = [&](int index) {
		const auto frame = getFrame(index);
		// If player already awaits next frame - we ignore if it's stale.
		return IsPrepared(frame) ? std::nullopt : PrepareState(frame);
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

auto VideoTrack::Shared::presentFrame(crl::time trackTime) -> PresentFrame {
	const auto present = [&](int counter, int index) -> PresentFrame {
		const auto frame = getFrame(index);
		Assert(IsPrepared(frame));
		const auto position = frame->displayPosition;

		// Release this frame to the main thread for rendering.
		_counter.store(
			(counter + 1) % (2 * kFramesCount),
			std::memory_order_release);
		return { position, crl::time(0) };
	};
	const auto nextCheckDelay = [&](int index) -> PresentFrame {
		const auto frame = getFrame(index);
		const auto next = getFrame((index + 1) % kFramesCount);
		if (!IsPrepared(frame)
			|| !IsPrepared(next)
			|| IsStale(frame, trackTime)) {
			return { kTimeUnknown, crl::time(0) };
		}
		return { kTimeUnknown, (trackTime - frame->displayPosition + 1) };
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

void VideoTrack::start(crl::time startTime) {
	_wrapped.with([=](Implementation &unwrapped) {
		unwrapped.start(startTime);
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

QImage VideoTrack::frame(const FrameRequest &request) const {
	const auto frame = _shared->frameForPaint();
	Assert(frame != nullptr);
	Assert(!frame->original.isNull());

	if (request.resize.isEmpty()) {
		return frame->original;
	} else if (frame->prepared.isNull() || frame->request != request) {
		// #TODO streaming prepare frame
		//frame->request = request;
		//frame->prepared = PrepareFrame(
		//	frame->original,
		//	request,
		//	std::move(frame->prepared));
	}
	return frame->prepared;
}

rpl::producer<crl::time> VideoTrack::renderNextFrame() const {
	return _wrapped.producer_on_main([](const Implementation &unwrapped) {
		return unwrapped.displayFrameAt();
	});
}

VideoTrack::~VideoTrack() {
	_wrapped.with([shared = std::move(_shared)](Implementation &unwrapped) {
		unwrapped.interrupt();
	});
}

} // namespace Streaming
} // namespace Media
