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

namespace Media {
namespace Streaming {
namespace {

constexpr auto kReceivedTillEnd = std::numeric_limits<crl::time>::max();
constexpr auto kBufferFor = 3 * crl::time(1000);
constexpr auto kLoadInAdvanceFor = 64 * crl::time(1000);

// If we played for 3 seconds and got stuck it looks like we're loading
// slower than we're playing, so load full file in that case.
constexpr auto kLoadFullIfStuckAfterPlayback = 3 * crl::time(1000);

[[nodiscard]] crl::time TrackClampReceivedTill(
		crl::time position,
		const TrackState &state) {
	return (state.duration == kTimeUnknown || position == kTimeUnknown)
		? position
		: (position == kReceivedTillEnd)
		? state.duration
		: std::clamp(position, 0LL, state.duration - 1);
}

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
}

} // namespace

Player::Player(
	not_null<Data::Session*> owner,
	std::unique_ptr<Loader> loader)
: _file(std::make_unique<File>(owner, std::move(loader)))
, _renderFrameTimer([=] { checkNextFrame(); }) {
}

not_null<FileDelegate*> Player::delegate() {
	return static_cast<FileDelegate*>(this);
}

void Player::checkNextFrame() {
	Expects(_nextFrameTime != kTimeUnknown);
	Expects(!_renderFrameTimer.isActive());

	const auto now = crl::now();
	if (now < _nextFrameTime) {
		_renderFrameTimer.callOnce(_nextFrameTime - now);
	} else {
		renderFrame(now);
	}
}

void Player::renderFrame(crl::time now) {
	Expects(_video != nullptr);

	const auto position = _video->markFrameDisplayed(now);
	Assert(position != kTimeUnknown);

	videoPlayedTill(position);
}

template <typename Track>
void Player::trackReceivedTill(
		const Track &track,
		TrackState &state,
		crl::time position) {
	if (position == kTimeUnknown) {
		return;
	} else if (state.duration != kTimeUnknown) {
		position = std::clamp(position, 0LL, state.duration);
		if (state.receivedTill < position) {
			state.receivedTill = position;
			trackSendReceivedTill(track, state);
		}
	} else {
		state.receivedTill = position;
	}
	if (!_pauseReading && bothReceivedEnough(kLoadInAdvanceFor)) {
		_pauseReading = true;
	}
}

static auto wakes = 0;
template <typename Track>
void Player::trackPlayedTill(
		const Track &track,
		TrackState &state,
		crl::time position) {
	const auto guard = base::make_weak(&_sessionGuard);
	trackReceivedTill(track, state, position);
	if (guard && position != kTimeUnknown) {
		position = std::clamp(position, 0LL, state.duration);
		state.position = position;
		_updates.fire({ PlaybackUpdate<Track>{ position } });
	}
	if (_pauseReading && !bothReceivedEnough(kLoadInAdvanceFor)) {
		_pauseReading = false;
		_file->wake();
		++wakes;
	}
}

template <typename Track>
void Player::trackSendReceivedTill(
		const Track &track,
		TrackState &state) {
	Expects(state.duration != kTimeUnknown);
	Expects(state.receivedTill != kTimeUnknown);

	_updates.fire({ PreloadedUpdate<Track>{ state.receivedTill } });
}

void Player::audioReceivedTill(crl::time position) {
	Expects(_audio != nullptr);

	position = TrackClampReceivedTill(position, _information.audio.state);
	trackReceivedTill(*_audio, _information.audio.state, position);
	checkResumeFromWaitingForData();
}

void Player::audioPlayedTill(crl::time position) {
	Expects(_audio != nullptr);

	trackPlayedTill(*_audio, _information.audio.state, position);
}

void Player::videoReceivedTill(crl::time position) {
	Expects(_video != nullptr);

	position = TrackClampReceivedTill(position, _information.video.state);
	trackReceivedTill(*_video, _information.video.state, position);
}

void Player::videoPlayedTill(crl::time position) {
	Expects(_video != nullptr);

	trackPlayedTill(*_video, _information.video.state, position);
}

void Player::fileReady(Stream &&video, Stream &&audio) {
	_waitingForData = false;

	const auto weak = base::make_weak(&_sessionGuard);
	const auto ready = [=](const Information &data) {
		crl::on_main(weak, [=, data = data]() mutable {
			streamReady(std::move(data));
		});
	};
	const auto error = [&](auto &stream) {
		return [=, &stream] {
			crl::on_main(weak, [=, &stream] {
				stream = nullptr;
				streamFailed();
			});
		};
	};
	const auto mode = _options.mode;
	if (audio.codec && (mode == Mode::Audio || mode == Mode::Both)) {
		_audioId = AudioMsgId::ForVideo();
		_audio = std::make_unique<AudioTrack>(
			_options,
			std::move(audio),
			_audioId,
			ready,
			error(_audio));
	} else {
		_audioId = AudioMsgId();
	}
	if (video.codec && (mode == Mode::Video || mode == Mode::Both)) {
		_video = std::make_unique<VideoTrack>(
			_options,
			std::move(video),
			_audioId,
			ready,
			error(_video));
	}
	if ((mode == Mode::Audio && !_audio)
		|| (mode == Mode::Video && !_video)
		|| (!_audio && !_video)) {
		LOG(("Streaming Error: Required stream not found for mode %1."
			).arg(int(mode)));
		fileError();
	}
}

void Player::fileError() {
	_waitingForData = false;

	crl::on_main(&_sessionGuard, [=] {
		fail();
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

bool Player::fileProcessPacket(Packet &&packet) {
	_waitingForData = false;

	const auto &native = packet.fields();
	const auto index = native.stream_index;
	if (packet.empty()) {
		_readTillEnd = true;
		if (_audio) {
			crl::on_main(&_sessionGuard, [=] {
				audioReceivedTill(kReceivedTillEnd);
			});
			_audio->process(Packet());
		}
		if (_video) {
			crl::on_main(&_sessionGuard, [=] {
				videoReceivedTill(kReceivedTillEnd);
			});
			_video->process(Packet());
		}
	} else if (_audio && _audio->streamIndex() == native.stream_index) {
		const auto time = PacketPosition(packet, _audio->streamTimeBase());
		crl::on_main(&_sessionGuard, [=] {
			audioReceivedTill(time);
		});
		_audio->process(std::move(packet));
	} else if (_video && _video->streamIndex() == native.stream_index) {
		const auto time = PacketPosition(packet, _video->streamTimeBase());
		crl::on_main(&_sessionGuard, [=] {
			videoReceivedTill(time);
		});
		_video->process(std::move(packet));
	}
	return fileReadMore();
}

bool Player::fileReadMore() {
	// return true if looping.
	return !_readTillEnd && !_pauseReading;
}

void Player::streamReady(Information &&information) {
	SaveValidStartInformation(_information, std::move(information));
	provideStartInformation();
}

void Player::streamFailed() {
	if (_stage == Stage::Initializing) {
		provideStartInformation();
	} else {
		fail();
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
		fail();
	} else {
		_stage = Stage::Ready;

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

void Player::fail() {
	_sessionLifetime = rpl::lifetime();
	const auto stopGuarded = crl::guard(&_sessionGuard, [=] { stop(); });
	_stage = Stage::Failed;
	_updates.fire_error({});
	stopGuarded();
}

void Player::play(const PlaybackOptions &options) {
	Expects(options.speed >= 0.5 && options.speed <= 2.);

	stop();

	_options = options;
	if (!Media::Audio::SupportsSpeedControl()) {
		_options.speed = 1.;
	}
	_stage = Stage::Initializing;
	_file->start(delegate(), _options.position);
}

void Player::pause() {
	Expects(valid());

	_pausedByUser = true;
	updatePausedState();
}

void Player::resume() {
	Expects(valid());

	_pausedByUser = false;
	updatePausedState();
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
	return FullTrackReceived(state)
		|| (state.position != kTimeUnknown
			&& state.position + amount <= state.receivedTill);
}

bool Player::bothReceivedEnough(crl::time amount) const {
	auto &info = _information;
	return (!_audio || trackReceivedEnough(info.audio.state, amount))
		&& (!_video || trackReceivedEnough(info.video.state, amount));
}

void Player::checkResumeFromWaitingForData() {
	if (_pausedByWaitingForData && bothReceivedEnough(kBufferFor)) {
		_pausedByWaitingForData = false;
		updatePausedState();
	}
}

void Player::start() {
	Expects(_stage == Stage::Ready);

	_stage = Stage::Started;
	const auto guard = base::make_weak(&_sessionGuard);

	rpl::merge(
		_audio ? _audio->waitingForData() : rpl::never(),
		_video ? _video->waitingForData() : rpl::never()
	) | rpl::filter([=] {
		return !FullTrackReceived(_information.video.state)
			|| !FullTrackReceived(_information.audio.state);
	}) | rpl::start_with_next([=] {
		_pausedByWaitingForData = true;
		updatePausedState();
		_updates.fire({ WaitingForData() });
	}, _sessionLifetime);

	if (guard && _audio) {
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
		_video->renderNextFrame(
		) | rpl::start_with_next_done([=](crl::time when) {
			_nextFrameTime = when;
			checkNextFrame();
		}, [=] {
			Expects(_stage == Stage::Started);

			_videoFinished = true;
			if (!_audio || _audioFinished) {
				_updates.fire({ Finished() });
			}
		}, _sessionLifetime);
	}
	if (guard && _audio) {
		trackSendReceivedTill(*_audio, _information.audio.state);
	}
	if (guard && _video) {
		trackSendReceivedTill(*_video, _information.video.state);
	}
}

void Player::stop() {
	_file->stop();
	_sessionLifetime = rpl::lifetime();
	if (_stage != Stage::Failed) {
		_stage = Stage::Uninitialized;
	}
	_audio = nullptr;
	_video = nullptr;
	invalidate_weak_ptrs(&_sessionGuard);
	_pausedByUser = _pausedByWaitingForData = _paused = false;
	_renderFrameTimer.cancel();
	_audioFinished = false;
	_videoFinished = false;
	_pauseReading = false;
	_readTillEnd = false;
	_information = Information();
}

bool Player::failed() const {
	return (_stage == Stage::Failed);
}

bool Player::playing() const {
	return (_stage == Stage::Started) && !_paused && !finished();
}

bool Player::buffering() const {
	return _pausedByWaitingForData;
}

bool Player::paused() const {
	return _pausedByUser;
}

bool Player::finished() const {
	return (_stage == Stage::Started)
		&& (!_audio || _audioFinished)
		&& (!_video || _videoFinished);
}

void Player::setSpeed(float64 speed) {
	Expects(valid());
	Expects(speed >= 0.5 && speed <= 2.);

	if (!Media::Audio::SupportsSpeedControl()) {
		speed = 1.;
	}
	if (_options.speed != speed) {
		_options.speed = speed;
		if (_audio) {
			_audio->setSpeed(speed);
		}
		if (_video) {
			_video->setSpeed(speed);
		}
	}
}

bool Player::valid() const {
	return (_stage != Stage::Uninitialized) && (_stage != Stage::Failed);
}

bool Player::ready() const {
	return valid() && (_stage != Stage::Initializing);
}

rpl::producer<Update, Error> Player::updates() const {
	return _updates.events();
}

QImage Player::frame(const FrameRequest &request) const {
	Expects(_video != nullptr);

	return _video->frame(request);
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
	LOG(("WAKES: %1").arg(wakes));
	stop();
}

} // namespace Streaming
} // namespace Media
