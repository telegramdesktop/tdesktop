/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_file.h"

#include "media/streaming/media_streaming_loader.h"

#include "media/audio/media_audio.h" // #TODO streaming
#include "media/audio/media_child_ffmpeg_loader.h"
#include "ui/toast/toast.h"

namespace Media {
namespace Streaming {

File::Context::Context(not_null<Reader*> reader)
: _reader(reader)
, _size(reader->size())
, _audioMsgId(AudioMsgId::ForVideo()) {
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
		_semaphore.acquire();
		if (_interrupted) {
			return -1;
		} else if (_reader->failed()) {
			_failed = true;
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
		_failed = true;
	}
}

void File::Context::logFatal(QLatin1String method, AvErrorWrap error) {
	if (!unroll()) {
		LogError(method, error);
		_failed = true;
	}
}

void File::Context::initStream(StreamWrap &wrap, AVMediaType type) {
	wrap.id = av_find_best_stream(
		_formatContext,
		type,
		-1,
		-1,
		nullptr,
		0);
	if (wrap.id < 0) {
		return;
	}

	wrap.info = _formatContext->streams[wrap.id];
	if (type == AVMEDIA_TYPE_VIDEO) {
		wrap.stream.rotation = ReadRotationFromMetadata(wrap.info);
	}

	wrap.stream.codec = MakeCodecPointer(wrap.info);
	if (!wrap.stream.codec) {
		ClearStream(wrap);
		return;
	}
	wrap.stream.frame = MakeFramePointer();
	if (!wrap.stream.frame) {
		ClearStream(wrap);
		return;
	}
}

void File::Context::seekToPosition(crl::time positionTime) {
	auto error = AvErrorWrap();

	if (!positionTime) {
		return;
	}
	const auto &main = mainStream();
	Assert(main.info != nullptr);
	const auto timeBase = main.info->time_base;
	const auto timeStamp = (positionTime * timeBase.den)
		/ (1000LL * timeBase.num);
	error = av_seek_frame(
		_formatContext,
		main.id,
		timeStamp,
		0);
	if (!error) {
		return;
	}
	error = av_seek_frame(
		_formatContext,
		main.id,
		timeStamp,
		AVSEEK_FLAG_BACKWARD);
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

void File::Context::start(Mode mode, crl::time positionTime) {
	auto error = AvErrorWrap();

	_mode = mode;
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

	initStream(_video, AVMEDIA_TYPE_VIDEO);
	initStream(_audio, AVMEDIA_TYPE_AUDIO);
	if (!mainStreamUnchecked().info) {
		return logFatal(qstr("RequiredStreamAbsent"));
	}

	readInformation(positionTime);

	if (_audio.info
		&& (_mode == Mode::Audio || _mode == Mode::Both)) { // #TODO streaming
		Player::mixer()->resume(_audioMsgId, true);
	}
}

auto File::Context::mainStreamUnchecked() const -> const StreamWrap & {
	return (_mode == Mode::Video || (_video.info && _mode != Mode::Audio))
		? _video
		: _audio;
}

auto File::Context::mainStream() const -> const StreamWrap & {
	const auto &result = mainStreamUnchecked();

	Ensures(result.info != nullptr);
	return result;
}

auto File::Context::mainStream() -> StreamWrap & {
	return const_cast<StreamWrap&>(((const Context*)this)->mainStream());
}

void File::Context::readInformation(crl::time positionTime) {
	const auto &main = mainStream();
	const auto info = main.info;
	auto information = Information();
	information.duration = PtsToTime(info->duration, info->time_base);

	auto result = readPacket();
	const auto packet = base::get_if<Packet>(&result);
	if (unroll()) {
		return;
	} else if (packet) {
		if (positionTime > 0) {
			const auto time = CountPacketPositionTime(
				_formatContext->streams[packet->fields().stream_index],
				*packet);
			information.started = (time == Information::kDurationUnknown)
				? positionTime
				: time;
		}
	} else {
		information.started = positionTime;
	}

	if (_audio.info
		&& (_mode == Mode::Audio || _mode == Mode::Both)) { // #TODO streaming
		auto soundData = std::make_unique<VideoSoundData>();
		soundData->context = _audio.stream.codec.release();
		soundData->frequency = _audio.info->codecpar->sample_rate;
		if (_audio.info->duration == AV_NOPTS_VALUE) {
			soundData->length = (_formatContext->duration * soundData->frequency) / AV_TIME_BASE;
		} else {
			soundData->length = (_audio.info->duration * soundData->frequency * _audio.info->time_base.num) / _audio.info->time_base.den;
		}
		Player::mixer()->play(_audioMsgId, std::move(soundData), information.started);
	}

	if (packet) {
		processPacket(std::move(*packet));
	} else {
		enqueueEofPackets();
	}

	information.cover = readFirstVideoFrame();
	if (unroll()) {
		return;
	} else if (!information.cover.isNull()) {
		information.video = information.cover.size();
		information.rotation = _video.stream.rotation;
		if (RotationSwapWidthHeight(information.rotation)) {
			information.video.transpose();
		}
	}

	information.audio = (_audio.info != nullptr);
	_information = std::move(information);
}

QImage File::Context::readFirstVideoFrame() {
	auto result = QImage();
	while (_video.info && result.isNull()) {
		auto frame = tryReadFirstVideoFrame();
		if (unroll()) {
			return QImage();
		}
		frame.match([&](QImage &image) {
			if (!image.isNull()) {
				result = std::move(image);
			} else {
				ClearStream(_video);
			}
		}, [&](const AvErrorWrap &error) {
			if (error.code() == AVERROR(EAGAIN)) {
				readNextPacket();
			} else {
				ClearStream(_video);
			}
		});
	}
	if (!_video.info && _mode == Mode::Video) {
		logFatal(qstr("RequiredStreamEmpty"));
		return QImage();
	}
	return result;
}

base::variant<QImage, AvErrorWrap> File::Context::tryReadFirstVideoFrame() {
	Expects(_video.info != nullptr);

	if (unroll()) {
		return AvErrorWrap();
	}
	const auto error = ReadNextFrame(_video.stream);
	if (error) {
		if (error->code() == AVERROR_EOF) {
			// No valid video stream.
			if (_mode == Mode::Video) {
				logFatal(qstr("RequiredStreamEmpty"));
			}
			return QImage();
		} else if (error->code() != AVERROR(EAGAIN)) {
			_failed = true;
		}
		return *error;
	}
	return ConvertFrame(_video.stream, QSize(), QImage());
}

void File::Context::enqueueEofPackets() {
	if (_audio.info) {
		Enqueue(_audio, Packet());
	}
	if (_video.info) {
		Enqueue(_video, Packet());
	}
	_readTillEnd = true;
}

void File::Context::processPacket(Packet &&packet) {
	const auto &native = packet.fields();
	const auto streamId = native.stream_index;
	const auto check = [&](StreamWrap &wrap) {
		if ((native.stream_index == wrap.id) && wrap.info) {
			// #TODO streaming queue packet to audio player
			if ((_mode == Mode::Audio || _mode == Mode::Both)
				&& (wrap.info == _audio.info)) {
				Player::mixer()->feedFromVideo({ &native, _audioMsgId });
				packet.release();
			} else {
				Enqueue(wrap, std::move(packet));
			}
			return true;
		}
		return false;
	};

	check(_audio) || check(_video);
}

void File::Context::readNextPacket() {
	auto result = readPacket();
	const auto packet = base::get_if<Packet>(&result);
	if (unroll()) {
		return;
	} else if (packet) {
		processPacket(std::move(*packet));
	} else {
		// Still trying to read by drain.
		Assert(result.is<AvErrorWrap>());
		Assert(result.get<AvErrorWrap>().code() == AVERROR_EOF);
		enqueueEofPackets();
	}
}

crl::time File::Context::CountPacketPositionTime(
		not_null<const AVStream*> info,
		const Packet &packet) {
	const auto &native = packet.fields();
	const auto packetPts = (native.pts == AV_NOPTS_VALUE)
		? native.dts
		: native.pts;
	const auto &timeBase = info->time_base;
	return PtsToTime(packetPts, info->time_base);
}

void File::Context::ClearStream(StreamWrap &wrap) {
	wrap.id = -1;
	wrap.stream = Stream();
	wrap.info = nullptr;
}

crl::time File::Context::CountPacketPositionTime(
		const StreamWrap &wrap,
		const Packet &packet) {
	return CountPacketPositionTime(wrap.info, packet);
}

void File::Context::Enqueue(StreamWrap &wrap, Packet &&packet) {
	const auto time = CountPacketPositionTime(wrap, packet);
	if (time != Information::kDurationUnknown) {
		wrap.stream.lastReadPositionTime = time;
	}

	QMutexLocker lock(&wrap.mutex);
	wrap.stream.queue.push_back(std::move(packet));
}

void File::Context::interrupt() {
	_interrupted = true;
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

File::Context::~Context() {
	ClearStream(_audio);
	ClearStream(_video);

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

bool File::Context::started() const {
	return _information.has_value();
}

bool File::Context::finished() const {
	return unroll() || _readTillEnd;
}

const Media::Streaming::Information & File::Context::information() const {
	Expects(_information.has_value());

	return *_information;
}

File::File(
	not_null<Data::Session*> owner,
	std::unique_ptr<Loader> loader)
: _reader(owner, std::move(loader)) {
}

void File::start(Mode mode, crl::time positionTime) {
	finish();

	_context = std::make_unique<Context>(&_reader);
	_thread = std::thread([=, context = _context.get()] {
		context->start(mode, positionTime);
		if (context->interrupted()) {
			return;
		} else if (context->failed()) {
			crl::on_main(context, [=] {
				// #TODO streaming failed
			});
		} else {
			crl::on_main(context, [=, info = context->information()] {
				// #TODO streaming started
			});
			while (!context->finished()) {
				context->readNextPacket();
			}
			crl::on_main(context, [] { AssertIsDebug();
				Ui::Toast::Show("Finished loading.");
			});
		}
	});
}

//rpl::producer<Information> File::information() const {
//
//}
//
//rpl::producer<Packet> File::video() const {
//
//}
//
//rpl::producer<Packet> File::audio() const {
//
//}

void File::finish() {
	if (_thread.joinable()) {
		_context->interrupt();
		_thread.join();
	}
	_context = nullptr;
}

File::~File() {
	finish();
}

} // namespace Streaming
} // namespace Media
