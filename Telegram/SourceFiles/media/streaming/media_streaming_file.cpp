/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_file.h"

#include "media/streaming/media_streaming_loader.h"
#include "media/streaming/media_streaming_file_delegate.h"
#include "ffmpeg/ffmpeg_utility.h"

namespace Media {
namespace Streaming {
namespace {

constexpr auto kMaxSingleReadAmount = 8 * 1024 * 1024;
constexpr auto kMaxQueuedPackets = 1024;

} // namespace

File::Context::Context(
	not_null<FileDelegate*> delegate,
	not_null<Reader*> reader)
: _delegate(delegate)
, _reader(reader)
, _size(reader->size()) {
}

File::Context::~Context() = default;

int File::Context::Read(void *opaque, uint8_t *buffer, int bufferSize) {
	return static_cast<Context*>(opaque)->read(
		bytes::make_span(buffer, bufferSize));
}

int64_t File::Context::Seek(void *opaque, int64_t offset, int whence) {
	return static_cast<Context*>(opaque)->seek(offset, whence);
}

int File::Context::read(bytes::span buffer) {
	Assert(_size >= _offset);
	const auto amount = std::min(std::size_t(_size - _offset), buffer.size());

	if (unroll()) {
		return -1;
	} else if (amount > kMaxSingleReadAmount) {
		LOG(("Streaming Error: Read callback asked for too much data: %1"
			).arg(amount));
		return -1;
	} else if (!amount) {
		return amount;
	}

	buffer = buffer.subspan(0, amount);
	while (true) {
		const auto result = _reader->fill(_offset, buffer, &_semaphore);
		if (result == Reader::FillState::Success) {
			break;
		} else if (result == Reader::FillState::WaitingRemote) {
			// Perhaps for the correct sleeping in case of enough packets
			// being read already we require SleepPolicy::Allowed here.
			// Otherwise if we wait for the remote frequently and
			// _queuedPackets never get to kMaxQueuedPackets and we don't call
			// processQueuedPackets(SleepPolicy::Allowed) ever.
			//
			// But right now we can't simply pass SleepPolicy::Allowed here,
			// it freezes because of two _semaphore.acquire one after another.
			processQueuedPackets(SleepPolicy::Disallowed);
			_delegate->fileWaitingForData();
		}
		_semaphore.acquire();
		if (_interrupted) {
			return -1;
		} else if (const auto error = _reader->streamingError()) {
			fail(*error);
			return -1;
		}
	}

	sendFullInCache();

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
		FFmpeg::LogError(method);
	}
}

void File::Context::logError(
		QLatin1String method,
		FFmpeg::AvErrorWrap error) {
	if (!unroll()) {
		FFmpeg::LogError(method, error);
	}
}

void File::Context::logFatal(QLatin1String method) {
	if (!unroll()) {
		FFmpeg::LogError(method);
		fail(_format ? Error::InvalidData : Error::OpenFailed);
	}
}

void File::Context::logFatal(
		QLatin1String method,
		FFmpeg::AvErrorWrap error) {
	if (!unroll()) {
		FFmpeg::LogError(method, error);
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
		if (info->disposition & AV_DISPOSITION_ATTACHED_PIC) {
			// ignore cover streams
			return Stream();
		}
		result.rotation = FFmpeg::ReadRotationFromMetadata(info);
		result.aspect = FFmpeg::ValidateAspectRatio(info->sample_aspect_ratio);
	} else if (type == AVMEDIA_TYPE_AUDIO) {
		result.frequency = info->codecpar->sample_rate;
		if (!result.frequency) {
			return result;
		}
	}

	result.codec = FFmpeg::MakeCodecPointer(info);
	if (!result.codec) {
		return result;
	}

	result.frame = FFmpeg::MakeFramePointer();
	if (!result.frame) {
		result.codec = nullptr;
		return result;
	}
	result.timeBase = info->time_base;
	result.duration = (info->duration != AV_NOPTS_VALUE)
		? FFmpeg::PtsToTime(info->duration, result.timeBase)
		: FFmpeg::PtsToTime(format->duration, FFmpeg::kUniversalTimeBase);
	if (result.duration == kTimeUnknown) {
		result.duration = kDurationUnavailable;
	} else if (result.duration <= 0) {
		result.codec = nullptr;
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
	auto error = FFmpeg::AvErrorWrap();

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
		FFmpeg::TimeToPts(
			std::clamp(position, crl::time(0), stream.duration - 1),
			stream.timeBase),
		AVSEEK_FLAG_BACKWARD);
	if (!error) {
		return;
	}
	return logFatal(qstr("av_seek_frame"), error);
}

std::variant<FFmpeg::Packet, FFmpeg::AvErrorWrap> File::Context::readPacket() {
	auto error = FFmpeg::AvErrorWrap();

	auto result = FFmpeg::Packet();
	error = av_read_frame(_format.get(), &result.fields());
	if (unroll()) {
		return FFmpeg::AvErrorWrap();
	} else if (!error) {
		return result;
	} else if (error.code() != AVERROR_EOF) {
		logFatal(qstr("av_read_frame"), error);
	}
	return error;
}

void File::Context::start(crl::time position) {
	auto error = FFmpeg::AvErrorWrap();

	if (unroll()) {
		return;
	}
	auto format = FFmpeg::MakeFormatPointer(
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
	if (_reader->isRemoteLoader()) {
		sendFullInCache(true);
	}
	if (video.codec || audio.codec) {
		seekToPosition(format.get(), video.codec ? video : audio, position);
	}
	if (unroll()) {
		return;
	}

	if (video.codec) {
		_queuedPackets[video.index].reserve(kMaxQueuedPackets);
	}
	if (audio.codec) {
		_queuedPackets[audio.index].reserve(kMaxQueuedPackets);
	}

	const auto header = _reader->headerSize();
	if (!_delegate->fileReady(header, std::move(video), std::move(audio))) {
		return fail(Error::OpenFailed);
	}
	_format = std::move(format);
}

void File::Context::sendFullInCache(bool force) {
	const auto started = _fullInCache.has_value();
	if (force || started) {
		const auto nowFullInCache = _reader->fullInCache();
		if (!started || *_fullInCache != nowFullInCache) {
			_fullInCache = nowFullInCache;
			_delegate->fileFullInCache(nowFullInCache);
		}
	}
}

void File::Context::readNextPacket() {
	auto result = readPacket();
	if (unroll()) {
		return;
	} else if (const auto packet = std::get_if<FFmpeg::Packet>(&result)) {
		const auto index = packet->fields().stream_index;
		const auto i = _queuedPackets.find(index);
		if (i == end(_queuedPackets)) {
			return;
		}
		i->second.push_back(std::move(*packet));
		if (i->second.size() == kMaxQueuedPackets) {
			processQueuedPackets(SleepPolicy::Allowed);
		}
		Assert(i->second.size() < kMaxQueuedPackets);
	} else {
		// Still trying to read by drain.
		Assert(v::is<FFmpeg::AvErrorWrap>(result));
		Assert(v::get<FFmpeg::AvErrorWrap>(result).code() == AVERROR_EOF);
		processQueuedPackets(SleepPolicy::Allowed);
		if (!finished()) {
			handleEndOfFile();
		}
	}
}

void File::Context::handleEndOfFile() {
	_delegate->fileProcessEndOfFile();
	if (_delegate->fileReadMore()) {
		_readTillEnd = false;
		auto error = FFmpeg::AvErrorWrap(av_seek_frame(
			_format.get(),
			-1, // stream_index
			0, // timestamp
			AVSEEK_FLAG_BACKWARD));
		if (error) {
			logFatal(qstr("av_seek_frame"));
		}

		// If we loaded a file till the end then we think it is fully cached,
		// assume we finished loading and don't want to keep all other
		// download tasks throttled because of an active streaming.
		_reader->tryRemoveLoaderAsync();
	} else {
		_readTillEnd = true;
	}
}

void File::Context::processQueuedPackets(SleepPolicy policy) {
	const auto more = _delegate->fileProcessPackets(_queuedPackets);
	if (!more && policy == SleepPolicy::Allowed) {
		do {
			_reader->startSleep(&_semaphore);
			_semaphore.acquire();
			_reader->stopSleep();
		} while (!unroll() && !_delegate->fileReadMore());
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

bool File::Context::finished() const {
	return unroll() || _readTillEnd;
}

void File::Context::stopStreamingAsync() {
	// If we finished loading we don't want to keep all other
	// download tasks throttled because of an active streaming.
	_reader->stopStreamingAsync();
}

File::File(std::shared_ptr<Reader> reader)
: _reader(std::move(reader)) {
}

void File::start(not_null<FileDelegate*> delegate, crl::time position) {
	stop(true);

	_reader->startStreaming();
	_context.emplace(delegate, _reader.get());
	_thread = std::thread([=, context = &*_context] {
		context->start(position);
		while (!context->finished()) {
			context->readNextPacket();
		}
		if (!context->interrupted()) {
			context->stopStreamingAsync();
		}
	});
}

void File::wake() {
	Expects(_context.has_value());

	_context->wake();
}

void File::stop(bool stillActive) {
	if (_thread.joinable()) {
		_context->interrupt();
		_thread.join();
	}
	_reader->stopStreaming(stillActive);
	_context.reset();
}

bool File::isRemoteLoader() const {
	return _reader->isRemoteLoader();
}

void File::setLoaderPriority(int priority) {
	_reader->setLoaderPriority(priority);
}

File::~File() {
	stop();
}

} // namespace Streaming
} // namespace Media
