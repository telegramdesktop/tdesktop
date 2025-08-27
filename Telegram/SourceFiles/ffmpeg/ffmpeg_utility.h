/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/bytes.h"
#include "base/algorithm.h"

#include <crl/crl_time.h>

#include <QSize>
#include <QString>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/version.h>
} // extern "C"

#define DA_FFMPEG_NEW_CHANNEL_LAYOUT (LIBAVUTIL_VERSION_INT >= \
	AV_VERSION_INT(57, 28, 100))

#define DA_FFMPEG_CONST_WRITE_CALLBACK (LIBAVFORMAT_VERSION_INT >= \
	AV_VERSION_INT(61, 01, 100))

#define DA_FFMPEG_HAVE_DURATION (LIBAVUTIL_VERSION_INT >= \
	AV_VERSION_INT(58, 02, 100))

class QImage;

namespace FFmpeg {

inline constexpr auto kPixelBytesSize = 4;
inline constexpr auto kAVBlockSize = 4096; // 4Kb for ffmpeg blocksize

constexpr auto kUniversalTimeBase = AVRational{ 1, AV_TIME_BASE };
constexpr auto kNormalAspect = AVRational{ 1, 1 };

class AvErrorWrap {
public:
	AvErrorWrap(int code = 0) : _code(code) {
	}

	[[nodiscard]] bool failed() const {
		return (_code < 0);
	}
	[[nodiscard]] explicit operator bool() const {
		return failed();
	}

	[[nodiscard]] int code() const {
		return _code;
	}

	[[nodiscard]] QString text() const {
		char string[AV_ERROR_MAX_STRING_SIZE] = { 0 };
		return QString::fromUtf8(av_make_error_string(
			string,
			sizeof(string),
			_code));
	}

private:
	int _code = 0;

};

class Packet {
public:
	Packet() = default;
	Packet(Packet &&other) : _data(base::take(other._data)) {
	}
	Packet &operator=(Packet &&other) {
		if (this != &other) {
			release();
			_data = base::take(other._data);
		}
		return *this;
	}
	~Packet() {
		release();
	}

	[[nodiscard]] AVPacket &fields() {
		if (!_data) {
			_data = av_packet_alloc();
		}
		return *_data;
	}
	[[nodiscard]] const AVPacket &fields() const {
		if (!_data) {
			_data = av_packet_alloc();
		}
		return *_data;
	}

	[[nodiscard]] bool empty() const {
		return !_data || !fields().data;
	}
	void release() {
		av_packet_free(&_data);
	}

private:
	mutable AVPacket *_data = nullptr;

};

struct IODeleter {
	void operator()(AVIOContext *value);
};
using IOPointer = std::unique_ptr<AVIOContext, IODeleter>;
[[nodiscard]] IOPointer MakeIOPointer(
	void *opaque,
	int(*read)(void *opaque, uint8_t *buffer, int bufferSize),
#if DA_FFMPEG_CONST_WRITE_CALLBACK
	int(*write)(void *opaque, const uint8_t *buffer, int bufferSize),
#else
	int(*write)(void *opaque, uint8_t *buffer, int bufferSize),
#endif
	int64_t(*seek)(void *opaque, int64_t offset, int whence));

struct FormatDeleter {
	void operator()(AVFormatContext *value);
};
using FormatPointer = std::unique_ptr<AVFormatContext, FormatDeleter>;
[[nodiscard]] FormatPointer MakeFormatPointer(
	void *opaque,
	int(*read)(void *opaque, uint8_t *buffer, int bufferSize),
#if DA_FFMPEG_CONST_WRITE_CALLBACK
	int(*write)(void *opaque, const uint8_t *buffer, int bufferSize),
#else
	int(*write)(void *opaque, uint8_t *buffer, int bufferSize),
#endif
	int64_t(*seek)(void *opaque, int64_t offset, int whence));
[[nodiscard]] FormatPointer MakeWriteFormatPointer(
	void *opaque,
	int(*read)(void *opaque, uint8_t *buffer, int bufferSize),
#if DA_FFMPEG_CONST_WRITE_CALLBACK
	int(*write)(void *opaque, const uint8_t *buffer, int bufferSize),
#else
	int(*write)(void *opaque, uint8_t *buffer, int bufferSize),
#endif
	int64_t(*seek)(void *opaque, int64_t offset, int whence),
	const QByteArray &format);

struct CodecDeleter {
	void operator()(AVCodecContext *value);
};
using CodecPointer = std::unique_ptr<AVCodecContext, CodecDeleter>;

struct CodecDescriptor {
	not_null<AVStream*> stream;
	bool hwAllowed = false;
};
[[nodiscard]] CodecPointer MakeCodecPointer(CodecDescriptor descriptor);

struct FrameDeleter {
	void operator()(AVFrame *value);
};
using FramePointer = std::unique_ptr<AVFrame, FrameDeleter>;
[[nodiscard]] FramePointer MakeFramePointer();
[[nodiscard]] FramePointer DuplicateFramePointer(AVFrame *frame);
[[nodiscard]] bool FrameHasData(AVFrame *frame);
void ClearFrameMemory(AVFrame *frame);

struct SwscaleDeleter {
	QSize srcSize;
	int srcFormat = int(AV_PIX_FMT_NONE);
	QSize dstSize;
	int dstFormat = int(AV_PIX_FMT_NONE);

	void operator()(SwsContext *value);
};
using SwscalePointer = std::unique_ptr<SwsContext, SwscaleDeleter>;
[[nodiscard]] SwscalePointer MakeSwscalePointer(
	QSize srcSize,
	int srcFormat,
	QSize dstSize,
	int dstFormat, // This field doesn't take part in caching!
	SwscalePointer *existing = nullptr);
[[nodiscard]] SwscalePointer MakeSwscalePointer(
	not_null<AVFrame*> frame,
	QSize resize,
	SwscalePointer *existing = nullptr);

struct SwresampleDeleter {
	AVSampleFormat srcFormat = AV_SAMPLE_FMT_NONE;
	int srcRate = 0;
	int srcChannels = 0;
	AVSampleFormat dstFormat = AV_SAMPLE_FMT_NONE;
	int dstRate = 0;
	int dstChannels = 0;

	void operator()(SwrContext *value);
};
using SwresamplePointer = std::unique_ptr<SwrContext, SwresampleDeleter>;
[[nodiscard]] SwresamplePointer MakeSwresamplePointer(
#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
	AVChannelLayout *srcLayout,
#else // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	uint64_t srcLayout,
#endif // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	AVSampleFormat srcFormat,
	int srcRate,
#if DA_FFMPEG_NEW_CHANNEL_LAYOUT
	AVChannelLayout *dstLayout,
#else // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	uint64_t dstLayout,
#endif // DA_FFMPEG_NEW_CHANNEL_LAYOUT
	AVSampleFormat dstFormat,
	int dstRate,
	SwresamplePointer *existing = nullptr);

void LogError(const QString &method, const QString &details = {});
void LogError(
	const QString &method,
	FFmpeg::AvErrorWrap error,
	const QString &details = {});

[[nodiscard]] const AVCodec *FindDecoder(not_null<AVCodecContext*> context);
[[nodiscard]] crl::time PtsToTime(int64_t pts, AVRational timeBase);
// Used for full duration conversion.
[[nodiscard]] crl::time PtsToTimeCeil(int64_t pts, AVRational timeBase);
[[nodiscard]] int64_t TimeToPts(crl::time time, AVRational timeBase);
[[nodiscard]] crl::time PacketPosition(
	const FFmpeg::Packet &packet,
	AVRational timeBase);
[[nodiscard]] crl::time PacketDuration(
	const FFmpeg::Packet &packet,
	AVRational timeBase);
[[nodiscard]] int DurationByPacket(
	const FFmpeg::Packet &packet,
	AVRational timeBase);
[[nodiscard]] int ReadRotationFromMetadata(not_null<AVStream*> stream);
[[nodiscard]] AVRational ValidateAspectRatio(AVRational aspect);
[[nodiscard]] bool RotationSwapWidthHeight(int rotation);
[[nodiscard]] QSize TransposeSizeByRotation(QSize size, int rotation);
[[nodiscard]] QSize CorrectByAspect(QSize size, AVRational aspect);

[[nodiscard]] bool GoodStorageForFrame(const QImage &storage, QSize size);
[[nodiscard]] QImage CreateFrameStorage(QSize size);

void UnPremultiply(QImage &to, const QImage &from);
void PremultiplyInplace(QImage &image);

} // namespace FFmpeg
