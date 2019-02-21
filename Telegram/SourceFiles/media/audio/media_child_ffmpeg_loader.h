/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/audio/media_audio_ffmpeg_loader.h"

struct VideoSoundData {
	AVCodecContext *context = nullptr;
	AVFrame *frame = nullptr;
	int32 frequency = Media::Player::kDefaultFrequency;
	int64 length = 0;
	float64 speed = 1.; // 0.5 <= speed <= 2.
	~VideoSoundData();
};

struct VideoSoundPart {
	const AVPacket *packet = nullptr;
	AudioMsgId audio;
};

namespace FFMpeg {

// AVPacket has a deprecated field, so when you copy an AVPacket
// variable (e.g. inside QQueue), a compile warning is emitted.
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

	bool open(crl::time positionMs) override;

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
	// Streaming player reads first frame by itself and provides it together
	// with the codec context. So we first read data from this frame and
	// only after that we try to read next packets.
	ReadResult readFromInitialFrame(
		QByteArray &result,
		int64 &samplesAdded);

	std::unique_ptr<VideoSoundData> _parentData;
	QQueue<FFMpeg::AVPacketDataWrap> _queue;
	bool _eofReached = false;

};
