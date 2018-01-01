/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "media/media_audio_loader.h"
#include "media/media_audio.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
} // extern "C"

#include <AL/al.h>

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

class ChildFFMpegLoader : public AudioPlayerLoader {
public:
	ChildFFMpegLoader(std::unique_ptr<VideoSoundData> &&data);

	bool open(TimeMs positionMs) override;

	bool check(const FileLocation &file, const QByteArray &data) override {
		return true;
	}

	int32 format() override {
		return _format;
	}

	int64 samplesCount() override {
		return _parentData->length;
	}

	int32 samplesFrequency() override {
		return _parentData->frequency;
	}

	ReadResult readMore(QByteArray &result, int64 &samplesAdded) override;
	void enqueuePackets(QQueue<FFMpeg::AVPacketDataWrap> &packets) override;

	bool eofReached() const {
		return _eofReached;
	}

	~ChildFFMpegLoader();

private:
	ReadResult readFromReadyFrame(QByteArray &result, int64 &samplesAdded);

	bool _eofReached = false;

	int32 _sampleSize = 2 * sizeof(uint16);
	int32 _format = AL_FORMAT_STEREO16;
	int32 _srcRate = Media::Player::kDefaultFrequency;
	int32 _dstRate = Media::Player::kDefaultFrequency;
	int32 _maxResampleSamples = 1024;
	uint8_t **_dstSamplesData = nullptr;

	std::unique_ptr<VideoSoundData> _parentData;
	AVSampleFormat _inputFormat;
	AVFrame *_frame = nullptr;

	SwrContext *_swrContext = nullptr;
	QQueue<FFMpeg::AVPacketDataWrap> _queue;

};
