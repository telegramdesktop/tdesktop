/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_player.h"

#include "media/streaming/media_streaming_file.h"
#include "media/streaming/media_streaming_loader.h"
#include "media/audio/media_audio.h"
#include "media/audio/media_child_ffmpeg_loader.h"

namespace Media {
namespace Streaming {

crl::time CountPacketPosition(
	not_null<const AVStream*> info,
	const Packet &packet) {
	const auto &native = packet.fields();
	const auto packetPts = (native.pts == AV_NOPTS_VALUE)
		? native.dts
		: native.pts;
	const auto & timeBase = info->time_base;
	return PtsToTime(packetPts, info->time_base);
}

// #TODO streaming
//void Enqueue(StreamWrap &wrap, Packet && packet) {
//	const auto time = CountPacketPosition(wrap, packet);
//	if (time != kTimeUnknown) {
//		wrap.stream.lastReadPosition = time;
//	}
//	wrap.stream.queue.push_back(std::move(packet));
//}

Player::Player(
	not_null<Data::Session*> owner,
	std::unique_ptr<Loader> loader)
: _file(std::make_unique<File>(owner, std::move(loader))) {
}

not_null<FileDelegate*> Player::delegate() {
	return static_cast<FileDelegate*>(this);
}

void Player::fileReady(Stream &&video, Stream &&audio) {
	_audio = std::move(audio);
	if (_audio.codec && (_mode == Mode::Audio || _mode == Mode::Both)) {
		_audioMsgId = AudioMsgId::ForVideo();
	} else {
		_audioMsgId = AudioMsgId();
	}
}

void Player::fileError() {

}

bool Player::fileProcessPacket(Packet &&packet) {
	const auto &native = packet.fields();
	if (packet.empty()) {
		_readTillEnd = true;
	} else if (native.stream_index == _audio.index) {
		if (_audioMsgId.playId()) {
			if (_audio.codec) {
				const auto position = PtsToTime(native.pts, _audio.timeBase);

				auto data = std::make_unique<VideoSoundData>();
				data->context = _audio.codec.release();
				data->frequency = _audio.frequency;
				data->length = (_audio.duration * data->frequency) / 1000LL;
				Media::Player::mixer()->play(_audioMsgId, std::move(data), position);

				// #TODO streaming resume when started playing
				Media::Player::mixer()->resume(_audioMsgId, true);
			}
			Media::Player::mixer()->feedFromVideo({ &native, _audioMsgId });
			packet.release();
		}
		//const auto position = PtsToTime(native.pts, stream->timeBase);
	}
	return fileReadMore();
}

bool Player::fileReadMore() {
	// return true if looping.
	return !_readTillEnd;
}

void Player::init(Mode mode, crl::time position) {
	stop();

	_mode = mode;
	_file->start(delegate(), position);
}

void Player::pause() {

}

void Player::resume() {

}

void Player::stop() {
	_file->stop();
	_updates = rpl::event_stream<Update, Error>();
}

bool Player::playing() const {
	return false;
}

rpl::producer<Update, Error> Player::updates() const {
	return _updates.events();
}

Player::~Player() = default;

} // namespace Streaming
} // namespace Media
