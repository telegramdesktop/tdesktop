/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_player.h"

#include "media/streaming/media_streaming_file.h"
#include "media/streaming/media_streaming_loader.h"
#include "media/streaming/media_streaming_audio_track.h"
#include "media/streaming/media_streaming_video_track.h"
#include "media/audio/media_audio.h" // for SupportsSpeedControl()
#include "data/data_document.h" // for DocumentData::duration()

namespace Media {
namespace Streaming {
namespace {

constexpr auto kBufferFor = 3 * crl::time(1000);
constexpr auto kLoadInAdvanceForRemote = 32 * crl::time(1000);
constexpr auto kLoadInAdvanceForLocal = 5 * crl::time(1000);
constexpr auto kMsFrequency = 1000; // 1000 ms per second.

// If we played for 3 seconds and got stuck it looks like we're loading
// slower than we're playing, so load full file in that case.
//constexpr auto kLoadFullIfStuckAfterPlayback = 3 * crl::time(1000);

[[nodiscard]] bool FullTrackReceived(const TrackState &state) {
	return (state.duration != kTimeUnknown)
		&& (state.receivedTill == state.duration);
}

void SaveValidStateInformation(TrackState &to, TrackState &&from) {
	Expects(from.position != kTimeUnknown);
	Expects(from.receivedTill != kTimeUnknown);
	Expects(from.duration != kTimeUnknown);

	to.duration = from.duration;
	to.position = from.position;
	to.receivedTill = (to.receivedTill == kTimeUnknown)
		? from.receivedTill
		: std::clamp(
			std::max(from.receivedTill, to.receivedTill),
			to.position,
			to.duration);
}

void SaveValidAudioInformation(
		AudioInformation &to,
		AudioInformation &&from) {
	SaveValidStateInformation(to.state, std::move(from.state));
}

void SaveValidVideoInformation(
		VideoInformation &to,
		VideoInformation &&from) {
	Expects(!from.size.isEmpty());
	Expects(!from.cover.isNull());

	SaveValidStateInformation(to.state, std::move(from.state));
	to.size = from.size;
	to.cover = std::move(from.cover);
	to.rotation = from.rotation;
}

void SaveValidStartInformation(Information &to, Information &&from) {
	if (from.audio.state.duration != kTimeUnknown) {
		SaveValidAudioInformation(to.audio, std::move(from.audio));
	}
	if (from.video.state.duration != kTimeUnknown) {
		SaveValidVideoInformation(to.video, std::move(from.video));
	}
	if (from.headerSize && !to.headerSize) {
		to.headerSize = from.headerSize;
	}
}

} // namespace

Player::Player(std::shared_ptr<Reader> reader)
: _file(std::make_unique<File>(std::move(reader)))
, _remoteLoader(_file->isRemoteLoader())
, _renderFrameTimer([=] { checkNextFrameRender(); }) {
}

not_null<FileDelegate*> Player::delegate() {
	return static_cast<FileDelegate*>(this);
}

void Player::checkNextFrameRender() {
	Expects(_nextFrameTime != kTimeUnknown);

	const auto now = crl::now();
	if (now < _nextFrameTime) {
		if (!_renderFrameTimer.isActive()) {
			_renderFrameTimer.callOnce(_nextFrameTime - now);
		}
	} else {
		_renderFrameTimer.cancel();
		renderFrame(now);
	}
}

void Player::checkNextFrameAvailability() {
	Expects(_video != nullptr);

	_nextFrameTime = _video->nextFrameDisplayTime();
	Assert(_nextFrameTime != kFrameDisplayTimeAlreadyDone);
	if (_nextFrameTime != kTimeUnknown) {
		checkNextFrameRender();
	}
}

void Player::renderFrame(crl::time now) {
	Expects(_video != nullptr);
	Expects(_nextFrameTime != kTimeUnknown);
	Expects(_nextFrameTime != kFrameDisplayTimeAlreadyDone);

	const auto position = _video->markFrameDisplayed(now);
	if (_options.waitForMarkAsShown) {
		_currentFrameTime = _nextFrameTime;
		_nextFrameTime = kFrameDisplayTimeAlreadyDone;
	} else {
		_video->markFrameShown();
		_nextFrameTime = kTimeUnknown;
	}

	Assert(position != kTimeUnknown);
	videoPlayedTill(position);
}

bool Player::markFrameShown() {
	Expects(_video != nullptr);

	if (_nextFrameTime == kFrameDisplayTimeAlreadyDone) {
		_nextFrameTime = kTimeUnknown;
		_video->addTimelineDelay(crl::now() - _currentFrameTime);
	}
	return _video->markFrameShown();
}

void Player::setLoaderPriority(int priority) {
	_file->setLoaderPriority(priority);
}

template <typename Track>
void Player::trackReceivedTill(
		const Track &track,
		TrackState &state,
		crl::time position) {
	if (position == kTimeUnknown) {
		return;
	} else if (state.duration != kTimeUnknown) {
		if (state.receivedTill < position) {
			state.receivedTill = position;
			trackSendReceivedTill(track, state);
		}
	} else {
		state.receivedTill = position;
	}
	if (!_pauseReading
		&& bothReceivedEnough(loadInAdvanceFor())
		&& !receivedTillEnd()) {
		_pauseReading = true;
	}
}

template <typename Track>
void Player::trackPlayedTill(
		const Track &track,
		TrackState &state,
		crl::time position) {
	const auto guard = base::make_weak(&_sessionGuard);
	trackReceivedTill(track, state, position);
	if (guard && position != kTimeUnknown) {
		state.position = position;
		const auto value = _options.loop
			? (position % computeTotalDuration())
			: position;
		_updates.fire({ PlaybackUpdate<Track>{ value } });
	}
	if (_pauseReading
		&& (!bothReceivedEnough(loadInAdvanceFor()) || receivedTillEnd())) {
		_pauseReading = false;
		_file->wake();
	}
}

template <typename Track>
void Player::trackSendReceivedTill(
		const Track &track,
		TrackState &state) {
	Expects(state.duration != kTimeUnknown);
	Expects(state.receivedTill != kTimeUnknown);

	if (!_remoteLoader || _fullInCacheSinceStart.value_or(false)) {
		return;
	}
	const auto receivedTill = std::max(
		state.receivedTill,
		_previousReceivedTill);
	const auto value = _options.loop
		? (receivedTill % computeTotalDuration())
		: receivedTill;
	_updates.fire({ PreloadedUpdate<Track>{ value } });
}

void Player::audioReceivedTill(crl::time position) {
	Expects(_audio != nullptr);

	trackReceivedTill(*_audio, _information.audio.state, position);
	checkResumeFromWaitingForData();
}

void Player::audioPlayedTill(crl::time position) {
	Expects(_audio != nullptr);

	trackPlayedTill(*_audio, _information.audio.state, position);
}

void Player::videoReceivedTill(crl::time position) {
	Expects(_video != nullptr);

	trackReceivedTill(*_video, _information.video.state, position);
	checkResumeFromWaitingForData();
}

void Player::videoPlayedTill(crl::time position) {
	Expects(_video != nullptr);

	trackPlayedTill(*_video, _information.video.state, position);
}

bool Player::fileReady(int headerSize, Stream &&video, Stream &&audio) {
	_waitingForData = false;

	const auto weak = base::make_weak(&_sessionGuard);
	const auto ready = [=](const Information &data) {
		crl::on_main(weak, [=, data = data]() mutable {
			data.headerSize = headerSize;
			streamReady(std::move(data));
		});
	};
	const auto error = [=](Error error) {
		crl::on_main(weak, [=] {
			streamFailed(error);
		});
	};
	const auto mode = _options.mode;
	if ((mode != Mode::Audio && mode != Mode::Both)
		|| audio.duration == kDurationUnavailable) {
		audio = Stream();
	}
	if (mode != Mode::Video && mode != Mode::Both) {
		video = Stream();
	}
	if (audio.duration == kDurationUnavailable) {
		LOG(("Streaming Error: Audio stream with unknown duration."));
		return false;
	} else if (audio.codec) {
		if (_options.audioId.audio() != nullptr) {
			_audioId = AudioMsgId(
				_options.audioId.audio(),
				_options.audioId.contextId(),
				AudioMsgId::CreateExternalPlayId());
		} else {
			_audioId = AudioMsgId::ForVideo();
		}
		_audio = std::make_unique<AudioTrack>(
			_options,
			std::move(audio),
			_audioId,
			ready,
			error);
	} else if (audio.index >= 0) {
		LOG(("Streaming Error: No codec for audio stream %1, mode %2."
			).arg(audio.index
			).arg(int(mode)));
		return false;
	} else {
		_audioId = AudioMsgId();
	}
	if (video.codec) {
		_video = std::make_unique<VideoTrack>(
			_options,
			std::move(video),
			_audioId,
			ready,
			error);
	} else if (video.index >= 0) {
		LOG(("Streaming Error: No codec for video stream %1, mode %2."
			).arg(video.index
			).arg(int(mode)));
		return false;
	}
	if ((mode == Mode::Audio && !_audio)
		|| (mode == Mode::Video && !_video)
		|| (!_audio && !_video)) {
		LOG(("Streaming Error: Required stream not found for mode %1."
			).arg(int(mode)));
		return false;
	} else if (_audio
		&& _video
		&& _video->streamDuration() == kDurationUnavailable) {
		LOG(("Streaming Error: Both streams with unknown video duration."));
		return false;
	}
	_totalDuration = std::max(
		_audio ? _audio->streamDuration() : kTimeUnknown,
		_video ? _video->streamDuration() : kTimeUnknown);

	Ensures(_totalDuration > 1);
	return true;
}

void Player::fileError(Error error) {
	_waitingForData = false;

	crl::on_main(&_sessionGuard, [=] {
		fail(error);
	});
}

void Player::fileFullInCache(bool fullInCache) {
	crl::on_main(&_sessionGuard, [=] {
		if (!_fullInCacheSinceStart.has_value()) {
			_fullInCacheSinceStart = fullInCache;
		}
		_fullInCache.fire_copy(fullInCache);
	});
}

void Player::fileWaitingForData() {
	if (_waitingForData) {
		return;
	}
	_waitingForData = true;
	if (_audio) {
		_audio->waitForData();
	}
	if (_video) {
		_video->waitForData();
	}
}

bool Player::fileProcessPackets(
		base::flat_map<int, std::vector<FFmpeg::Packet>> &packets) {
	_waitingForData = false;
	for (auto &[index, list] : packets) {
		if (list.empty()) {
			continue;
		}
		if (_audio && _audio->streamIndex() == index) {
			//for (const auto &packet : list) {
			//	// Maybe it is enough to count by list.back()?.. hope so.
			//	accumulate_max(
			//		_durationByLastAudioPacket,
			//		durationByPacket(*_audio, packet));
			//}
			accumulate_max(
				_durationByLastAudioPacket,
				durationByPacket(*_audio, list.back()));
			const auto till = _loopingShift + std::clamp(
				FFmpeg::PacketPosition(
					list.back(),
					_audio->streamTimeBase()),
				crl::time(0),
				computeAudioDuration() - 1);
			crl::on_main(&_sessionGuard, [=] {
				audioReceivedTill(till);
			});
			_audio->process(base::take(list));
		} else if (_video && _video->streamIndex() == index) {
			//for (const auto &packet : list) {
			//	// Maybe it is enough to count by list.back()?.. hope so.
			//	accumulate_max(
			//		_durationByLastVideoPacket,
			//		durationByPacket(*_video, packet));
			//}
			accumulate_max(
				_durationByLastVideoPacket,
				durationByPacket(*_video, list.back()));
			const auto till = _loopingShift + std::clamp(
				FFmpeg::PacketPosition(
					list.back(),
					_video->streamTimeBase()),
				crl::time(0),
				computeVideoDuration() - 1);
			crl::on_main(&_sessionGuard, [=] {
				videoReceivedTill(till);
			});
			_video->process(base::take(list));
		} else {
			list.clear(); // Free non-needed packets.
		}
	}
	return fileReadMore();
}

void Player::fileProcessEndOfFile() {
	_waitingForData = false;
	_readTillEnd = true;
	setDurationByPackets();
	const auto generateEmptyQueue = [] {
		auto result = std::vector<FFmpeg::Packet>();
		result.emplace_back();
		return result;
	};
	if (_audio) {
		const auto till = _loopingShift + computeAudioDuration();
		crl::on_main(&_sessionGuard, [=] {
			audioReceivedTill(till);
		});
		_audio->process(generateEmptyQueue());
	}
	if (_video) {
		const auto till = _loopingShift + computeVideoDuration();
		crl::on_main(&_sessionGuard, [=] {
			videoReceivedTill(till);
		});
		_video->process(generateEmptyQueue());
	}
}

bool Player::fileReadMore() {
	if (_options.loop && _readTillEnd) {
		const auto duration = computeTotalDuration();
		if (duration == kDurationUnavailable) {
			LOG(("Streaming Error: "
				"Couldn't find out the real stream duration."));
			fileError(Error::InvalidData);
			return false;
		}
		_loopingShift += duration;
		_readTillEnd = false;
		return true;
	}
	return !_readTillEnd && !_pauseReading;
}

void Player::streamReady(Information &&information) {
	SaveValidStartInformation(_information, std::move(information));
	provideStartInformation();
}

void Player::streamFailed(Error error) {
	fail(error);
}

template <typename Track>
int Player::durationByPacket(
		const Track &track,
		const FFmpeg::Packet &packet) {
	// We've set this value on the first cycle.
	if (_loopingShift || _totalDuration != kDurationUnavailable) {
		return 0;
	}
	const auto result = DurationByPacket(packet, track.streamTimeBase());
	if (result < 0) {
		fileError(Error::InvalidData);
		return 0;
	}

	Ensures(result > 0);
	return result;
}

void Player::setDurationByPackets() {
	if (_loopingShift || _totalDuration != kDurationUnavailable) {
		return;
	}
	const auto duration = std::max(
		_durationByLastAudioPacket,
		_durationByLastVideoPacket);
	if (duration > 1) {
		_durationByPackets = duration;
	} else {
		LOG(("Streaming Error: Bad total duration by packets: %1"
			).arg(duration));
		fileError(Error::InvalidData);
	}
}

void Player::provideStartInformation() {
	Expects(_stage == Stage::Initializing);

	if ((_audio && _information.audio.state.duration == kTimeUnknown)
		|| (_video && _information.video.state.duration == kTimeUnknown)) {
		return; // Not ready yet.
	} else if ((!_audio && !_video)
		|| (!_audio && _options.mode == Mode::Audio)
		|| (!_video && _options.mode == Mode::Video)) {
		fail(Error::OpenFailed);
	} else {
		_stage = Stage::Ready;

		if (_audio && _audioFinished) {
			// Audio was stopped before it was ready.
			_audio->stop();
		}

		// Don't keep the reference to the video cover.
		auto copy = _information;
		_information.video.cover = QImage();

		_updates.fire(Update{ std::move(copy) });

		if (_stage == Stage::Ready && !_paused) {
			_paused = true;
			updatePausedState();
		}
	}
}

void Player::fail(Error error) {
	_sessionLifetime = rpl::lifetime();
	const auto stopGuarded = crl::guard(&_sessionGuard, [=] { stop(); });
	_lastFailure = error;
	_updates.fire_error(std::move(error));
	stopGuarded();
}

void Player::play(const PlaybackOptions &options) {
	Expects(options.speed >= 0.5 && options.speed <= 2.);

	// Looping video with audio is not supported for now.
	Expects(!options.loop || (options.mode != Mode::Both));

	const auto previous = getCurrentReceivedTill(computeTotalDuration());

	stop(true);
	_lastFailure = std::nullopt;

	savePreviousReceivedTill(options, previous);
	_options = options;
	if (!Media::Audio::SupportsSpeedControl()) {
		_options.speed = 1.;
	}
	_stage = Stage::Initializing;
	_file->start(delegate(), _options.position);
}

void Player::savePreviousReceivedTill(
		const PlaybackOptions &options,
		crl::time previousReceivedTill) {
	// Save previous 'receivedTill' values if we seek inside the range.
	_previousReceivedTill = ((options.position >= _options.position)
		&& (options.mode == _options.mode)
		&& (options.position < previousReceivedTill))
		? previousReceivedTill
		: kTimeUnknown;
}

crl::time Player::loadInAdvanceFor() const {
	return _remoteLoader ? kLoadInAdvanceForRemote : kLoadInAdvanceForLocal;
}

crl::time Player::computeTotalDuration() const {
	if (_totalDuration != kDurationUnavailable) {
		return _totalDuration;
	} else if (const auto byPackets = _durationByPackets.load()) {
		return byPackets;
	}
	return kDurationUnavailable;
}

crl::time Player::computeAudioDuration() const {
	Expects(_audio != nullptr);

	const auto result = _audio->streamDuration();
	if (result != kDurationUnavailable) {
		return result;
	} else if ((_loopingShift || _readTillEnd)
		&& _durationByLastAudioPacket) {
		// We looped, so it already holds full stream duration.
		return _durationByLastAudioPacket;
	}
	return kDurationUnavailable;
}

crl::time Player::computeVideoDuration() const {
	Expects(_video != nullptr);

	const auto result = _video->streamDuration();
	if (result != kDurationUnavailable) {
		return result;
	} else if ((_loopingShift || _readTillEnd)
		&& _durationByLastVideoPacket) {
		// We looped, so it already holds full stream duration.
		return _durationByLastVideoPacket;
	}
	return kDurationUnavailable;
}

void Player::pause() {
	Expects(active());

	_pausedByUser = true;
	updatePausedState();
}

void Player::resume() {
	Expects(active());

	_pausedByUser = false;
	updatePausedState();
}

void Player::stop() {
	stop(false);
}

void Player::stopAudio() {
	if (!_video) {
		stop();
	} else if (_audio) {
		_audioFinished = true;
		if (_information.audio.state.duration != kTimeUnknown) {
			// Audio is ready.
			_audio->stop();
		}
	}
}

void Player::updatePausedState() {
	const auto paused = _pausedByUser || _pausedByWaitingForData;
	if (_paused == paused) {
		return;
	}
	_paused = paused;
	if (!_paused && _stage == Stage::Ready) {
		const auto guard = base::make_weak(&_sessionGuard);
		start();
		if (!guard) {
			return;
		}
	}

	if (_stage != Stage::Started) {
		return;
	}
	if (_paused) {
		_pausedTime = crl::now();
		//if (_pausedByWaitingForData
		//	&& _pausedTime - _startedTime > kLoadFullIfStuckAfterPlayback) {
		//	_loadFull = true;
		//}
		if (_audio) {
			_audio->pause(_pausedTime);
		}
		if (_video) {
			_video->pause(_pausedTime);
		}
	} else {
		_startedTime = crl::now();
		if (_audio) {
			_audio->resume(_startedTime);
		}
		if (_video) {
			_video->resume(_startedTime);
		}
	}
}

bool Player::trackReceivedEnough(
		const TrackState &state,
		crl::time amount) const {
	return (!_options.loop && FullTrackReceived(state))
		|| (state.position != kTimeUnknown
			&& (state.position + std::min(amount, state.duration)
				<= state.receivedTill));
}

bool Player::bothReceivedEnough(crl::time amount) const {
	const auto &info = _information;
	return (!_audio || trackReceivedEnough(info.audio.state, amount))
		&& (!_video || trackReceivedEnough(info.video.state, amount));
}

bool Player::receivedTillEnd() const {
	if (_options.loop) {
		return false;
	}
	return (!_video || FullTrackReceived(_information.video.state))
		&& (!_audio || FullTrackReceived(_information.audio.state));
}

void Player::checkResumeFromWaitingForData() {
	if (_pausedByWaitingForData && bothReceivedEnough(kBufferFor)) {
		_pausedByWaitingForData = false;
		updatePausedState();
		_updates.fire({ WaitingForData{ false } });
	}
}

void Player::start() {
	Expects(_stage == Stage::Ready);

	_stage = Stage::Started;
	const auto guard = base::make_weak(&_sessionGuard);

	rpl::merge(
		_audio ? _audio->waitingForData() : nullptr,
		_video ? _video->waitingForData() : nullptr
	) | rpl::filter([=] {
		return !bothReceivedEnough(kBufferFor);
	}) | rpl::start_with_next([=] {
		_pausedByWaitingForData = true;
		updatePausedState();
		_updates.fire({ WaitingForData{ true } });
	}, _sessionLifetime);

	if (guard && _audio && !_audioFinished) {
		_audio->playPosition(
		) | rpl::start_with_next_done([=](crl::time position) {
			audioPlayedTill(position);
		}, [=] {
			Expects(_stage == Stage::Started);

			_audioFinished = true;
			if (!_video || _videoFinished) {
				_updates.fire({ Finished() });
			}
		}, _sessionLifetime);
	}

	if (guard && _video) {
		_video->checkNextFrame(
		) | rpl::start_with_next_done([=] {
			checkVideoStep();
		}, [=] {
			Assert(_stage == Stage::Started);

			_videoFinished = true;
			if (!_audio || _audioFinished) {
				_updates.fire({ Finished() });
			}
		}, _sessionLifetime);

		crl::on_main_update_requests(
		) | rpl::filter([=] {
			return !_videoFinished;
		}) | rpl::start_with_next([=] {
			checkVideoStep();
		}, _sessionLifetime);
	}
	if (guard && _audio) {
		if (_audioFinished) {
			if (!_video || _videoFinished) {
				_updates.fire({ Finished() });
			}
		} else {
			trackSendReceivedTill(*_audio, _information.audio.state);
		}
	}
	if (guard && _video) {
		trackSendReceivedTill(*_video, _information.video.state);
	}
}

void Player::checkVideoStep() {
	if (_nextFrameTime == kFrameDisplayTimeAlreadyDone) {
		return;
	} else if (_nextFrameTime != kTimeUnknown) {
		checkNextFrameRender();
	} else {
		checkNextFrameAvailability();
	}
}

void Player::stop(bool stillActive) {
	_file->stop(stillActive);
	_sessionLifetime = rpl::lifetime();
	_stage = Stage::Uninitialized;
	_audio = nullptr;
	_video = nullptr;
	invalidate_weak_ptrs(&_sessionGuard);
	_pausedByUser = _pausedByWaitingForData = _paused = false;
	_renderFrameTimer.cancel();
	_nextFrameTime = kTimeUnknown;
	_audioFinished = false;
	_videoFinished = false;
	_pauseReading = false;
	_readTillEnd = false;
	_loopingShift = 0;
	_durationByPackets = 0;
	_durationByLastAudioPacket = 0;
	_durationByLastVideoPacket = 0;
	const auto header = _information.headerSize;
	_information = Information();
	_information.headerSize = header;
}

std::optional<Error> Player::failed() const {
	return _lastFailure;
}

bool Player::playing() const {
	return (_stage == Stage::Started)
		&& !paused()
		&& !finished()
		&& !failed();
}

bool Player::buffering() const {
	return _pausedByWaitingForData;
}

bool Player::paused() const {
	return _pausedByUser && active();
}

bool Player::finished() const {
	return (_stage == Stage::Started)
		&& (!_audio || _audioFinished)
		&& (!_video || _videoFinished);
}

float64 Player::speed() const {
	return _options.speed;
}

void Player::setSpeed(float64 speed) {
	Expects(speed >= 0.5 && speed <= 2.);

	if (!Media::Audio::SupportsSpeedControl()) {
		speed = 1.;
	}
	if (_options.speed != speed) {
		_options.speed = speed;
		if (active()) {
			if (_audio) {
				_audio->setSpeed(speed);
			}
			if (_video) {
				_video->setSpeed(speed);
			}
		}
	}
}

void Player::setWaitForMarkAsShown(bool wait) {
	if (_options.waitForMarkAsShown != wait) {
		_options.waitForMarkAsShown = wait;
		if (_video) {
			_video->setWaitForMarkAsShown(wait);
		}
	}
}

bool Player::active() const {
	return (_stage != Stage::Uninitialized) && !finished() && !failed();
}

bool Player::ready() const {
	return (_stage != Stage::Uninitialized)
		&& (_stage != Stage::Initializing);
}

rpl::producer<Update, Error> Player::updates() const {
	return _updates.events();
}

rpl::producer<bool> Player::fullInCache() const {
	return _fullInCache.events();
}

QSize Player::videoSize() const {
	return _information.video.size;
}

QImage Player::frame(
		const FrameRequest &request,
		const Instance *instance) const {
	Expects(_video != nullptr);

	return _video->frame(request, instance);
}

FrameWithInfo Player::frameWithInfo(const Instance *instance) const {
	Expects(_video != nullptr);

	return _video->frameWithInfo(instance);
}

QImage Player::currentFrameImage() const {
	Expects(_video != nullptr);

	return _video->currentFrameImage();
}

void Player::unregisterInstance(not_null<const Instance*> instance) {
	if (_video) {
		_video->unregisterInstance(instance);
	}
}

Media::Player::TrackState Player::prepareLegacyState() const {
	using namespace Media::Player;

	auto result = Media::Player::TrackState();
	result.id = _audioId.externalPlayId() ? _audioId : _options.audioId;
	result.state = (_lastFailure == Error::OpenFailed
		|| _lastFailure == Error::NotStreamable)
		? State::StoppedAtStart
		: _lastFailure
		? State::StoppedAtError
		: finished()
		? State::StoppedAtEnd
		: (_stage == Stage::Uninitialized)
		? State::Stopped
		: paused()
		? State::Paused
		: State::Playing;
	result.position = std::max(
		_information.audio.state.position,
		_information.video.state.position);
	result.length = computeTotalDuration();
	if (result.position == kTimeUnknown) {
		result.position = _options.position;
	} else if (_options.loop && result.length > 0) {
		result.position %= result.length;
	}
	result.receivedTill = (_remoteLoader
		&& !_fullInCacheSinceStart.value_or(false))
		? getCurrentReceivedTill(result.length)
		: 0;
	result.frequency = kMsFrequency;
	result.fileHeaderSize = _information.headerSize;

	if (result.length == kTimeUnknown) {
		const auto document = _options.audioId.audio();
		const auto duration = document ? document->getDuration() : 0;
		if (duration > 0) {
			result.length = duration * crl::time(1000);
		} else {
			result.length = std::max(crl::time(result.position), crl::time(0));
		}
	}
	return result;
}

crl::time Player::getCurrentReceivedTill(crl::time duration) const {
	const auto forTrack = [&](const TrackState &state) {
		return (state.duration > 0 && state.receivedTill == state.duration)
			? std::max(state.receivedTill, duration)
			: state.receivedTill;
	};
	const auto previous = std::max(_previousReceivedTill, crl::time(0));
	const auto result = std::min(
		std::max(forTrack(_information.audio.state), previous),
		std::max(forTrack(_information.video.state), previous));
	return (result >= 0 && duration > 1 && _options.loop)
		? (result % duration)
		: result;
}

void Player::lock() {
	++_locks;
}

void Player::unlock() {
	Expects(_locks > 0);

	--_locks;
	if (!_locks) {
		stopAudio();
		if (active()) {
			setSpeed(1.);
		}
		setWaitForMarkAsShown(true);
	}
}

bool Player::locked() const {
	return (_locks > 0);
}

rpl::lifetime &Player::lifetime() {
	return _lifetime;
}

Player::~Player() {
	// The order of field destruction is important.
	//
	// We are forced to maintain the correct order in the stop() method,
	// because it can be called even before the player destruction.
	//
	// So instead of maintaining it in the class definition as well we
	// simply call stop() here, after that the destruction is trivial.
	stop();
}

} // namespace Streaming
} // namespace Media
