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
} // extern "C"

namespace Media {
namespace Streaming {

[[nodiscard]] crl::time PtsToTime(int64_t pts, const AVRational &timeBase);

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

void LogError(QLatin1String method);
void LogError(QLatin1String method, AvErrorWrap error);

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

class CodecPointer {
public:
	CodecPointer(std::nullptr_t = nullptr);
	CodecPointer(CodecPointer &&other);
	CodecPointer &operator=(CodecPointer &&other);
	CodecPointer &operator=(std::nullptr_t);
	~CodecPointer();

	[[nodiscard]] static CodecPointer FromStream(
		not_null<AVStream*> stream);

	[[nodiscard]] AVCodecContext *get() const;
	[[nodiscard]] AVCodecContext *operator->() const;
	[[nodiscard]] operator AVCodecContext*() const;
	[[nodiscard]] AVCodecContext* release();

private:
	void destroy();

	AVCodecContext *_context = nullptr;

};

struct FrameDeleter {
	using pointer = AVFrame*;
	[[nodiscard]] static pointer create();
	void operator()(pointer value);
};
using FramePointer = std::unique_ptr<FrameDeleter::pointer, FrameDeleter>;

struct Stream {
	CodecPointer codec;
	FramePointer frame;
	std::deque<Packet> queue;
	crl::time lastReadPositionTime = 0;
	int invalidDataPackets = 0;
};

[[nodiscard]] std::optional<AvErrorWrap> ReadNextFrame(Stream &stream);

} // namespace Streaming
} // namespace Media
