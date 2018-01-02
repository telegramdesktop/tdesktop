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

#include "media/media_audio.h"
#include "media/media_audio_loader.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
} // extern "C"

#include <AL/al.h>

class AbstractFFMpegLoader : public AudioPlayerLoader {
public:
	AbstractFFMpegLoader(
		const FileLocation &file,
		const QByteArray &data,
		base::byte_vector &&bytes)
	: AudioPlayerLoader(file, data, std::move(bytes)) {
	}

	bool open(TimeMs positionMs) override;

	int64 samplesCount() override {
		return _samplesCount;
	}

	int samplesFrequency() override {
		return _samplesFrequency;
	}

	static uint64_t ComputeChannelLayout(
		uint64_t channel_layout,
		int channels);

	~AbstractFFMpegLoader();

protected:
	static int64 Mul(int64 value, AVRational rational);

	int _samplesFrequency = Media::Player::kDefaultFrequency;
	int64 _samplesCount = 0;

	uchar *ioBuffer = nullptr;
	AVIOContext *ioContext = nullptr;
	AVFormatContext *fmtContext = nullptr;
	AVCodec *codec = nullptr;
	int32 streamId = 0;

	bool _opened = false;

private:
	static int _read_data(void *opaque, uint8_t *buf, int buf_size);
	static int64_t _seek_data(void *opaque, int64_t offset, int whence);
	static int _read_bytes(void *opaque, uint8_t *buf, int buf_size);
	static int64_t _seek_bytes(void *opaque, int64_t offset, int whence);
	static int _read_file(void *opaque, uint8_t *buf, int buf_size);
	static int64_t _seek_file(void *opaque, int64_t offset, int whence);

};

class FFMpegLoader : public AbstractFFMpegLoader {
public:
	FFMpegLoader(
		const FileLocation &file,
		const QByteArray &data,
		base::byte_vector &&bytes);

	bool open(TimeMs positionMs) override;

	int64 samplesCount() override {
		return _swrDstSamplesCount;
	}

	int samplesFrequency() override {
		return _swrDstRate;
	}

	int format() override {
		return _format;
	}

	ReadResult readMore(QByteArray &result, int64 &samplesAdded) override;

	~FFMpegLoader();

protected:
	int sampleSize = 2 * sizeof(uint16);

private:
	ReadResult readFromReadyFrame(QByteArray &result, int64 &samplesAdded);
	bool frameHasDesiredFormat() const;
	bool initResampleForFrame();
	bool initResampleUsingFormat();
	bool ensureResampleSpaceAvailable(int samples);

	AVCodecContext *_codecContext = nullptr;
	AVPacket _packet;
	int _format = AL_FORMAT_STEREO16;
	AVFrame *_frame = nullptr;

	SwrContext *_swrContext = nullptr;

	int _swrSrcRate = 0;
	AVSampleFormat _swrSrcFormat = AV_SAMPLE_FMT_NONE;
	uint64_t _swrSrcChannelLayout = 0;

	const int _swrDstRate = Media::Player::kDefaultFrequency;
	AVSampleFormat _swrDstFormat = AV_SAMPLE_FMT_S16;
	uint64_t _swrDstChannelLayout = AV_CH_LAYOUT_STEREO;
	int _swrDstChannels = 2;

	int64 _swrDstSamplesCount = 0;
	uint8_t **_swrDstData = nullptr;
	int _swrDstDataCapacity = 0;

};
