/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/audio/media_audio.h"
#include "media/audio/media_audio_loader.h"
#include "media/streaming/media_streaming_utility.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
} // extern "C"

#include <al.h>

namespace Core {
class FileLocation;
} // namespace Core

namespace Media {

class AbstractFFMpegLoader : public AudioPlayerLoader {
public:
	AbstractFFMpegLoader(
		const Core::FileLocation &file,
		const QByteArray &data,
		bytes::vector &&buffer)
	: AudioPlayerLoader(file, data, std::move(buffer)) {
	}

	bool open(crl::time positionMs) override;

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

class AbstractAudioFFMpegLoader : public AbstractFFMpegLoader {
public:
	AbstractAudioFFMpegLoader(
		const Core::FileLocation &file,
		const QByteArray &data,
		bytes::vector &&buffer);

	int64 samplesCount() override {
		return _outputSamplesCount;
	}

	int samplesFrequency() override {
		return _swrDstRate;
	}

	int format() override {
		return _outputFormat;
	}

	~AbstractAudioFFMpegLoader();

protected:
	bool initUsingContext(
		not_null<AVCodecContext *> context,
		int64 initialCount,
		int initialFrequency);
	ReadResult readFromReadyContext(
		not_null<AVCodecContext *> context,
		QByteArray &result,
		int64 &samplesAdded);

	// Streaming player provides the first frame to the ChildFFMpegLoader
	// so we replace our allocated frame with the one provided.
	ReadResult replaceFrameAndRead(
		FFmpeg::FramePointer frame,
		QByteArray &result,
		int64 &samplesAdded);

	int sampleSize() const {
		return _outputSampleSize;
	}

private:
	ReadResult readFromReadyFrame(QByteArray &result, int64 &samplesAdded);
	bool frameHasDesiredFormat() const;
	bool initResampleForFrame();
	bool initResampleUsingFormat();
	bool ensureResampleSpaceAvailable(int samples);

	void appendSamples(
		QByteArray &result,
		int64 &samplesAdded,
		uint8_t **data,
		int count) const;

	FFmpeg::FramePointer _frame;
	int _outputFormat = AL_FORMAT_STEREO16;
	int _outputChannels = 2;
	int _outputSampleSize = 2 * sizeof(uint16);
	int64 _outputSamplesCount = 0;

	SwrContext *_swrContext = nullptr;

	int _swrSrcRate = 0;
	AVSampleFormat _swrSrcSampleFormat = AV_SAMPLE_FMT_NONE;
	uint64_t _swrSrcChannelLayout = 0;

	const int _swrDstRate = Media::Player::kDefaultFrequency;
	AVSampleFormat _swrDstSampleFormat = AV_SAMPLE_FMT_S16;
	uint64_t _swrDstChannelLayout = AV_CH_LAYOUT_STEREO;
	uint8_t **_swrDstData = nullptr;
	int _swrDstDataCapacity = 0;

};

class FFMpegLoader : public AbstractAudioFFMpegLoader {
public:
	FFMpegLoader(
		const Core::FileLocation &file,
		const QByteArray &data,
		bytes::vector &&buffer);

	bool open(crl::time positionMs) override;

	ReadResult readMore(QByteArray &result, int64 &samplesAdded) override;

	~FFMpegLoader();

private:
	bool openCodecContext();
	bool seekTo(crl::time positionMs);

	AVCodecContext *_codecContext = nullptr;
	AVPacket _packet;

};

} // namespace Media
