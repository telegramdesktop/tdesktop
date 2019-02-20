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

namespace Media {
namespace Streaming {
namespace {

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

void Player::start() {
	Expects(_stage == Stage::Ready);

	_stage = Stage::Started;
	if (_audio) {
		_audio->playPosition(
		) | rpl::start_with_next_done([=](crl::time position) {
			audioPlayedTill(position);
		}, [=] {
			// audio finished
		}, _lifetime);
	}
	if (_video) {
		_video->renderNextFrame(
		) | rpl::start_with_next_done([=](crl::time when) {
			_nextFrameTime = when;
			checkNextFrame();
		}, [=] {
			// video finished
		}, _lifetime);
	}
	if (_audio) {
		_audio->start();
	}
	if (_video) {
		_video->start();
	}
}

void Player::checkNextFrame() {
	Expects(_nextFrameTime != kTimeUnknown);

	const auto now = crl::now();
	if (now < _nextFrameTime) {
		_renderFrameTimer.callOnce(_nextFrameTime - now);
	} else {
		_renderFrameTimer.cancel();
		renderFrame(now);
	}
}

void Player::renderFrame(crl::time now) {
	if (_video) {
		const auto position = _video->markFrameDisplayed(now);
		if (position != kTimeUnknown) {
			videoPlayedTill(position);
		}
	}
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
			_updates.fire({ PreloadedUpdate<Track>{ position } });
		}
	} else {
		state.receivedTill = position;
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
		position = std::clamp(position, 0LL, state.duration);
		state.position = position;
		_updates.fire({ PlaybackUpdate<Track>{ position } });
	}
}

void Player::audioReceivedTill(crl::time position) {
	Expects(_audio != nullptr);

	trackReceivedTill(*_audio, _information.audio.state, position);
}

void Player::audioPlayedTill(crl::time position) {
	Expects(_audio != nullptr);

	trackPlayedTill(*_audio, _information.audio.state, position);
}

void Player::videoReceivedTill(crl::time position) {
	Expects(_video != nullptr);

	trackReceivedTill(*_video, _information.video.state, position);
}

void Player::videoPlayedTill(crl::time position) {
	Expects(_video != nullptr);

	trackPlayedTill(*_video, _information.video.state, position);
}

void Player::fileReady(Stream &&video, Stream &&audio) {
	const auto weak = base::make_weak(&_sessionGuard);
	const auto ready = [=](const Information & data) {
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
	if (audio.codec && (_mode == Mode::Audio || _mode == Mode::Both)) {
		_audio = std::make_unique<AudioTrack>(
			std::move(audio),
			ready,
			error(_audio));
	}
	if (video.codec && (_mode == Mode::Video || _mode == Mode::Both)) {
		_video = std::make_unique<VideoTrack>(
			std::move(video),
			ready,
			error(_video));
	}
	if ((_mode == Mode::Audio && !_audio)
		|| (_mode == Mode::Video && !_video)
		|| (!_audio && !_video)) {
		LOG(("Streaming Error: Required stream not found for mode %1."
			).arg(int(_mode)));
		fileError();
	}
}

void Player::fileError() {
	crl::on_main(&_sessionGuard, [=] {
		fail();
	});
}

bool Player::fileProcessPacket(Packet &&packet) {
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
	return !_readTillEnd;
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
		|| (!_audio && _mode == Mode::Audio)
		|| (!_video && _mode == Mode::Video)) {
		fail();
	} else {
		_stage = Stage::Ready;

		// Don't keep the reference to the video cover.
		auto copy = _information;
		_information.video.cover = QImage();

		_updates.fire(Update{ std::move(copy) });
	}
}

void Player::fail() {
	const auto stopGuarded = crl::guard(&_sessionGuard, [=] { stop(); });
	_stage = Stage::Failed;
	_updates.fire_error({});
	stopGuarded();
}

void Player::init(Mode mode, crl::time position) {
	stop();

	_mode = mode;
	_stage = Stage::Initializing;
	_file->start(delegate(), position);
}

void Player::pause() {
	_paused = true;
	// #TODO streaming pause
}

void Player::resume() {
	_paused = false;
	// #TODO streaming pause
}

void Player::stop() {
	_file->stop();
	_audio = nullptr;
	_video = nullptr;
	_paused = false;
	invalidate_weak_ptrs(&_sessionGuard);
	if (_stage != Stage::Failed) {
		_stage = Stage::Uninitialized;
	}
	_updates = rpl::event_stream<Update, Error>();
}

bool Player::failed() const {
	return (_stage == Stage::Failed);
}

bool Player::playing() const {
	return (_stage == Stage::Started) && !_paused;
}

bool Player::paused() const {
	return _paused;
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

Player::~Player() = default;

} // namespace Streaming
} // namespace Media
