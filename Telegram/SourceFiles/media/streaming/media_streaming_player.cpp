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

void SaveValidInformation(Information &to, Information &&from) {
	if (from.state.audio.position != kTimeUnknown) {
		to.state.audio = from.state.audio;
	}
	if (from.audioDuration != kTimeUnknown) {
		to.audioDuration = from.audioDuration;
	}
	if (from.state.video.position != kTimeUnknown) {
		to.state.video = from.state.video;
	}
	if (from.videoDuration != kTimeUnknown) {
		to.videoDuration = from.videoDuration;
	}
	if (!from.videoSize.isEmpty()) {
		to.videoSize = from.videoSize;
	}
	if (!from.videoCover.isNull()) {
		to.videoCover = std::move(from.videoCover);
		to.videoRotation = from.videoRotation;
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
	//if (_audio) {
	//	_audio->state(
	//	) | rpl::start_with_next([](const TrackState & state) {
	//	}, _lifetime);
	//}
	if (_video) {
		_video->renderNextFrame(
		) | rpl::start_with_next([=](crl::time when) {
			_nextFrameTime = when;
			checkNextFrame();
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
		_video->markFrameDisplayed(now);
		_updates.fire({ UpdateVideo{ _nextFrameTime } });
	}
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
			_audio->process(Packet());
		}
		if (_video) {
			_video->process(Packet());
		}
	} else if (_audio && _audio->streamIndex() == native.stream_index) {
		_audio->process(std::move(packet));
	} else if (_video && _video->streamIndex() == native.stream_index) {
		_video->process(std::move(packet));
	}
	return fileReadMore();
}

bool Player::fileReadMore() {
	// return true if looping.
	return !_readTillEnd;
}

void Player::streamReady(Information &&information) {
	SaveValidInformation(_information, std::move(information));
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

	if ((_audio && _information.audioDuration == kTimeUnknown)
		|| (_video && _information.videoDuration == kTimeUnknown)) {
		return; // Not ready yet.
	} else if ((!_audio && !_video)
		|| (!_audio && _mode == Mode::Audio)
		|| (!_video && _mode == Mode::Video)) {
		fail();
	} else {
		_stage = Stage::Ready;
		_updates.fire(Update{ std::move(_information) });
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
