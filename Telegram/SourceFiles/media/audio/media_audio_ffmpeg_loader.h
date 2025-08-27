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
#include <libavfilter/avfilter.h>
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

	bool open(crl::time positionMs, float64 speed = 1.) override;

	crl::time duration() override {
		return _duration;
	}
	void overrideDuration(int64 startedAtSample, crl::time duration) {
		_startedAtSample = startedAtSample;
		_duration = duration;
	}

	int samplesFrequency() override {
		return _samplesFrequency;
	}

#if !DA_FFMPEG_NEW_CHANNEL_LAYOUT
	static uint64_t ComputeChannelLayout(
		uint64_t channel_layout,
		int channels);
#endif // !DA_FFMPEG_NEW_CHANNEL_LAYOUT

	[[nodiscard]] int64 startedAtSample() const {
		return _startedAtSample;
	}

	~AbstractFFMpegLoader();

protected:
	static int64 Mul(int64 value, AVRational rational);

	int _samplesFrequency = Media::Player::kDefaultFrequency;
	int64 _startedAtSample = 0;
	crl::time _duration = 0;

	uchar *ioBuffer = nullptr;
	AVIOContext *ioContext = nullptr;
	AVFormatContext *fmtContext = nullptr;
#if LIBAVFORMAT_VERSION_MAJOR >= 59
	const AVCodec *codec = nullptr;
#else
	AVCodec *codec = nullptr;
#endif
	int32 streamId = 0;

	bool _opened = false;

private:
	static int ReadData(void *opaque, uint8_t *buf, int buf_size);
	static int64_t SeekData(void *opaque, int64_t offset, int whence);
	static int ReadBytes(void *opaque, uint8_t *buf, int buf_size);
	static int64_t SeekBytes(void *opaque, int64_t offset, int whence);
	static int ReadFile(void *opaque, uint8_t *buf, int buf_size);
	static int64_t SeekFile(void *opaque, int64_t offset, int whence);

};

class AbstractAudioFFMpegLoader : public AbstractFFMpegLoader {
public:
	AbstractAudioFFMpegLoader(
		const Core::FileLocation &file,
		const QByteArray &data,
		bytes::vector &&buffer);

	void dropFramesTill(int64 samples) override;
	int64 startReadingQueuedFrames(float64 newSpeed) override;

	int samplesFrequency() override {
		return _swrDstRate;
	}

	int sampleSize() override {
		return _outputSampleSize;
	}

	int format() override {
		return _outputFormat;
	}

	~AbstractAudioFFMpegLoader();

protected:
	bool initUsingContext(not_null<AVCodecContext*> context, float64 speed);
	[[nodiscard]] ReadResult readFromReadyContext(
		not_null<AVCodecContext*> context);

	// Streaming player provides the first frame to the ChildFFMpegLoader
	// so we replace our allocated frame with the one provided.
	[[nodiscard]] ReadResult replaceFrameAndRead(FFmpeg::FramePointer frame);

private:
	struct EnqueuedFrame {
		int64 position = 0;
		int64 samples = 0;
		FFmpeg::FramePointer frame;
	};
	[[nodiscard]] ReadResult readFromReadyFrame();
	[[nodiscard]] ReadResult readOrBufferForFilter(
		not_null<AVFrame*> frame,
		int64 samplesOverride);
	bool frameHasDesiredFormat(not_null<AVFrame*> frame) const;
	bool initResampleForFrame();
	bool initResampleUsingFormat();
	bool ensureResampleSpaceAvailable(int samples);

	bool changeSpeedFilter(float64 speed);
	void createSpeedFilter(float64 speed);

	void enqueueNormalFrame(
		not_null<AVFrame*> frame,
		int64 samples = 0);
	void enqueueFramesFinished();
	[[nodiscard]] auto fillFrameFromQueued()
		-> std::variant<not_null<const EnqueuedFrame*>, ReadError>;

	FFmpeg::FramePointer _frame;
	FFmpeg::FramePointer _resampledFrame;
	FFmpeg::FramePointer _filteredFrame;
	int _resampledFrameCapacity = 0;

	int64 _framesQueuedSamples = 0;
	std::deque<EnqueuedFrame> _framesQueued;
	int _framesQueuedIndex = -1;

	int _outputFormat = AL_FORMAT_STEREO16;
	int _outputChannels = 2;
	int _outputSampleSize = 2 * sizeof(uint16);

	SwrContext *_swrContext = nullptr;

	int _swrSrcRate = 0;
	AVSampleFormat _swrSrcSampleFormat = AV_SAMPLE_FMT_NONE;

	const int _swrDstRate = Media::Player::kDefaultFrequency;
	AVSampleFormat _swrDstSampleFormat = AV_SAMPLE_FMT_S16;

#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
	AVChannelLayout _swrSrcChannelLayout = AV_CHANNEL_LAYOUT_STEREO;
	AVChannelLayout _swrDstChannelLayout = AV_CHANNEL_LAYOUT_STEREO;
#else // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	uint64_t _swrSrcChannelLayout = 0;
	uint64_t _swrDstChannelLayout = AV_CH_LAYOUT_STEREO;
#endif // DA_FFMPEG_NEW_CHANNEL_LAYOUT

	AVFilterGraph *_filterGraph = nullptr;
	float64 _filterSpeed = 1.;
	AVFilterContext *_filterSrc = nullptr;
	AVFilterContext *_atempo = nullptr;
	AVFilterContext *_filterSink = nullptr;

};

class FFMpegLoader : public AbstractAudioFFMpegLoader {
public:
	FFMpegLoader(
		const Core::FileLocation &file,
		const QByteArray &data,
		bytes::vector &&buffer);

	bool open(crl::time positionMs, float64 speed = 1.) override;

	ReadResult readMore() override;

	~FFMpegLoader();

private:
	bool openCodecContext();
	bool seekTo(crl::time positionMs);

	AVCodecContext *_codecContext = nullptr;
	AVPacket _packet;
	bool _readTillEnd = false;

};

} // namespace Media
