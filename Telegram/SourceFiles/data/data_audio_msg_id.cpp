/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_audio_msg_id.h"

#include "data/data_document.h"

namespace {

constexpr auto kMinLengthForChangeablePlaybackSpeed = 20 * TimeId(60); // 20 minutes.

} // namespace

AudioMsgId::AudioMsgId() {
}

AudioMsgId::AudioMsgId(
	not_null<DocumentData*> audio,
	FullMsgId msgId,
	uint32 externalPlayId)
: _audio(audio)
, _contextId(msgId)
, _externalPlayId(externalPlayId)
, _changeablePlaybackSpeed(_audio->isVoiceMessage()
	|| _audio->isVideoMessage()
	|| (_audio->getDuration() >= kMinLengthForChangeablePlaybackSpeed)) {
	setTypeFromAudio();
}

uint32 AudioMsgId::CreateExternalPlayId() {
	static auto Result = uint32(0);
	return ++Result ? Result : ++Result;
}

AudioMsgId AudioMsgId::ForVideo() {
	auto result = AudioMsgId();
	result._externalPlayId = CreateExternalPlayId();
	result._type = Type::Video;
	return result;
}

void AudioMsgId::setTypeFromAudio() {
	if (_audio->isVoiceMessage() || _audio->isVideoMessage()) {
		_type = Type::Voice;
	} else if (_audio->isVideoFile()) {
		_type = Type::Video;
	} else if (_audio->isAudioFile()) {
		_type = Type::Song;
	} else {
		_type = Type::Unknown;
	}
}

AudioMsgId::Type AudioMsgId::type() const {
	return _type;
}

DocumentData *AudioMsgId::audio() const {
	return _audio;
}

FullMsgId AudioMsgId::contextId() const {
	return _contextId;
}

uint32 AudioMsgId::externalPlayId() const {
	return _externalPlayId;
}

bool AudioMsgId::changeablePlaybackSpeed() const {
	return _changeablePlaybackSpeed;
}

AudioMsgId::operator bool() const {
	return (_audio != nullptr) || (_externalPlayId != 0);
}

bool AudioMsgId::operator<(const AudioMsgId &other) const {
	if (quintptr(audio()) < quintptr(other.audio())) {
		return true;
	} else if (quintptr(other.audio()) < quintptr(audio())) {
		return false;
	} else if (contextId() < other.contextId()) {
		return true;
	} else if (other.contextId() < contextId()) {
		return false;
	}
	return (externalPlayId() < other.externalPlayId());
}

bool AudioMsgId::operator==(const AudioMsgId &other) const {
	return (audio() == other.audio())
		&& (contextId() == other.contextId())
		&& (externalPlayId() == other.externalPlayId());
}

bool AudioMsgId::operator!=(const AudioMsgId &other) const {
	return !(*this == other);
}
