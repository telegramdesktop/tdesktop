/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_file.h"

#include "media/streaming/media_streaming_loader.h"
#include "media/streaming/media_streaming_file_delegate.h"

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
	while (!_reader->fill(_offset, buffer, &_semaphore)) {
		_delegate->fileWaitingForData();
		_semaphore.acquire();
		if (_interrupted) {
			return -1;
		} else if (const auto error = _reader->failed()) {
			fail(*error);
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
		fail(_format ? Error::InvalidData : Error::OpenFailed);
	}
}

void File::Context::logFatal(QLatin1String method, AvErrorWrap error) {
	if (!unroll()) {
		LogError(method, error);
		fail(_format ? Error::InvalidData : Error::OpenFailed);
	}
}

Stream File::Context::initStream(
		not_null<AVFormatContext*> format,
		AVMediaType type) {
	auto result = Stream();
	const auto index = result.index = av_find_best_stream(
		format,
		type,
		-1,
		-1,
		nullptr,
		0);
	if (index < 0) {
		return result;
	}

	const auto info = format->streams[index];
	if (type == AVMEDIA_TYPE_VIDEO) {
		result.rotation = ReadRotationFromMetadata(info);
		result.aspect = ValidateAspectRatio(info->sample_aspect_ratio);
	} else if (type == AVMEDIA_TYPE_AUDIO) {
		result.frequency = info->codecpar->sample_rate;
		if (!result.frequency) {
			return result;
		}
	}

	result.codec = MakeCodecPointer(info);
	if (!result.codec) {
		return result;
	}

	result.frame = MakeFramePointer();
	if (!result.frame) {
		result.codec = nullptr;
		return result;
	}
	result.timeBase = info->time_base;
	result.duration = (info->duration != AV_NOPTS_VALUE)
		? PtsToTime(info->duration, result.timeBase)
		: PtsToTime(format->duration, kUniversalTimeBase);
	if (!result.duration) {
		result.codec = nullptr;
	} else if (result.duration == kTimeUnknown) {
		result.duration = kDurationUnavailable;
	} else {
		++result.duration;
		if (result.duration > kDurationMax) {
			result.duration = 0;
			result.codec = nullptr;
		}
	}
	return result;
}

void File::Context::seekToPosition(
		not_null<AVFormatContext*> format,
		const Stream &stream,
		crl::time position) {
	auto error = AvErrorWrap();

	if (!position) {
		return;
	} else if (stream.duration == kDurationUnavailable) {
		// Seek in files with unknown duration is not supported.
		return;
	}
	//
	// Non backward search reads the whole file if the position is after
	// the last keyframe inside the index. So we search only backward.
	//
	//const auto seekFlags = 0;
	//error = av_seek_frame(
	//	format,
	//	streamIndex,
	//	TimeToPts(position, kUniversalTimeBase),
	//	seekFlags);
	//if (!error) {
	//	return;
	//}
	//
	error = av_seek_frame(
		format,
		stream.index,
		TimeToPts(
			std::clamp(position, crl::time(0), stream.duration - 1),
			stream.timeBase),
		AVSEEK_FLAG_BACKWARD);
	if (!error) {
		return;
	}
	return logFatal(qstr("av_seek_frame"), error);
}

base::variant<Packet, AvErrorWrap> File::Context::readPacket() {
	auto error = AvErrorWrap();

	auto result = Packet();
	error = av_read_frame(_format.get(), &result.fields());
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
	auto format = MakeFormatPointer(
		static_cast<void *>(this),
		&Context::Read,
		nullptr,
		&Context::Seek);
	if (!format) {
		return fail(Error::OpenFailed);
	}

	if ((error = avformat_find_stream_info(format.get(), nullptr))) {
		return logFatal(qstr("avformat_find_stream_info"), error);
	}

	auto video = initStream(format.get(), AVMEDIA_TYPE_VIDEO);
	if (unroll()) {
		return;
	}

	auto audio = initStream(format.get(), AVMEDIA_TYPE_AUDIO);
	if (unroll()) {
		return;
	}

	_reader->headerDone();
	if (video.codec || audio.codec) {
		seekToPosition(format.get(), video.codec ? video : audio, position);
	}
	if (unroll()) {
		return;
	}

	if (!_delegate->fileReady(std::move(video), std::move(audio))) {
		return fail(Error::OpenFailed);
	}
	_format = std::move(format);
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
	if (_delegate->fileReadMore()) {
		_readTillEnd = false;
		auto error = AvErrorWrap(av_seek_frame(
			_format.get(),
			-1, // stream_index
			0, // timestamp
			AVSEEK_FLAG_BACKWARD));
		if (error) {
			logFatal(qstr("av_seek_frame"));
		}
	} else {
		_readTillEnd = true;
	}
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

void File::Context::fail(Error error) {
	_failed = true;
	_delegate->fileError(error);
}

File::Context::~Context() = default;

bool File::Context::finished() const {
	return unroll() || _readTillEnd;
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
	_reader.stop();
	_context.reset();
}

bool File::isRemoteLoader() const {
	return _reader.isRemoteLoader();
}

File::~File() {
	stop();
}

} // namespace Streaming
} // namespace Media
