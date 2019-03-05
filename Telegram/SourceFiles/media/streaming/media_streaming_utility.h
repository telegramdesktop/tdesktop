/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/streaming/media_streaming_common.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
} // extern "C"

namespace Media {
namespace Streaming {

constexpr auto kUniversalTimeBase = AVRational{ 1, AV_TIME_BASE };
constexpr auto kNormalAspect = AVRational{ 1, 1 };

struct TimePoint {
	crl::time trackTime = kTimeUnknown;
	crl::time worldTime = kTimeUnknown;

	bool valid() const {
		return (trackTime != kTimeUnknown) && (worldTime != kTimeUnknown);
	}
	explicit operator bool() const {
		return valid();
	}
};

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
	Packet() {
		setEmpty();
	}
	Packet(const AVPacket &data) {
		bytes::copy(_data, bytes::object_as_span(&data));
	}
	Packet(Packet &&other) {
		bytes::copy(_data, other._data);
		if (!other.empty()) {
			other.release();
		}
	}
	Packet &operator=(Packet &&other) {
		if (this != &other) {
			av_packet_unref(&fields());
			bytes::copy(_data, other._data);
			if (!other.empty()) {
				other.release();
			}
		}
		return *this;
	}
	~Packet() {
		av_packet_unref(&fields());
	}

	[[nodiscard]] AVPacket &fields() {
		return *reinterpret_cast<AVPacket*>(_data);
	}
	[[nodiscard]] const AVPacket &fields() const {
		return *reinterpret_cast<const AVPacket*>(_data);
	}

	[[nodiscard]] bool empty() const {
		return !fields().data;
	}
	void release() {
		setEmpty();
	}

private:
	void setEmpty() {
		auto &native = fields();
		av_init_packet(&native);
		native.data = nullptr;
		native.size = 0;
	}

	alignas(alignof(AVPacket)) bytes::type _data[sizeof(AVPacket)];

};

struct IODeleter {
	void operator()(AVIOContext *value);
};
using IOPointer = std::unique_ptr<AVIOContext, IODeleter>;
[[nodiscard]] IOPointer MakeIOPointer(
	void *opaque,
	int(*read)(void *opaque, uint8_t *buffer, int bufferSize),
	int(*write)(void *opaque, uint8_t *buffer, int bufferSize),
	int64_t(*seek)(void *opaque, int64_t offset, int whence));

struct FormatDeleter {
	void operator()(AVFormatContext *value);
};
using FormatPointer = std::unique_ptr<AVFormatContext, FormatDeleter>;
[[nodiscard]] FormatPointer MakeFormatPointer(
	void *opaque,
	int(*read)(void *opaque, uint8_t *buffer, int bufferSize),
	int(*write)(void *opaque, uint8_t *buffer, int bufferSize),
	int64_t(*seek)(void *opaque, int64_t offset, int whence));

struct CodecDeleter {
	void operator()(AVCodecContext *value);
};
using CodecPointer = std::unique_ptr<AVCodecContext, CodecDeleter>;
[[nodiscard]] CodecPointer MakeCodecPointer(not_null<AVStream*> stream);

struct FrameDeleter {
	void operator()(AVFrame *value);
};
using FramePointer = std::unique_ptr<AVFrame, FrameDeleter>;
[[nodiscard]] FramePointer MakeFramePointer();
[[nodiscard]] bool FrameHasData(AVFrame *frame);
void ClearFrameMemory(AVFrame *frame);

struct SwscaleDeleter {
	QSize resize;
	QSize frameSize;
	int frameFormat = int(AV_PIX_FMT_NONE);

	void operator()(SwsContext *value);
};
using SwscalePointer = std::unique_ptr<SwsContext, SwscaleDeleter>;
[[nodiscard]] SwscalePointer MakeSwscalePointer(
	not_null<AVFrame*> frame,
	QSize resize,
	SwscalePointer *existing = nullptr);

struct Stream {
	int index = -1;
	crl::time duration = kTimeUnknown;
	AVRational timeBase = kUniversalTimeBase;
	CodecPointer codec;
	FramePointer frame;
	std::deque<Packet> queue;
	int invalidDataPackets = 0;

	// Audio only.
	int frequency = 0;

	// Video only.
	int rotation = 0;
	AVRational aspect = kNormalAspect;
	SwscalePointer swscale;
};

void LogError(QLatin1String method);
void LogError(QLatin1String method, AvErrorWrap error);

[[nodiscard]] crl::time PtsToTime(int64_t pts, AVRational timeBase);
// Used for full duration conversion.
[[nodiscard]] crl::time PtsToTimeCeil(int64_t pts, AVRational timeBase);
[[nodiscard]] int64_t TimeToPts(crl::time time, AVRational timeBase);
[[nodiscard]] crl::time PacketPosition(
	const Packet &packet,
	AVRational timeBase);
[[nodiscard]] crl::time FramePosition(const Stream &stream);
[[nodiscard]] int ReadRotationFromMetadata(not_null<AVStream*> stream);
[[nodiscard]] AVRational ValidateAspectRatio(AVRational aspect);
[[nodiscard]] bool RotationSwapWidthHeight(int rotation);
[[nodiscard]] QSize CorrectByAspect(QSize size, AVRational aspect);
[[nodiscard]] AvErrorWrap ProcessPacket(Stream &stream, Packet &&packet);
[[nodiscard]] AvErrorWrap ReadNextFrame(Stream &stream);

[[nodiscard]] bool GoodForRequest(
	const QImage &image,
	const FrameRequest &request);
[[nodiscard]] bool GoodStorageForFrame(const QImage &storage, QSize size);
[[nodiscard]] QImage CreateFrameStorage(QSize size);
[[nodiscard]] QImage ConvertFrame(
	Stream &stream,
	AVFrame *frame,
	QSize resize,
	QImage storage);
[[nodiscard]] QImage PrepareByRequest(
	const QImage &original,
	const FrameRequest &request,
	QImage storage);

} // namespace Streaming
} // namespace Media
