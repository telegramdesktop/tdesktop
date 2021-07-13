/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_video_track.h"

#include "ffmpeg/ffmpeg_utility.h"
#include "media/audio/media_audio.h"
#include "base/concurrent_timer.h"
#include "core/crash_reports.h"

namespace Media {
namespace Streaming {
namespace {

constexpr auto kMaxFrameArea = 3840 * 2160; // usual 4K
constexpr auto kDisplaySkipped = crl::time(-1);
constexpr auto kFinishedPosition = std::numeric_limits<crl::time>::max();
static_assert(kDisplaySkipped != kTimeUnknown);

[[nodiscard]] QImage ConvertToARGB32(const FrameYUV420 &data) {
	Expects(data.y.data != nullptr);
	Expects(data.u.data != nullptr);
	Expects(data.v.data != nullptr);
	Expects(!data.size.isEmpty());

	//if (FFmpeg::RotationSwapWidthHeight(stream.rotation)) {
	//	resize.transpose();
	//}

	auto result = FFmpeg::CreateFrameStorage(data.size);
	const auto swscale = FFmpeg::MakeSwscalePointer(
		data.size,
		AV_PIX_FMT_YUV420P,
		data.size,
		AV_PIX_FMT_BGRA);
	if (!swscale) {
		return QImage();
	}

	// AV_NUM_DATA_POINTERS defined in AVFrame struct
	const uint8_t *srcData[AV_NUM_DATA_POINTERS] = {
		static_cast<const uint8_t*>(data.y.data),
		static_cast<const uint8_t*>(data.u.data),
		static_cast<const uint8_t*>(data.v.data),
		nullptr,
	};
	int srcLinesize[AV_NUM_DATA_POINTERS] = {
		data.y.stride,
		data.u.stride,
		data.v.stride,
		0,
	};
	uint8_t *dstData[AV_NUM_DATA_POINTERS] = { result.bits(), nullptr };
	int dstLinesize[AV_NUM_DATA_POINTERS] = { result.bytesPerLine(), 0 };

	sws_scale(
		swscale.get(),
		srcData,
		srcLinesize,
		0,
		data.size.height(),
		dstData,
		dstLinesize);

	return result;
}

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
		Fn<void(Error)> error);

	void process(std::vector<FFmpeg::Packet> &&packets);

	[[nodiscard]] rpl::producer<> checkNextFrame() const;
	[[nodiscard]] rpl::producer<> waitingForData() const;

	void pause(crl::time time);
	void resume(crl::time time);
	void setSpeed(float64 speed);
	void setWaitForMarkAsShown(bool wait);
	void interrupt();
	void frameShown();
	void addTimelineDelay(crl::time delayed);
	void updateFrameRequest(
		const Instance *instance,
		const FrameRequest &request);
	void removeFrameRequest(const Instance *instance);

	void rasterizeFrame(not_null<Frame*> frame);
	[[nodiscard]] bool requireARGB32() const;

private:
	enum class FrameResult {
		Done,
		Error,
		Waiting,
		Looped,
		Finished,
	};
	using ReadEnoughState = std::variant<
		v::null_t,
		FrameResult,
		Shared::PrepareNextCheck>;

	void fail(Error error);
	[[nodiscard]] bool interrupted() const;
	[[nodiscard]] bool tryReadFirstFrame(FFmpeg::Packet &&packet);
	[[nodiscard]] bool fillStateFromFrame();
	[[nodiscard]] bool processFirstFrame();
	void queueReadFrames(crl::time delay = 0);
	void readFrames();
	[[nodiscard]] ReadEnoughState readEnoughFrames(crl::time trackTime);
	[[nodiscard]] FrameResult readFrame(not_null<Frame*> frame);
	void fillRequests(not_null<Frame*> frame) const;
	[[nodiscard]] QSize chooseOriginalResize() const;
	void presentFrameIfNeeded();
	void callReady();
	[[nodiscard]] bool loopAround();
	[[nodiscard]] crl::time computeDuration() const;
	[[nodiscard]] int durationByPacket(const FFmpeg::Packet &packet);

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
	bool _readTillEnd = false;
	FnMut<void(const Information &)> _ready;
	Fn<void(Error)> _error;
	crl::time _pausedTime = kTimeUnknown;
	crl::time _resumedTime = kTimeUnknown;
	int _durationByLastPacket = 0;
	mutable TimePoint _syncTimePoint;
	crl::time _loopingShift = 0;
	rpl::event_stream<> _checkNextFrame;
	rpl::event_stream<> _waitingForData;
	base::flat_map<const Instance*, FrameRequest> _requests;

	bool _queued = false;
	base::ConcurrentTimer _readFramesTimer;

	// For initial frame skipping for an exact seek.
	FFmpeg::FramePointer _initialSkippingFrame;

};

VideoTrackObject::VideoTrackObject(
	crl::weak_on_queue<VideoTrackObject> weak,
	const PlaybackOptions &options,
	not_null<Shared*> shared,
	Stream &&stream,
	const AudioMsgId &audioId,
	FnMut<void(const Information &)> ready,
	Fn<void(Error)> error)
: _weak(std::move(weak))
, _options(options)
, _shared(shared)
, _stream(std::move(stream))
, _audioId(audioId)
, _ready(std::move(ready))
, _error(std::move(error))
, _readFramesTimer(_weak, [=] { readFrames(); }) {
	Expects(_stream.duration > 1);
	Expects(_ready != nullptr);
	Expects(_error != nullptr);
}

rpl::producer<> VideoTrackObject::checkNextFrame() const {
	return interrupted()
		? (rpl::complete<>() | rpl::type_erased())
		: !_shared->firstPresentHappened()
		? (_checkNextFrame.events() | rpl::type_erased())
		: _checkNextFrame.events_starting_with({});
}

rpl::producer<> VideoTrackObject::waitingForData() const {
	return interrupted()
		? (rpl::never() | rpl::type_erased())
		: _waitingForData.events();
}

void VideoTrackObject::process(std::vector<FFmpeg::Packet> &&packets) {
	if (interrupted() || packets.empty()) {
		return;
	}
	if (packets.front().empty()) {
		Assert(packets.size() == 1);
		_readTillEnd = true;
	} else if (!_readTillEnd) {
		//for (const auto &packet : packets) {
		//	// Maybe it is enough to count by list.back()?.. hope so.
		//	accumulate_max(
		//		_durationByLastPacket,
		//		durationByPacket(packet));
		//	if (interrupted()) {
		//		return;
		//	}
		//}
		accumulate_max(
			_durationByLastPacket,
			durationByPacket(packets.back()));
		if (interrupted()) {
			return;
		}
	}
	for (auto i = begin(packets), e = end(packets); i != e; ++i) {
		if (_shared->initialized()) {
			_stream.queue.insert(
				end(_stream.queue),
				std::make_move_iterator(i),
				std::make_move_iterator(e));
			queueReadFrames();
			break;
		} else if (!tryReadFirstFrame(std::move(*i))) {
			fail(Error::InvalidData);
			break;
		}
	}
}

int VideoTrackObject::durationByPacket(const FFmpeg::Packet &packet) {
	// We've set this value on the first cycle.
	if (_loopingShift || _stream.duration != kDurationUnavailable) {
		return 0;
	}
	const auto result = FFmpeg::DurationByPacket(packet, _stream.timeBase);
	if (result < 0) {
		fail(Error::InvalidData);
		return 0;
	}

	Ensures(result > 0);
	return result;
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
	auto time = trackTime().trackTime;
	while (true) {
		const auto result = readEnoughFrames(time);
		v::match(result, [&](FrameResult result) {
			if (result == FrameResult::Done
				|| result == FrameResult::Finished) {
				presentFrameIfNeeded();
			} else if (result == FrameResult::Looped) {
				const auto duration = computeDuration();
				Assert(duration != kDurationUnavailable);
				time -= duration;
			}
		}, [&](Shared::PrepareNextCheck delay) {
			Expects(delay == kTimeUnknown || delay > 0);

			if (delay != kTimeUnknown) {
				queueReadFrames(delay);
			}
		}, [](v::null_t) {
		});
		if (!v::is_null(result)) {
			break;
		}
	}
}

auto VideoTrackObject::readEnoughFrames(crl::time trackTime)
-> ReadEnoughState {
	const auto dropStaleFrames = !_options.waitForMarkAsShown;
	const auto state = _shared->prepareState(trackTime, dropStaleFrames);
	return v::match(state, [&](Shared::PrepareFrame frame)
	-> ReadEnoughState {
		while (true) {
			const auto result = readFrame(frame);
			if (result != FrameResult::Done) {
				return result;
			} else if (!dropStaleFrames
				|| !VideoTrack::IsStale(frame, trackTime)) {
				return v::null;
			}
		}
	}, [&](Shared::PrepareNextCheck delay) -> ReadEnoughState {
		Expects(delay == kTimeUnknown || delay > 0); // Debugging crash.

		return delay;
	}, [&](v::null_t) -> ReadEnoughState {
		return FrameResult::Done;
	});
}

bool VideoTrackObject::loopAround() {
	const auto duration = computeDuration();
	if (duration == kDurationUnavailable) {
		LOG(("Streaming Error: "
			"Couldn't find out the real video stream duration."));
		return false;
	}
	avcodec_flush_buffers(_stream.codec.get());
	_loopingShift += duration;
	_readTillEnd = false;
	return true;
}

crl::time VideoTrackObject::computeDuration() const {
	if (_stream.duration != kDurationUnavailable) {
		return _stream.duration;
	} else if ((_loopingShift || _readTillEnd) && _durationByLastPacket) {
		// We looped, so it already holds full stream duration.
		return _durationByLastPacket;
	}
	return kDurationUnavailable;
}

auto VideoTrackObject::readFrame(not_null<Frame*> frame) -> FrameResult {
	if (const auto error = ReadNextFrame(_stream)) {
		if (error.code() == AVERROR_EOF) {
			if (!_options.loop) {
				frame->position = kFinishedPosition;
				frame->displayed = kTimeUnknown;
				return FrameResult::Finished;
			} else if (loopAround()) {
				return FrameResult::Looped;
			} else {
				fail(Error::InvalidData);
				return FrameResult::Error;
			}
		} else if (error.code() != AVERROR(EAGAIN) || _readTillEnd) {
			fail(Error::InvalidData);
			return FrameResult::Error;
		}
		Assert(_stream.queue.empty());
		_waitingForData.fire({});
		return FrameResult::Waiting;
	}
	const auto position = currentFramePosition();
	if (position == kTimeUnknown) {
		fail(Error::InvalidData);
		return FrameResult::Error;
	}
	std::swap(frame->decoded, _stream.frame);
	frame->position = position;
	frame->displayed = kTimeUnknown;
	return FrameResult::Done;
}

void VideoTrackObject::fillRequests(not_null<Frame*> frame) const {
	auto i = frame->prepared.begin();
	for (const auto &[instance, request] : _requests) {
		while (i != frame->prepared.end() && i->first < instance) {
			i = frame->prepared.erase(i);
		}
		if (i == frame->prepared.end() || i->first > instance) {
			i = frame->prepared.emplace(instance, request).first;
		}
		++i;
	}
	while (i != frame->prepared.end()) {
		i = frame->prepared.erase(i);
	}
}

QSize VideoTrackObject::chooseOriginalResize() const {
	auto chosen = QSize();
	for (const auto &[_, request] : _requests) {
		if (request.resize.isEmpty()) {
			return QSize();
		}
		const auto byWidth = (request.resize.width() >= chosen.width());
		const auto byHeight = (request.resize.height() >= chosen.height());
		if (byWidth && byHeight) {
			chosen = request.resize;
		} else if (byWidth || byHeight) {
			return QSize();
		}
	}
	return chosen;
}

bool VideoTrackObject::requireARGB32() const {
	for (const auto &[_, request] : _requests) {
		if (!request.requireARGB32) {
			return false;
		}
	}
	return true;
}

void VideoTrackObject::rasterizeFrame(not_null<Frame*> frame) {
	Expects(frame->position != kFinishedPosition);

	fillRequests(frame);
	frame->format = FrameFormat::None;
	if (frame->decoded->format == AV_PIX_FMT_YUV420P && !requireARGB32()) {
		frame->alpha = false;
		frame->yuv420 = ExtractYUV420(_stream, frame->decoded.get());
		if (frame->yuv420.size.isEmpty()
			|| frame->yuv420.chromaSize.isEmpty()
			|| !frame->yuv420.y.data
			|| !frame->yuv420.u.data
			|| !frame->yuv420.v.data) {
			frame->prepared.clear();
			fail(Error::InvalidData);
			return;
		}
		if (!frame->original.isNull()) {
			frame->original = QImage();
			for (auto &[_, prepared] : frame->prepared) {
				prepared.image = QImage();
			}
		}
		frame->format = FrameFormat::YUV420;
	} else {
		frame->alpha = (frame->decoded->format == AV_PIX_FMT_BGRA);
		frame->yuv420.size = {
			frame->decoded->width,
			frame->decoded->height
		};
		frame->original = ConvertFrame(
			_stream,
			frame->decoded.get(),
			chooseOriginalResize(),
			std::move(frame->original));
		if (frame->original.isNull()) {
			frame->prepared.clear();
			fail(Error::InvalidData);
			return;
		}
		frame->format = FrameFormat::ARGB32;
	}

	VideoTrack::PrepareFrameByRequests(frame, _stream.rotation);

	Ensures(VideoTrack::IsRasterized(frame));
}

void VideoTrackObject::presentFrameIfNeeded() {
	if (_pausedTime != kTimeUnknown || _resumedTime == kTimeUnknown) {
		return;
	}
	const auto dropStaleFrames = !_options.waitForMarkAsShown;
	const auto presented = _shared->presentFrame(
		this,
		trackTime(),
		_options.speed,
		dropStaleFrames);
	addTimelineDelay(presented.addedWorldTimeDelay);
	if (presented.displayPosition == kFinishedPosition) {
		interrupt();
		_checkNextFrame = rpl::event_stream<>();
		return;
	} else if (presented.displayPosition != kTimeUnknown) {
		_checkNextFrame.fire({});
	}
	if (presented.nextCheckDelay != kTimeUnknown) {
		Assert(presented.nextCheckDelay >= 0);
		queueReadFrames(presented.nextCheckDelay);
	}
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

void VideoTrackObject::setWaitForMarkAsShown(bool wait) {
	if (interrupted()) {
		return;
	}
	_options.waitForMarkAsShown = wait;
}

bool VideoTrackObject::interrupted() const {
	return (_shared == nullptr);
}

void VideoTrackObject::frameShown() {
	if (interrupted()) {
		return;
	}
	queueReadFrames();
}

void VideoTrackObject::addTimelineDelay(crl::time delayed) {
	Expects(_syncTimePoint.valid());

	if (!delayed) {
		return;
	}
	_syncTimePoint.worldTime += delayed;
}

void VideoTrackObject::updateFrameRequest(
		const Instance *instance,
		const FrameRequest &request) {
	_requests[instance] = request;
}

void VideoTrackObject::removeFrameRequest(const Instance *instance) {
	_requests.remove(instance);
}

bool VideoTrackObject::tryReadFirstFrame(FFmpeg::Packet &&packet) {
	if (ProcessPacket(_stream, std::move(packet)).failed()) {
		return false;
	}
	while (true) {
		if (const auto error = ReadNextFrame(_stream)) {
			if (error.code() == AVERROR_EOF) {
				if (!_initialSkippingFrame) {
					return false;
				}
				// Return the last valid frame if we seek too far.
				_stream.frame = std::move(_initialSkippingFrame);
				return processFirstFrame();
			} else if (error.code() != AVERROR(EAGAIN) || _readTillEnd) {
				return false;
			} else {
				// Waiting for more packets.
				return true;
			}
		} else if (!fillStateFromFrame()) {
			return false;
		} else if (_syncTimePoint.trackTime >= _options.position) {
			return processFirstFrame();
		}

		// Seek was with AVSEEK_FLAG_BACKWARD so first we get old frames.
		// Try skipping frames until one is after the requested position.
		std::swap(_initialSkippingFrame, _stream.frame);
		if (!_stream.frame) {
			_stream.frame = FFmpeg::MakeFramePointer();
		}
	}
}

bool VideoTrackObject::processFirstFrame() {
	if (_stream.frame->width * _stream.frame->height > kMaxFrameArea) {
		return false;
	}
	auto frame = ConvertFrame(
		_stream,
		_stream.frame.get(),
		QSize(),
		QImage());
	if (frame.isNull()) {
		return false;
	}
	_shared->init(std::move(frame), _syncTimePoint.trackTime);
	callReady();
	queueReadFrames();
	return true;
}

crl::time VideoTrackObject::currentFramePosition() const {
	const auto position = FramePosition(_stream);
	if (position == kTimeUnknown || position == kFinishedPosition) {
		return kTimeUnknown;
	}
	return _loopingShift + std::clamp(
		position,
		crl::time(0),
		computeDuration() - 1);
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

	auto data = VideoInformation();
	data.size = FFmpeg::CorrectByAspect(
		frame->original.size(),
		_stream.aspect);
	if (FFmpeg::RotationSwapWidthHeight(_stream.rotation)) {
		data.size.transpose();
	}
	data.cover = frame->original;
	data.rotation = _stream.rotation;
	data.state.duration = _stream.duration;
	data.state.position = _syncTimePoint.trackTime;
	data.state.receivedTill = _readTillEnd
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
	if (_options.syncVideoByAudio && _audioId.externalPlayId()) {
		const auto mixer = Media::Player::mixer();
		const auto point = mixer->getExternalSyncTimePoint(_audioId);
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

void VideoTrackObject::fail(Error error) {
	interrupt();
	_error(error);
}

void VideoTrack::Shared::init(QImage &&cover, crl::time position) {
	Expects(!initialized());

	_frames[0].original = std::move(cover);
	_frames[0].position = position;
	_frames[0].format = FrameFormat::ARGB32;

	// Usually main thread sets displayed time before _counter increment.
	// But in this case we update _counter, so we set a fake displayed time.
	_frames[0].displayed = kDisplaySkipped;

	_delay = 0;
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

not_null<const VideoTrack::Frame*> VideoTrack::Shared::getFrame(
		int index) const {
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
		} else if (!IsDecoded(next)) {
			return next;
		} else if (next->position < frame->position) {
			std::swap(*frame, *next);
		}
		if (next->position == kFinishedPosition || !dropStaleFrames) {
			return PrepareNextCheck(kTimeUnknown);
		} else if (IsStale(frame, trackTime)) {
			std::swap(*frame, *next);
			next->displayed = kDisplaySkipped;
			return next;
		} else {
			if (frame->position - trackTime + 1 <= 0) { // Debugging crash.
				CrashReports::SetAnnotation(
					"DelayValues",
					(QString::number(frame->position)
						+ " + 1 <= "
						+ QString::number(trackTime)));
			}
			Assert(frame->position >= trackTime);
			Assert(frame->position - trackTime + 1 > 0);

			return PrepareNextCheck(frame->position - trackTime + 1);
		}
	};
	const auto finishPrepare = [&](int index) -> PrepareState {
		// If player already awaits next frame - we ignore if it's stale.
		dropStaleFrames = false;
		const auto result = prepareNext(index);
		return v::is<PrepareNextCheck>(result) ? PrepareState() : result;
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

// Sometimes main thread subscribes to check frame requests before
// the first frame is ready and presented and sometimes after.
bool VideoTrack::Shared::firstPresentHappened() const {
	switch (counter()) {
	case 0: return false;
	case 1: return true;
	}
	Unexpected("Counter value in VideoTrack::Shared::firstPresentHappened.");
}

auto VideoTrack::Shared::presentFrame(
	not_null<VideoTrackObject*> object,
	TimePoint time,
	float64 playbackSpeed,
	bool dropStaleFrames)
-> PresentFrame {
	const auto present = [&](int counter, int index) -> PresentFrame {
		const auto frame = getFrame(index);
		const auto position = frame->position;
		const auto addedWorldTimeDelay = base::take(_delay);
		if (position == kFinishedPosition) {
			return { kFinishedPosition, kTimeUnknown, addedWorldTimeDelay };
		}
		object->rasterizeFrame(frame);
		if (!IsRasterized(frame)) {
			// Error happened during frame prepare.
			return { kTimeUnknown, kTimeUnknown, addedWorldTimeDelay };
		}
		const auto trackLeft = position - time.trackTime;
		frame->display = time.worldTime
			+ addedWorldTimeDelay
			+ crl::time(std::round(trackLeft / playbackSpeed));

		// Release this frame to the main thread for rendering.
		_counter.store(
			counter + 1,
			std::memory_order_release);
		return { position, crl::time(0), addedWorldTimeDelay };
	};
	const auto nextCheckDelay = [&](int index) -> PresentFrame {
		const auto frame = getFrame(index);
		if (frame->position == kFinishedPosition) {
			return { kFinishedPosition, kTimeUnknown };
		}
		const auto next = getFrame((index + 1) % kFramesCount);
		if (!IsDecoded(frame) || !IsDecoded(next)) {
			return { kTimeUnknown, crl::time(0) };
		} else if (next->position == kFinishedPosition
			|| !dropStaleFrames
			|| IsStale(frame, time.trackTime)) {
			return { kTimeUnknown, kTimeUnknown };
		}
		return { kTimeUnknown, (frame->position - time.trackTime + 1) };
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

crl::time VideoTrack::Shared::nextFrameDisplayTime() const {
	const auto frameDisplayTime = [&](int counter) {
		const auto next = (counter + 1) % (2 * kFramesCount);
		const auto index = next / 2;
		const auto frame = getFrame(index);
		if (frame->displayed != kTimeUnknown) {
			// Frame already displayed, but not yet shown.
			return kFrameDisplayTimeAlreadyDone;
		}
		Assert(IsRasterized(frame));
		Assert(frame->display != kTimeUnknown);

		return frame->display;
	};

	switch (counter()) {
	case 0: return kTimeUnknown;
	case 1: return frameDisplayTime(1);
	case 2: return kTimeUnknown;
	case 3: return frameDisplayTime(3);
	case 4: return kTimeUnknown;
	case 5: return frameDisplayTime(5);
	case 6: return kTimeUnknown;
	case 7: return frameDisplayTime(7);
	}
	Unexpected("Counter value in VideoTrack::Shared::nextFrameDisplayTime.");
}

crl::time VideoTrack::Shared::markFrameDisplayed(crl::time now) {
	const auto mark = [&](int counter) {
		const auto next = (counter + 1) % (2 * kFramesCount);
		const auto index = next / 2;
		const auto frame = getFrame(index);
		Assert(frame->position != kTimeUnknown);
		if (frame->displayed == kTimeUnknown) {
			frame->displayed = now;
		}
		return frame->position;
	};

	switch (counter()) {
	case 0: Unexpected("Value 0 in VideoTrack::Shared::markFrameDisplayed.");
	case 1: return mark(1);
	case 2: Unexpected("Value 2 in VideoTrack::Shared::markFrameDisplayed.");
	case 3: return mark(3);
	case 4: Unexpected("Value 4 in VideoTrack::Shared::markFrameDisplayed.");
	case 5: return mark(5);
	case 6: Unexpected("Value 6 in VideoTrack::Shared::markFrameDisplayed.");
	case 7: return mark(7);
	}
	Unexpected("Counter value in VideoTrack::Shared::markFrameDisplayed.");
}

void VideoTrack::Shared::addTimelineDelay(crl::time delayed) {
	if (!delayed) {
		return;
	}
	const auto recountCurrentFrame = [&](int counter) {
		_delay += delayed;
		//const auto next = (counter + 1) % (2 * kFramesCount);
		//const auto index = next / 2;
		//const auto frame = getFrame(index);
		//if (frame->displayed != kTimeUnknown) {
		//	// Frame already displayed.
		//	return;
		//}
		//Assert(IsRasterized(frame));
		//Assert(frame->display != kTimeUnknown);
		//frame->display = countFrameDisplayTime(frame->index);
	};

	switch (counter()) {
	case 0: Unexpected("Value 0 in VideoTrack::Shared::addTimelineDelay.");
	case 1: return recountCurrentFrame(1);
	case 2: Unexpected("Value 2 in VideoTrack::Shared::addTimelineDelay.");
	case 3: return recountCurrentFrame(3);
	case 4: Unexpected("Value 4 in VideoTrack::Shared::addTimelineDelay.");
	case 5: return recountCurrentFrame(5);
	case 6: Unexpected("Value 6 in VideoTrack::Shared::addTimelineDelay.");
	case 7: return recountCurrentFrame(7);
	}
	Unexpected("Counter value in VideoTrack::Shared::addTimelineDelay.");
}

bool VideoTrack::Shared::markFrameShown() {
	const auto jump = [&](int counter) {
		const auto next = (counter + 1) % (2 * kFramesCount);
		const auto index = next / 2;
		const auto frame = getFrame(index);
		if (frame->displayed == kTimeUnknown) {
			return false;
		}
		if (counter == 2 * kFramesCount - 1) {
			++_counterCycle;
		}
		_counter.store(
			next,
			std::memory_order_release);
		return true;
	};

	switch (counter()) {
	case 0: return false;
	case 1: return jump(1);
	case 2: return false;
	case 3: return jump(3);
	case 4: return false;
	case 5: return jump(5);
	case 6: return false;
	case 7: return jump(7);
	}
	Unexpected("Counter value in VideoTrack::Shared::markFrameShown.");
}

not_null<VideoTrack::Frame*> VideoTrack::Shared::frameForPaint() {
	return frameForPaintWithIndex().frame;
}

VideoTrack::FrameWithIndex VideoTrack::Shared::frameForPaintWithIndex() {
	const auto index = counter() / 2;
	const auto frame = getFrame(index);
	Assert(frame->format != FrameFormat::None);
	Assert(frame->position != kTimeUnknown);
	Assert(frame->displayed != kTimeUnknown);
	return {
		.frame = frame,
		.index = (_counterCycle * 2 * kFramesCount) + index,
	};

}

VideoTrack::VideoTrack(
	const PlaybackOptions &options,
	Stream &&stream,
	const AudioMsgId &audioId,
	FnMut<void(const Information &)> ready,
	Fn<void(Error)> error)
: _streamIndex(stream.index)
, _streamTimeBase(stream.timeBase)
, _streamDuration(stream.duration)
, _streamRotation(stream.rotation)
//, _streamAspect(stream.aspect)
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

crl::time VideoTrack::streamDuration() const {
	return _streamDuration;
}

void VideoTrack::process(std::vector<FFmpeg::Packet> &&packets) {
	_wrapped.with([
		packets = std::move(packets)
	](Implementation &unwrapped) mutable {
		unwrapped.process(std::move(packets));
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

void VideoTrack::setWaitForMarkAsShown(bool wait) {
	_wrapped.with([=](Implementation &unwrapped) {
		unwrapped.setWaitForMarkAsShown(wait);
	});
}

crl::time VideoTrack::nextFrameDisplayTime() const {
	return _shared->nextFrameDisplayTime();
}

crl::time VideoTrack::markFrameDisplayed(crl::time now) {
	const auto result = _shared->markFrameDisplayed(now);

	Ensures(result != kTimeUnknown);
	return result;
}

void VideoTrack::addTimelineDelay(crl::time delayed) {
	_shared->addTimelineDelay(delayed);
	//if (!delayed) {
	//	return;
	//}
	//_wrapped.with([=](Implementation &unwrapped) mutable {
	//	unwrapped.addTimelineDelay(delayed);
	//});
}

bool VideoTrack::markFrameShown() {
	if (!_shared->markFrameShown()) {
		return false;
	}
	_wrapped.with([](Implementation &unwrapped) {
		unwrapped.frameShown();
	});
	return true;
}

QImage VideoTrack::frame(
		const FrameRequest &request,
		const Instance *instance) {
	const auto frame = _shared->frameForPaint();
	const auto i = frame->prepared.find(instance);
	const auto none = (i == frame->prepared.end());
	const auto preparedFor = frame->prepared.empty()
		? FrameRequest::NonStrict()
		: (none ? frame->prepared.begin() : i)->second.request;
	const auto changed = !preparedFor.goodFor(request);
	const auto useRequest = changed ? request : preparedFor;
	if (changed) {
		_wrapped.with([=](Implementation &unwrapped) {
			unwrapped.updateFrameRequest(instance, useRequest);
		});
	}
	if (frame->original.isNull()
		&& frame->format == FrameFormat::YUV420) {
		frame->original = ConvertToARGB32(frame->yuv420);
	}
	if (!frame->alpha
		&& GoodForRequest(frame->original, _streamRotation, useRequest)) {
		return frame->original;
	} else if (changed || none || i->second.image.isNull()) {
		const auto j = none
			? frame->prepared.emplace(instance, useRequest).first
			: i;
		if (changed && !none) {
			i->second.request = useRequest;
		}
		if (frame->prepared.size() > 1) {
			for (auto &[alreadyInstance, prepared] : frame->prepared) {
				if (alreadyInstance != instance
					&& prepared.request == useRequest
					&& !prepared.image.isNull()) {
					return prepared.image;
				}
			}
		}
		j->second.image = PrepareByRequest(
			frame->original,
			frame->alpha,
			_streamRotation,
			useRequest,
			std::move(j->second.image));
		return j->second.image;
	}
	return i->second.image;
}

FrameWithInfo VideoTrack::frameWithInfo(const Instance *instance) {
	const auto data = _shared->frameForPaintWithIndex();
	const auto i = data.frame->prepared.find(instance);
	const auto none = (i == data.frame->prepared.end());
	if (none || i->second.request.requireARGB32) {
		_wrapped.with([=](Implementation &unwrapped) {
			unwrapped.updateFrameRequest(
				instance,
				{ .requireARGB32 = false });
		});
	}
	return {
		.original = data.frame->original,
		.yuv420 = &data.frame->yuv420,
		.format = data.frame->format,
		.index = data.index,
	};
}

QImage VideoTrack::currentFrameImage() {
	const auto frame = _shared->frameForPaint();
	if (frame->original.isNull() && frame->format == FrameFormat::YUV420) {
		frame->original = ConvertToARGB32(frame->yuv420);
	}
	return frame->original;
}

void VideoTrack::unregisterInstance(not_null<const Instance*> instance) {
	_wrapped.with([=](Implementation &unwrapped) {
		unwrapped.removeFrameRequest(instance);
	});
}

void VideoTrack::PrepareFrameByRequests(
		not_null<Frame*> frame,
		int rotation) {
	Expects(frame->format != FrameFormat::ARGB32
		|| !frame->original.isNull());

	if (frame->format != FrameFormat::ARGB32) {
		return;
	}

	const auto begin = frame->prepared.begin();
	const auto end = frame->prepared.end();
	for (auto i = begin; i != end; ++i) {
		auto &prepared = i->second;
		if (frame->alpha
			|| !GoodForRequest(frame->original, rotation, prepared.request)) {
			auto j = begin;
			for (; j != i; ++j) {
				if (j->second.request == prepared.request) {
					prepared.image = QImage();
					break;
				}
			}
			if (j == i) {
				prepared.image = PrepareByRequest(
					frame->original,
					frame->alpha,
					rotation,
					prepared.request,
					std::move(prepared.image));
			}
		}
	}
}

bool VideoTrack::IsDecoded(not_null<const Frame*> frame) {
	return (frame->position != kTimeUnknown)
		&& (frame->displayed == kTimeUnknown);
}

bool VideoTrack::IsRasterized(not_null<const Frame*> frame) {
	return IsDecoded(frame)
		&& (!frame->original.isNull()
			|| frame->format == FrameFormat::YUV420);
}

bool VideoTrack::IsStale(not_null<const Frame*> frame, crl::time trackTime) {
	Expects(IsDecoded(frame));

	return (frame->position < trackTime);
}

rpl::producer<> VideoTrack::checkNextFrame() const {
	return _wrapped.producer_on_main([](const Implementation &unwrapped) {
		return unwrapped.checkNextFrame();
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
