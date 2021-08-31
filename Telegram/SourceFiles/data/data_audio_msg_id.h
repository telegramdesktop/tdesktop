/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class DocumentData;

class AudioMsgId final {
public:
	enum class Type {
		Unknown,
		Voice,
		Song,
		Video,
	};

	AudioMsgId();
	AudioMsgId(
		not_null<DocumentData*> audio,
		FullMsgId msgId,
		uint32 externalPlayId = 0);

	[[nodiscard]] static uint32 CreateExternalPlayId();
	[[nodiscard]] static AudioMsgId ForVideo();

	[[nodiscard]] Type type() const;
	[[nodiscard]] DocumentData *audio() const;
	[[nodiscard]] FullMsgId contextId() const;
	[[nodiscard]] uint32 externalPlayId() const;
	[[nodiscard]] bool changeablePlaybackSpeed() const;
	[[nodiscard]] explicit operator bool() const;

	bool operator<(const AudioMsgId &other) const;
	bool operator==(const AudioMsgId &other) const;
	bool operator!=(const AudioMsgId &other) const;

private:
	void setTypeFromAudio();

	DocumentData *_audio = nullptr;
	Type _type = Type::Unknown;
	FullMsgId _contextId;
	uint32 _externalPlayId = 0;
	bool _changeablePlaybackSpeed = false;

};
