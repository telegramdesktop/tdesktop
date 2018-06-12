/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/media_clip_ffmpeg.h"

#include "media/media_audio.h"
#include "media/media_child_ffmpeg_loader.h"
#include "storage/file_download.h"

namespace Media {
namespace Clip {
namespace internal {
namespace {

constexpr int kSkipInvalidDataPackets = 10;
constexpr int kAlignImageBy = 16;

void alignedImageBufferCleanupHandler(void *data) {
	auto buffer = static_cast<uchar*>(data);
	delete[] buffer;
}

// Create a QImage of desired size where all the data is aligned to 16 bytes.
QImage createAlignedImage(QSize size) {
	auto width = size.width();
	auto height = size.height();
	auto widthalign = kAlignImageBy / 4;
	auto neededwidth = width + ((width % widthalign) ? (widthalign - (width % widthalign)) : 0);
	auto bytesperline = neededwidth * 4;
	auto buffer = new uchar[bytesperline * height + kAlignImageBy];
	auto cleanupdata = static_cast<void*>(buffer);
	auto bufferval = reinterpret_cast<uintptr_t>(buffer);
	auto alignedbuffer = buffer + ((bufferval % kAlignImageBy) ? (kAlignImageBy - (bufferval % kAlignImageBy)) : 0);
	return QImage(alignedbuffer, width, height, bytesperline, QImage::Format_ARGB32, alignedImageBufferCleanupHandler, cleanupdata);
}

bool isAlignedImage(const QImage &image) {
	return !(reinterpret_cast<uintptr_t>(image.constBits()) % kAlignImageBy) && !(image.bytesPerLine() % kAlignImageBy);
}

} // namespace

FFMpegReaderImplementation::FFMpegReaderImplementation(FileLocation *location, QByteArray *data, const AudioMsgId &audio) : ReaderImplementation(location, data)
, _audioMsgId(audio) {
	_frame = av_frame_alloc();
	av_init_packet(&_packetNull);
	_packetNull.data = nullptr;
	_packetNull.size = 0;
}

ReaderImplementation::ReadResult FFMpegReaderImplementation::readNextFrame() {
	if (_frameRead) {
		av_frame_unref(_frame);
		_frameRead = false;
	}

	do {
		int res = avcodec_receive_frame(_codecContext, _frame);
		if (res >= 0) {
			processReadFrame();
			return ReadResult::Success;
		}

		if (res == AVERROR_EOF) {
			clearPacketQueue();
			if (_mode == Mode::Normal) {
				return ReadResult::EndOfFile;
			}
			if (!_hadFrame) {
				LOG(("Gif Error: Got EOF before a single frame was read!"));
				return ReadResult::Error;
			}

			if ((res = avformat_seek_file(_fmtContext, _streamId, std::numeric_limits<int64_t>::min(), 0, std::numeric_limits<int64_t>::max(), 0)) < 0) {
				if ((res = av_seek_frame(_fmtContext, _streamId, 0, AVSEEK_FLAG_BYTE)) < 0) {
					if ((res = av_seek_frame(_fmtContext, _streamId, 0, AVSEEK_FLAG_FRAME)) < 0) {
						if ((res = av_seek_frame(_fmtContext, _streamId, 0, 0)) < 0) {
							char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
							LOG(("Gif Error: Unable to av_seek_frame() to the start %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
							return ReadResult::Error;
						}
					}
				}
			}
			avcodec_flush_buffers(_codecContext);
			_hadFrame = false;
			_frameMs = 0;
			_lastReadVideoMs = _lastReadAudioMs = 0;
			_skippedInvalidDataPackets = 0;

			continue;
		} else if (res != AVERROR(EAGAIN)) {
			char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
			LOG(("Gif Error: Unable to avcodec_receive_frame() %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			return ReadResult::Error;
		}

		while (_packetQueue.isEmpty()) {
			auto packetResult = readAndProcessPacket();
			if (packetResult == PacketResult::Error) {
				return ReadResult::Error;
			} else if (packetResult == PacketResult::EndOfFile) {
				break;
			}
		}
		if (_packetQueue.isEmpty()) {
			avcodec_send_packet(_codecContext, nullptr); // drain
			continue;
		}

		startPacket();

		AVPacket packet;
		FFMpeg::packetFromDataWrap(packet, _packetQueue.head());
		res = avcodec_send_packet(_codecContext, &packet);
		if (res < 0) {
			finishPacket();

			char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
			LOG(("Gif Error: Unable to avcodec_send_packet() %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			if (res == AVERROR_INVALIDDATA) {
				if (++_skippedInvalidDataPackets < kSkipInvalidDataPackets) {
					continue; // try to skip bad packet
				}
			}
			return ReadResult::Error;
		}
		finishPacket();
	} while (true);

	return ReadResult::Error;
}

void FFMpegReaderImplementation::processReadFrame() {
	int64 duration = _frame->pkt_duration;
	int64 framePts = _frame->pts;
	TimeMs frameMs = (framePts * 1000LL * _fmtContext->streams[_streamId]->time_base.num) / _fmtContext->streams[_streamId]->time_base.den;
	_currentFrameDelay = _nextFrameDelay;
	if (_frameMs + _currentFrameDelay < frameMs) {
		_currentFrameDelay = int32(frameMs - _frameMs);
	} else if (frameMs < _frameMs + _currentFrameDelay) {
		frameMs = _frameMs + _currentFrameDelay;
	}

	if (duration == AV_NOPTS_VALUE) {
		_nextFrameDelay = 0;
	} else {
		_nextFrameDelay = (duration * 1000LL * _fmtContext->streams[_streamId]->time_base.num) / _fmtContext->streams[_streamId]->time_base.den;
	}
	_frameMs = frameMs;

	_hadFrame = _frameRead = true;
	_frameTime += _currentFrameDelay;
}

ReaderImplementation::ReadResult FFMpegReaderImplementation::readFramesTill(TimeMs frameMs, TimeMs systemMs) {
	if (_audioStreamId < 0) { // just keep up
		if (_frameRead && _frameTime > frameMs) {
			return ReadResult::Success;
		}
		auto readResult = readNextFrame();
		if (readResult != ReadResult::Success || _frameTime > frameMs) {
			return readResult;
		}
		readResult = readNextFrame();
		if (_frameTime <= frameMs) {
			_frameTime = frameMs + 5; // keep up
		}
		return readResult;
	}

	// sync by audio stream
	auto correctMs = (frameMs >= 0) ? Player::mixer()->getVideoCorrectedTime(_audioMsgId, frameMs, systemMs) : frameMs;
	if (!_frameRead) {
		auto readResult = readNextFrame();
		if (readResult != ReadResult::Success) {
			return readResult;
		}
	}
	while (_frameTime <= correctMs) {
		auto readResult = readNextFrame();
		if (readResult != ReadResult::Success) {
			return readResult;
		}
	}
	if (frameMs >= 0) {
		_frameTimeCorrection = frameMs - correctMs;
	}
	return ReadResult::Success;
}

TimeMs FFMpegReaderImplementation::frameRealTime() const {
	return _frameMs;
}

TimeMs FFMpegReaderImplementation::framePresentationTime() const {
	return qMax(_frameTime + _frameTimeCorrection, 0LL);
}

TimeMs FFMpegReaderImplementation::durationMs() const {
	if (_fmtContext->streams[_streamId]->duration == AV_NOPTS_VALUE) return 0;
	return (_fmtContext->streams[_streamId]->duration * 1000LL * _fmtContext->streams[_streamId]->time_base.num) / _fmtContext->streams[_streamId]->time_base.den;
}

bool FFMpegReaderImplementation::renderFrame(QImage &to, bool &hasAlpha, const QSize &size) {
	Expects(_frameRead);
	_frameRead = false;

	if (!_width || !_height) {
		_width = _frame->width;
		_height = _frame->height;
		if (!_width || !_height) {
			LOG(("Gif Error: Bad frame size %1").arg(logData()));
			return false;
		}
	}
	QSize toSize(size.isEmpty() ? QSize(_width, _height) : size);
	if (!size.isEmpty() && rotationSwapWidthHeight()) {
		toSize.transpose();
	}
	if (to.isNull() || to.size() != toSize || !to.isDetached() || !isAlignedImage(to)) {
		to = createAlignedImage(toSize);
	}
	hasAlpha = (_frame->format == AV_PIX_FMT_BGRA || (_frame->format == -1 && _codecContext->pix_fmt == AV_PIX_FMT_BGRA));
	if (_frame->width == toSize.width() && _frame->height == toSize.height() && hasAlpha) {
		int32 sbpl = _frame->linesize[0], dbpl = to.bytesPerLine(), bpl = qMin(sbpl, dbpl);
		uchar *s = _frame->data[0], *d = to.bits();
		for (int32 i = 0, l = _frame->height; i < l; ++i) {
			memcpy(d + i * dbpl, s + i * sbpl, bpl);
		}
	} else {
		if ((_swsSize != toSize) || (_frame->format != -1 && _frame->format != _codecContext->pix_fmt) || !_swsContext) {
			_swsSize = toSize;
			_swsContext = sws_getCachedContext(_swsContext, _frame->width, _frame->height, AVPixelFormat(_frame->format), toSize.width(), toSize.height(), AV_PIX_FMT_BGRA, 0, 0, 0, 0);
		}
		// AV_NUM_DATA_POINTERS defined in AVFrame struct
		uint8_t *toData[AV_NUM_DATA_POINTERS] = { to.bits(), nullptr };
		int toLinesize[AV_NUM_DATA_POINTERS] = { to.bytesPerLine(), 0 };
		int res;
		if ((res = sws_scale(_swsContext, _frame->data, _frame->linesize, 0, _frame->height, toData, toLinesize)) != _swsSize.height()) {
			LOG(("Gif Error: Unable to sws_scale to good size %1, height %2, should be %3").arg(logData()).arg(res).arg(_swsSize.height()));
			return false;
		}
	}
	if (_rotation != Rotation::None) {
		QTransform rotationTransform;
		switch (_rotation) {
		case Rotation::Degrees90: rotationTransform.rotate(90); break;
		case Rotation::Degrees180: rotationTransform.rotate(180); break;
		case Rotation::Degrees270: rotationTransform.rotate(270); break;
		}
		to = to.transformed(rotationTransform);
	}

	// Read some future packets for audio stream.
	if (_audioStreamId >= 0) {
		while (_frameMs + 5000 > _lastReadAudioMs
			&& _frameMs + 15000 > _lastReadVideoMs) {
			auto packetResult = readAndProcessPacket();
			if (packetResult != PacketResult::Ok) {
				break;
			}
		}
	}

	av_frame_unref(_frame);
	return true;
}

FFMpegReaderImplementation::Rotation FFMpegReaderImplementation::rotationFromDegrees(int degrees) const {
	switch (degrees) {
	case 90: return Rotation::Degrees90;
	case 180: return Rotation::Degrees180;
	case 270: return Rotation::Degrees270;
	}
	return Rotation::None;
}

bool FFMpegReaderImplementation::start(Mode mode, TimeMs &positionMs) {
	_mode = mode;

	initDevice();
	if (!_device->open(QIODevice::ReadOnly)) {
		LOG(("Gif Error: Unable to open device %1").arg(logData()));
		return false;
	}
	_ioBuffer = (uchar*)av_malloc(AVBlockSize);
	_ioContext = avio_alloc_context(_ioBuffer, AVBlockSize, 0, static_cast<void*>(this), &FFMpegReaderImplementation::_read, 0, &FFMpegReaderImplementation::_seek);
	_fmtContext = avformat_alloc_context();
	if (!_fmtContext) {
		LOG(("Gif Error: Unable to avformat_alloc_context %1").arg(logData()));
		return false;
	}
	_fmtContext->pb = _ioContext;

	int res = 0;
	char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
	if ((res = avformat_open_input(&_fmtContext, 0, 0, 0)) < 0) {
		_ioBuffer = 0;

		LOG(("Gif Error: Unable to avformat_open_input %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		return false;
	}
	_opened = true;

	if ((res = avformat_find_stream_info(_fmtContext, 0)) < 0) {
		LOG(("Gif Error: Unable to avformat_find_stream_info %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		return false;
	}

	_streamId = av_find_best_stream(_fmtContext, AVMEDIA_TYPE_VIDEO, -1, -1, 0, 0);
	if (_streamId < 0) {
		LOG(("Gif Error: Unable to av_find_best_stream %1, error %2, %3").arg(logData()).arg(_streamId).arg(av_make_error_string(err, sizeof(err), _streamId)));
		return false;
	}
	_packetNull.stream_index = _streamId;

	auto rotateTag = av_dict_get(_fmtContext->streams[_streamId]->metadata, "rotate", NULL, 0);
	if (rotateTag && *rotateTag->value) {
		auto stringRotateTag = QString::fromUtf8(rotateTag->value);
		auto toIntSucceeded = false;
		auto rotateDegrees = stringRotateTag.toInt(&toIntSucceeded);
		if (toIntSucceeded) {
			_rotation = rotationFromDegrees(rotateDegrees);
		}
	}

	_codecContext = avcodec_alloc_context3(nullptr);
	if (!_codecContext) {
		LOG(("Gif Error: Unable to avcodec_alloc_context3 %1").arg(logData()));
		return false;
	}
	if ((res = avcodec_parameters_to_context(_codecContext, _fmtContext->streams[_streamId]->codecpar)) < 0) {
		LOG(("Gif Error: Unable to avcodec_parameters_to_context %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		return false;
	}
	av_codec_set_pkt_timebase(_codecContext, _fmtContext->streams[_streamId]->time_base);
	av_opt_set_int(_codecContext, "refcounted_frames", 1, 0);

	_codec = avcodec_find_decoder(_codecContext->codec_id);

	_audioStreamId = av_find_best_stream(_fmtContext, AVMEDIA_TYPE_AUDIO, -1, -1, 0, 0);
	if (_mode == Mode::Inspecting) {
		_hasAudioStream = (_audioStreamId >= 0);
		_audioStreamId = -1;
	} else if (_mode == Mode::Silent || !_audioMsgId.playId()) {
		_audioStreamId = -1;
	}

	if ((res = avcodec_open2(_codecContext, _codec, 0)) < 0) {
		LOG(("Gif Error: Unable to avcodec_open2 %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		return false;
	}

	std::unique_ptr<VideoSoundData> soundData;
	if (_audioStreamId >= 0) {
		auto audioContext = avcodec_alloc_context3(nullptr);
		if (!audioContext) {
			LOG(("Audio Error: Unable to avcodec_alloc_context3 %1").arg(logData()));
			return false;
		}
		if ((res = avcodec_parameters_to_context(audioContext, _fmtContext->streams[_audioStreamId]->codecpar)) < 0) {
			LOG(("Audio Error: Unable to avcodec_parameters_to_context %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			return false;
		}
		av_codec_set_pkt_timebase(audioContext, _fmtContext->streams[_audioStreamId]->time_base);
		av_opt_set_int(audioContext, "refcounted_frames", 1, 0);

		auto audioCodec = avcodec_find_decoder(audioContext->codec_id);
		if ((res = avcodec_open2(audioContext, audioCodec, 0)) < 0) {
			avcodec_free_context(&audioContext);
			LOG(("Gif Error: Unable to avcodec_open2 %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			_audioStreamId = -1;
		} else {
			soundData = std::make_unique<VideoSoundData>();
			soundData->context = audioContext;
			soundData->frequency = _fmtContext->streams[_audioStreamId]->codecpar->sample_rate;
			if (_fmtContext->streams[_audioStreamId]->duration == AV_NOPTS_VALUE) {
				soundData->length = (_fmtContext->duration * soundData->frequency) / AV_TIME_BASE;
			} else {
				soundData->length = (_fmtContext->streams[_audioStreamId]->duration * soundData->frequency * _fmtContext->streams[_audioStreamId]->time_base.num) / _fmtContext->streams[_audioStreamId]->time_base.den;
			}
		}
	}
	if (positionMs > 0) {
		const auto timeBase = _fmtContext->streams[_streamId]->time_base;
		const auto timeStamp = (positionMs * timeBase.den)
			/ (1000LL * timeBase.num);
		if (av_seek_frame(_fmtContext, _streamId, timeStamp, 0) < 0) {
			if (av_seek_frame(_fmtContext, _streamId, timeStamp, AVSEEK_FLAG_BACKWARD) < 0) {
				return false;
			}
		}
	}

	AVPacket packet;
	auto readResult = readPacket(&packet);
	if (readResult == PacketResult::Ok && positionMs > 0) {
		positionMs = countPacketMs(&packet);
	}

	if (hasAudio()) {
		Player::mixer()->play(_audioMsgId, std::move(soundData), positionMs);
	}

	if (readResult == PacketResult::Ok) {
		processPacket(&packet);
	}

	return true;
}

bool FFMpegReaderImplementation::inspectAt(TimeMs &positionMs) {
	if (positionMs > 0) {
		const auto timeBase = _fmtContext->streams[_streamId]->time_base;
		const auto timeStamp = (positionMs * timeBase.den)
			/ (1000LL * timeBase.num);
		if (av_seek_frame(_fmtContext, _streamId, timeStamp, 0) < 0) {
			if (av_seek_frame(_fmtContext, _streamId, timeStamp, AVSEEK_FLAG_BACKWARD) < 0) {
				return false;
			}
		}
	}

	_packetQueue.clear();

	AVPacket packet;
	auto readResult = readPacket(&packet);
	if (readResult == PacketResult::Ok && positionMs > 0) {
		positionMs = countPacketMs(&packet);
	}

	if (readResult == PacketResult::Ok) {
		processPacket(&packet);
	}

	return true;
}

bool FFMpegReaderImplementation::isGifv() const {
	if (_hasAudioStream) {
		return false;
	}
	if (dataSize() > Storage::kMaxAnimationInMemory) {
		return false;
	}
	if (_codecContext->codec_id != AV_CODEC_ID_H264) {
		return false;
	}
	return true;
}

QString FFMpegReaderImplementation::logData() const {
	return qsl("for file '%1', data size '%2'").arg(_location ? _location->name() : QString()).arg(_data->size());
}

FFMpegReaderImplementation::~FFMpegReaderImplementation() {
	clearPacketQueue();

	if (_frameRead) {
		av_frame_unref(_frame);
		_frameRead = false;
	}
	if (_codecContext) avcodec_free_context(&_codecContext);
	if (_swsContext) sws_freeContext(_swsContext);
	if (_opened) {
		avformat_close_input(&_fmtContext);
	}
	if (_ioContext) {
		av_freep(&_ioContext->buffer);
		av_freep(&_ioContext);
	} else if (_ioBuffer) {
		av_freep(&_ioBuffer);
	}
	if (_fmtContext) avformat_free_context(_fmtContext);
	av_frame_free(&_frame);
}

FFMpegReaderImplementation::PacketResult FFMpegReaderImplementation::readPacket(AVPacket *packet) {
	av_init_packet(packet);
	packet->data = nullptr;
	packet->size = 0;

	int res = 0;
	if ((res = av_read_frame(_fmtContext, packet)) < 0) {
		if (res == AVERROR_EOF) {
			if (_audioStreamId >= 0) {
				// queue terminating packet to audio player
				VideoSoundPart part;
				part.packet = &_packetNull;
				part.audio = _audioMsgId;
				Player::mixer()->feedFromVideo(std::move(part));
			}
			return PacketResult::EndOfFile;
		}
		char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
		LOG(("Gif Error: Unable to av_read_frame() %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		return PacketResult::Error;
	}
	return PacketResult::Ok;
}

void FFMpegReaderImplementation::processPacket(AVPacket *packet) {
	auto videoPacket = (packet->stream_index == _streamId);
	auto audioPacket = (_audioStreamId >= 0 && packet->stream_index == _audioStreamId);
	if (audioPacket || videoPacket) {
		if (videoPacket) {
			_lastReadVideoMs = countPacketMs(packet);

			_packetQueue.enqueue(FFMpeg::dataWrapFromPacket(*packet));
		} else if (audioPacket) {
			_lastReadAudioMs = countPacketMs(packet);

			// queue packet to audio player
			VideoSoundPart part;
			part.packet = packet;
			part.audio = _audioMsgId;
			Player::mixer()->feedFromVideo(std::move(part));
		}
	} else {
		av_packet_unref(packet);
	}
}

TimeMs FFMpegReaderImplementation::countPacketMs(AVPacket *packet) const {
	int64 packetPts = (packet->pts == AV_NOPTS_VALUE) ? packet->dts : packet->pts;
	TimeMs packetMs = (packetPts * 1000LL * _fmtContext->streams[packet->stream_index]->time_base.num) / _fmtContext->streams[packet->stream_index]->time_base.den;
	return packetMs;
}

FFMpegReaderImplementation::PacketResult FFMpegReaderImplementation::readAndProcessPacket() {
	AVPacket packet;
	auto result = readPacket(&packet);
	if (result == PacketResult::Ok) {
		processPacket(&packet);
	}
	return result;
}

void FFMpegReaderImplementation::startPacket() {
	if (!_packetStarted && !_packetQueue.isEmpty()) {
		AVPacket packet;
		FFMpeg::packetFromDataWrap(packet, _packetQueue.head());
		_packetStartedSize = packet.size;
		_packetStartedData = packet.data;
		_packetStarted = true;
	}
}

void FFMpegReaderImplementation::finishPacket() {
	if (_packetStarted) {
		AVPacket packet;
		FFMpeg::packetFromDataWrap(packet, _packetQueue.head());
		packet.size = _packetStartedSize;
		packet.data = _packetStartedData;
		_packetStarted = false;
		av_packet_unref(&packet);
		_packetQueue.dequeue();
	}
}

void FFMpegReaderImplementation::clearPacketQueue() {
	finishPacket();
	auto packets = base::take(_packetQueue);
	for (auto &packetData : packets) {
		AVPacket packet;
		FFMpeg::packetFromDataWrap(packet, packetData);
		av_packet_unref(&packet);
	}
}

int FFMpegReaderImplementation::_read(void *opaque, uint8_t *buf, int buf_size) {
	FFMpegReaderImplementation *l = reinterpret_cast<FFMpegReaderImplementation*>(opaque);
	return int(l->_device->read((char*)(buf), buf_size));
}

int64_t FFMpegReaderImplementation::_seek(void *opaque, int64_t offset, int whence) {
	FFMpegReaderImplementation *l = reinterpret_cast<FFMpegReaderImplementation*>(opaque);

	switch (whence) {
	case SEEK_SET: return l->_device->seek(offset) ? l->_device->pos() : -1;
	case SEEK_CUR: return l->_device->seek(l->_device->pos() + offset) ? l->_device->pos() : -1;
	case SEEK_END: return l->_device->seek(l->_device->size() + offset) ? l->_device->pos() : -1;
	case AVSEEK_SIZE: {
		// Special whence for determining filesize without any seek.
		return l->_dataSize;
	} break;
	}
	return -1;
}

} // namespace internal
} // namespace Clip
} // namespace Media
