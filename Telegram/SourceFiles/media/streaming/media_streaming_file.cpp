/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_file.h"

#include "media/streaming/media_streaming_loader.h"
#include "media/streaming/media_streaming_file_delegate.h"

#include "ui/toast/toast.h" // #TODO streaming

namespace Media {
namespace Streaming {

File::Context::Context(
	not_null<FileDelegate*> delegate,
	not_null<Reader*> reader)
: _delegate(delegate)
, _reader(reader)
, _size(reader->size()) {
}

int File::Context::Read(void *opaque, uint8_t *buffer, int bufferSize) {
	return static_cast<Context*>(opaque)->read(
		bytes::make_span(buffer, bufferSize));
}

int64_t File::Context::Seek(void *opaque, int64_t offset, int whence) {
	return static_cast<Context*>(opaque)->seek(offset, whence);
}

int File::Context::read(bytes::span buffer) {
	const auto amount = std::min(size_type(_size - _offset), buffer.size());
	if (unroll() || amount < 0) {
		return -1;
	} else if (!amount) {
		return amount;
	}
	buffer = buffer.subspan(0, amount);
	while (!_reader->fill(buffer, _offset, &_semaphore)) {
		_delegate->fileWaitingForData();
		_semaphore.acquire();
		if (_interrupted) {
			return -1;
		} else if (_reader->failed()) {
			fail();
			return -1;
		}
	}
	_offset += amount;
	return amount;
}

int64_t File::Context::seek(int64_t offset, int whence) {
	const auto checkedSeek = [&](int64_t offset) {
		if (_failed || offset < 0 || offset > _size) {
			return -1;
		}
		return (_offset = offset);
	};
	switch (whence) {
	case SEEK_SET: return checkedSeek(offset);
	case SEEK_CUR: return checkedSeek(_offset + offset);
	case SEEK_END: return checkedSeek(_size + offset);
	case AVSEEK_SIZE: return _size;
	}
	return -1;
}

void File::Context::logError(QLatin1String method) {
	if (!unroll()) {
		LogError(method);
	}
}

void File::Context::logError(QLatin1String method, AvErrorWrap error) {
	if (!unroll()) {
		LogError(method, error);
	}
}

void File::Context::logFatal(QLatin1String method) {
	if (!unroll()) {
		LogError(method);
		fail();
	}
}

void File::Context::logFatal(QLatin1String method, AvErrorWrap error) {
	if (!unroll()) {
		LogError(method, error);
		fail();
	}
}

Stream File::Context::initStream(AVMediaType type) {
	auto result = Stream();
	const auto index = result.index = av_find_best_stream(
		_formatContext,
		type,
		-1,
		-1,
		nullptr,
		0);
	if (index < 0) {
		return {};
	}

	const auto info = _formatContext->streams[index];
	if (type == AVMEDIA_TYPE_VIDEO) {
		result.rotation = ReadRotationFromMetadata(info);
	} else if (type == AVMEDIA_TYPE_AUDIO) {
		result.frequency = info->codecpar->sample_rate;
		if (!result.frequency) {
			return {};
		}
	}

	result.codec = MakeCodecPointer(info);
	if (!result.codec) {
		return {};
	}
	result.frame = MakeFramePointer();
	if (!result.frame) {
		return {};
	}
	result.timeBase = info->time_base;
	result.duration = (info->duration != AV_NOPTS_VALUE)
		? PtsToTimeCeil(info->duration, result.timeBase)
		: PtsToTimeCeil(_formatContext->duration, kUniversalTimeBase);
	if (result.duration == kTimeUnknown || !result.duration) {
		return {};
	}
	return result;
}

void File::Context::seekToPosition(crl::time position) {
	auto error = AvErrorWrap();

	if (!position) {
		return;
	}
	const auto streamIndex = -1;
	const auto seekFlags = 0;
	error = av_seek_frame(
		_formatContext,
		streamIndex,
		TimeToPts(position, kUniversalTimeBase),
		seekFlags);
	if (!error) {
		return;
	}
	return logFatal(qstr("av_seek_frame"), error);
}

base::variant<Packet, AvErrorWrap> File::Context::readPacket() {
	auto error = AvErrorWrap();

	auto result = Packet();
	error = av_read_frame(_formatContext, &result.fields());
	if (unroll()) {
		return AvErrorWrap();
	} else if (!error) {
		return std::move(result);
	} else if (error.code() != AVERROR_EOF) {
		logFatal(qstr("av_read_frame"), error);
	}
	return error;
}

void File::Context::start(crl::time position) {
	auto error = AvErrorWrap();

	if (unroll()) {
		return;
	}
	_ioBuffer = reinterpret_cast<uchar*>(av_malloc(AVBlockSize));
	_ioContext = avio_alloc_context(
		_ioBuffer,
		AVBlockSize,
		0,
		static_cast<void*>(this),
		&Context::Read,
		nullptr,
		&Context::Seek);
	_formatContext = avformat_alloc_context();
	if (!_formatContext) {
		return logFatal(qstr("avformat_alloc_context"));
	}
	_formatContext->pb = _ioContext;

	error = avformat_open_input(&_formatContext, nullptr, nullptr, nullptr);
	if (error) {
		_ioBuffer = nullptr;
		return logFatal(qstr("avformat_open_input"), error);
	}
	_opened = true;

	if ((error = avformat_find_stream_info(_formatContext, nullptr))) {
		return logFatal(qstr("avformat_find_stream_info"), error);
	}

	auto video = initStream(AVMEDIA_TYPE_VIDEO);
	if (unroll()) {
		return;
	}

	auto audio = initStream(AVMEDIA_TYPE_AUDIO);
	if (unroll()) {
		return;
	}

	seekToPosition(position);
	if (unroll()) {
		return;
	}

	_delegate->fileReady(std::move(video), std::move(audio));
}

void File::Context::readNextPacket() {
	auto result = readPacket();
	if (unroll()) {
		return;
	} else if (const auto packet = base::get_if<Packet>(&result)) {
		const auto more = _delegate->fileProcessPacket(std::move(*packet));
		if (!more) {
			do {
				_semaphore.acquire();
			} while (!unroll() && !_delegate->fileReadMore());
		}
	} else {
		// Still trying to read by drain.
		Assert(result.is<AvErrorWrap>());
		Assert(result.get<AvErrorWrap>().code() == AVERROR_EOF);
		handleEndOfFile();
	}
}
void File::Context::handleEndOfFile() {
	const auto more = _delegate->fileProcessPacket(Packet());
	// #TODO streaming looping
	_readTillEnd = true;
}

void File::Context::interrupt() {
	_interrupted = true;
	_semaphore.release();
}

void File::Context::wake() {
	_semaphore.release();
}

bool File::Context::interrupted() const {
	return _interrupted;
}

bool File::Context::failed() const {
	return _failed;
}

bool File::Context::unroll() const {
	return failed() || interrupted();
}

void File::Context::fail() {
	_failed = true;
	_delegate->fileError();
}

File::Context::~Context() {
	if (_opened) {
		avformat_close_input(&_formatContext);
	}
	if (_ioContext) {
		av_freep(&_ioContext->buffer);
		av_freep(&_ioContext);
	} else if (_ioBuffer) {
		av_freep(&_ioBuffer);
	}
	if (_formatContext) {
		avformat_free_context(_formatContext);
	}
}

bool File::Context::finished() const {
	return unroll() || _readTillEnd; // #TODO streaming looping
}

File::File(
	not_null<Data::Session*> owner,
	std::unique_ptr<Loader> loader)
: _reader(owner, std::move(loader)) {
}

void File::start(not_null<FileDelegate*> delegate, crl::time position) {
	stop();

	_context.emplace(delegate, &_reader);
	_thread = std::thread([=, context = &*_context] {
		context->start(position);
		while (!context->finished()) {
			context->readNextPacket();
		}

		crl::on_main(context, [] { AssertIsDebug();
			Ui::Toast::Show("Finished loading.");
		});
	});
}

void File::wake() {
	Expects(_context.has_value());

	_context->wake();
}

void File::stop() {
	if (_thread.joinable()) {
		_context->interrupt();
		_thread.join();
	}
	_context.reset();
}

File::~File() {
	stop();
}

} // namespace Streaming
} // namespace Media
