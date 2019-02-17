/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_audio_track.h"

#include "media/streaming/media_streaming_utility.h"
#include "media/audio/media_audio.h"
#include "media/audio/media_child_ffmpeg_loader.h"
#include "media/player/media_player_instance.h"

namespace Media {
namespace Streaming {

AudioTrack::AudioTrack(
	Stream &&stream,
	FnMut<void(const Information &)> ready,
	Fn<void()> error)
: _stream(std::move(stream))
, _ready(std::move(ready))
, _error(std::move(error)) {
	Expects(_ready != nullptr);
	Expects(_error != nullptr);
}

int AudioTrack::streamIndex() const {
	// Thread-safe, because _stream.index is immutable.
	return _stream.index;
}

AVRational AudioTrack::streamTimeBase() const {
	return _stream.timeBase;
}

void AudioTrack::process(Packet &&packet) {
	if (_audioMsgId.playId()) {
		mixerEnqueue(std::move(packet));
	} else if (!tryReadFirstFrame(std::move(packet))) {
		_error();
	}
}

bool AudioTrack::tryReadFirstFrame(Packet &&packet) {
	// #TODO streaming fix seek to the end.
	const auto last = packet.empty();
	if (ProcessPacket(_stream, std::move(packet)).failed()) {
		return false;
	}
	if (const auto error = ReadNextFrame(_stream)) {
		return !last && (error.code() == AVERROR(EAGAIN));
	}
	if (!fillStateFromFrame()) {
		return false;
	}
	mixerInit();
	callReady();
	return true;
}

bool AudioTrack::fillStateFromFrame() {
	_state.position = _state.receivedTill = FramePosition(_stream);
	return (_state.position != kTimeUnknown);
}

void AudioTrack::mixerInit() {
	Expects(!_audioMsgId.playId());

	_audioMsgId = AudioMsgId::ForVideo();

	auto data = std::make_unique<VideoSoundData>();
	data->context = _stream.codec.release();
	data->frequency = _stream.frequency;
	data->length = (_stream.duration * data->frequency) / 1000LL;
	Media::Player::mixer()->play(
		_audioMsgId,
		std::move(data),
		_state.position);
}

void AudioTrack::callReady() {
	Expects(_ready != nullptr);

	auto data = Information();
	data.audioDuration = _stream.duration;
	data.state.audio = _state;
	base::take(_ready)(data);
}

void AudioTrack::mixerEnqueue(Packet &&packet) {
	Media::Player::mixer()->feedFromVideo({
		&packet.fields(),
		_audioMsgId
	});
	packet.release();
}

void AudioTrack::start() {
	Expects(_ready == nullptr);
	Expects(_audioMsgId.playId() != 0);
	// #TODO streaming support start() when paused.
	Media::Player::mixer()->resume(_audioMsgId, true);
}

rpl::producer<TrackState, Error> AudioTrack::state() {
	Expects(_ready == nullptr);

	if (!_subscription) {
		auto &updated = Media::Player::instance()->updatedNotifier();
		_subscription = updated.add_subscription([=](
				const Media::Player::TrackState &state) {
//			_state = state;
		});
		//) | rpl::filter([](const State &state) {
		//	return !!state.id;
		//});
	}
	return rpl::single<const TrackState&, Error>(_state);
}

AudioTrack::~AudioTrack() {
	if (_audioMsgId.playId()) {
		Media::Player::mixer()->stop(_audioMsgId);
	}
}

} // namespace Streaming
} // namespace Media
