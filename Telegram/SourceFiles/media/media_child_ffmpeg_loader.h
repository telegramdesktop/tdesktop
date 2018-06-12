/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/media_audio_ffmpeg_loader.h"

struct VideoSoundData {
	AVCodecContext *context = nullptr;
	int32 frequency = Media::Player::kDefaultFrequency;
	int64 length = 0;
	~VideoSoundData();
};

struct VideoSoundPart {
	AVPacket *packet = nullptr;
	AudioMsgId audio;
	uint32 playId = 0;
};

namespace FFMpeg {

// AVPacket has a deprecated field, so when you copy an AVPacket
// variable (e.g. inside QQueue), a compile warning is emited.
// We wrap full AVPacket data in a new AVPacketDataWrap struct.
// All other fields are copied from AVPacket without modifications.
struct AVPacketDataWrap {
	char __data[sizeof(AVPacket)];
};

inline void packetFromDataWrap(AVPacket &packet, const AVPacketDataWrap &data) {
	memcpy(&packet, &data, sizeof(data));
}

inline AVPacketDataWrap dataWrapFromPacket(const AVPacket &packet) {
	AVPacketDataWrap data;
	memcpy(&data, &packet, sizeof(data));
	return data;
}

inline bool isNullPacket(const AVPacket &packet) {
	return packet.data == nullptr && packet.size == 0;
}

inline bool isNullPacket(const AVPacket *packet) {
	return isNullPacket(*packet);
}

inline void freePacket(AVPacket *packet) {
	if (!isNullPacket(packet)) {
		av_packet_unref(packet);
	}
}

} // namespace FFMpeg

class ChildFFMpegLoader : public AbstractAudioFFMpegLoader {
public:
	ChildFFMpegLoader(std::unique_ptr<VideoSoundData> &&data);

	bool open(TimeMs positionMs) override;

	bool check(const FileLocation &file, const QByteArray &data) override {
		return true;
	}

	ReadResult readMore(QByteArray &result, int64 &samplesAdded) override;
	void enqueuePackets(QQueue<FFMpeg::AVPacketDataWrap> &packets) override;

	bool eofReached() const {
		return _eofReached;
	}

	~ChildFFMpegLoader();

private:
	std::unique_ptr<VideoSoundData> _parentData;
	QQueue<FFMpeg::AVPacketDataWrap> _queue;
	bool _eofReached = false;

};
