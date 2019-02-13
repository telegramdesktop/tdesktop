/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_common.h"

extern "C" {
#include <libavutil/opt.h>
} // extern "C"

namespace Media {
namespace Streaming {
namespace {

constexpr int kSkipInvalidDataPackets = 10;

} // namespace

void LogError(QLatin1String method) {
	LOG(("Streaming Error: Error in %1.").arg(method));
}

void LogError(QLatin1String method, AvErrorWrap error) {
	LOG(("Streaming Error: Error in %1 (code: %2, text: %3)."
		).arg(method
		).arg(error.code()
		).arg(error.text()));
}

crl::time PtsToTime(int64_t pts, const AVRational &timeBase) {
	return (pts == AV_NOPTS_VALUE)
		? Information::kDurationUnknown
		: ((pts * 1000LL * timeBase.num) / timeBase.den);
}

std::optional<AvErrorWrap> ReadNextFrame(Stream &stream) {
	Expects(stream.frame != nullptr);

	auto error = AvErrorWrap();

	if (stream.frame->data) {
		av_frame_unref(stream.frame.get());
	}
	do {
		error = avcodec_receive_frame(stream.codec, stream.frame.get());
		if (!error) {
			//processReadFrame(); // #TODO streaming
			return std::nullopt;
		}

		if (error.code() != AVERROR(EAGAIN) || stream.queue.empty()) {
			return error;
		}

		const auto packet = &stream.queue.front().fields();
		const auto guard = gsl::finally([
			&,
			size = packet->size,
			data = packet->data
		] {
			packet->size = size;
			packet->data = data;
			stream.queue.pop_front();
		});

		error = avcodec_send_packet(
			stream.codec,
			packet->data ? packet : nullptr); // Drain on eof.
		if (!error) {
			continue;
		}
		LogError(qstr("avcodec_send_packet"), error);
		if (error.code() == AVERROR_INVALIDDATA
			// There is a sample voice message where skipping such packet
			// results in a crash (read_access to nullptr) in swr_convert().
			&& stream.codec->codec_id != AV_CODEC_ID_OPUS) {
			if (++stream.invalidDataPackets < kSkipInvalidDataPackets) {
				continue; // Try to skip a bad packet.
			}
		}
		return error;
	} while (true);

	[[unreachable]];
}

CodecPointer::CodecPointer(std::nullptr_t) {
}

CodecPointer::CodecPointer(CodecPointer &&other)
: _context(base::take(other._context)) {
}

CodecPointer &CodecPointer::operator=(CodecPointer &&other) {
	if (this != &other) {
		destroy();
		_context = base::take(other._context);
	}
	return *this;
}

CodecPointer &CodecPointer::operator=(std::nullptr_t) {
	destroy();
	return *this;
}

void CodecPointer::destroy() {
	if (_context) {
		avcodec_free_context(&_context);
	}
}

CodecPointer CodecPointer::FromStream(not_null<AVStream*> stream) {
	auto error = AvErrorWrap();

	auto result = CodecPointer();
	const auto context = result._context = avcodec_alloc_context3(nullptr);
	if (!context) {
		LogError(qstr("avcodec_alloc_context3"));
		return {};
	}
	error = avcodec_parameters_to_context(context, stream->codecpar);
	if (error) {
		LogError(qstr("avcodec_parameters_to_context"), error);
		return {};
	}
	av_codec_set_pkt_timebase(context, stream->time_base);
	av_opt_set_int(context, "refcounted_frames", 1, 0);

	const auto codec = avcodec_find_decoder(context->codec_id);
	if (!codec) {
		LogError(qstr("avcodec_find_decoder"), context->codec_id);
		return {};
	} else if ((error = avcodec_open2(context, codec, nullptr))) {
		LogError(qstr("avcodec_open2"), error);
		return {};
	}
	return result;
}

AVCodecContext *CodecPointer::get() const {
	return _context;
}

AVCodecContext *CodecPointer::operator->() const {
	Expects(_context != nullptr);

	return get();
}

CodecPointer::operator AVCodecContext*() const {
	return get();
}

AVCodecContext* CodecPointer::release() {
	return base::take(_context);
}

CodecPointer::~CodecPointer() {
	destroy();
}

FrameDeleter::pointer FrameDeleter::create() {
	return av_frame_alloc();
}

void FrameDeleter::operator()(pointer value) {
	av_frame_free(&value);
}

} // namespace Streaming
} // namespace Media
