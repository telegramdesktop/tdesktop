/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
} // extern "C"

namespace Media {
namespace Streaming {

enum class Mode {
	Both,
	Audio,
	Video,
	Inspection
};

struct Information {
	static constexpr auto kDurationUnknown = crl::time(-1);

	QSize video;
	bool audio = false;
	crl::time duration = kDurationUnknown;

	crl::time started = 0;
	QImage cover;
	int rotation = 0;
};

class AvErrorWrap {
public:
	AvErrorWrap(int code = 0) : _code(code) {
	}

	[[nodiscard]] explicit operator bool() const {
		return (_code < 0);
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

struct CodecDeleter {
	void operator()(AVCodecContext *value);
};
using CodecPointer = std::unique_ptr<AVCodecContext, CodecDeleter>;
CodecPointer MakeCodecPointer(not_null<AVStream*> stream);

struct FrameDeleter {
	void operator()(AVFrame *value);
};
using FramePointer = std::unique_ptr<AVFrame, FrameDeleter>;
FramePointer MakeFramePointer();

struct SwsContextDeleter {
	void operator()(SwsContext *value);
};
using SwsContextPointer = std::unique_ptr<SwsContext, SwsContextDeleter>;
SwsContextPointer MakeSwsContextPointer(
	not_null<AVFrame*> frame,
	QSize resize,
	SwsContextPointer *existing = nullptr);

struct Stream {
	CodecPointer codec;
	FramePointer frame;
	std::deque<Packet> queue;
	crl::time lastReadPositionTime = 0;
	int invalidDataPackets = 0;

	// Video only.
	int rotation = 0;
	SwsContextPointer swsContext;
};

void LogError(QLatin1String method);
void LogError(QLatin1String method, AvErrorWrap error);

[[nodiscard]] crl::time PtsToTime(int64_t pts, const AVRational &timeBase);
[[nodiscard]] int ReadRotationFromMetadata(not_null<AVStream*> stream);
[[nodiscard]] bool RotationSwapWidthHeight(int rotation);
[[nodiscard]] std::optional<AvErrorWrap> ReadNextFrame(Stream &stream);
[[nodiscard]] QImage ConvertFrame(
	Stream& stream,
	QSize resize,
	QImage storage);

} // namespace Streaming
} // namespace Media
