/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/clip/media_clip_ffmpeg.h"

#include "media/audio/media_audio.h"
#include "media/audio/media_child_ffmpeg_loader.h"
#include "storage/file_download.h"

namespace Media {
namespace Clip {
namespace internal {
namespace {

constexpr auto kSkipInvalidDataPackets = 10;
constexpr auto kMaxInlineArea = 1280 * 720;
constexpr auto kMaxSendingArea = 3840 * 2160; // usual 4K

// See https://github.com/telegramdesktop/tdesktop/issues/7225
constexpr auto kAlignImageBy = 64;

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
	return QImage(alignedbuffer, width, height, bytesperline, QImage::Format_ARGB32_Premultiplied, alignedImageBufferCleanupHandler, cleanupdata);
}

bool isAlignedImage(const QImage &image) {
	return !(reinterpret_cast<uintptr_t>(image.constBits()) % kAlignImageBy) && !(image.bytesPerLine() % kAlignImageBy);
}

} // namespace

FFMpegReaderImplementation::FFMpegReaderImplementation(
	FileLocation *location,
	QByteArray *data,
	const AudioMsgId &audio)
: ReaderImplementation(location, data)
, _frame(FFmpeg::MakeFramePointer())
, _audioMsgId(audio) {
}

ReaderImplementation::ReadResult FFMpegReaderImplementation::readNextFrame() {
	do {
		int res = avcodec_receive_frame(_codecContext, _frame.get());
		if (res >= 0) {
			const auto limit = (_mode == Mode::Inspecting)
				? kMaxSendingArea
				: kMaxInlineArea;
			if (_frame->width * _frame->height > limit) {
				return ReadResult::Error;
			}
			processReadFrame();
			return ReadResult::Success;
		}

		if (res == AVERROR_EOF) {
			_packetQueue.clear();
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

		while (_packetQueue.empty()) {
			auto packetResult = readAndProcessPacket();
			if (packetResult == PacketResult::Error) {
				return ReadResult::Error;
			} else if (packetResult == PacketResult::EndOfFile) {
				break;
			}
		}
		if (_packetQueue.empty()) {
			avcodec_send_packet(_codecContext, nullptr); // drain
			continue;
		}

		auto packet = std::move(_packetQueue.front());
		_packetQueue.pop_front();

		const auto native = &packet.fields();
		const auto guard = gsl::finally([
			&,
			size = native->size,
			data = native->data
		] {
			native->size = size;
			native->data = data;
			packet = FFmpeg::Packet();
		});

		res = avcodec_send_packet(_codecContext, native);
		if (res < 0) {
			char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
			LOG(("Gif Error: Unable to avcodec_send_packet() %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			if (res == AVERROR_INVALIDDATA) {
				if (++_skippedInvalidDataPackets < kSkipInvalidDataPackets) {
					continue; // try to skip bad packet
				}
			}
			return ReadResult::Error;
		}
	} while (true);

	return ReadResult::Error;
}

void FFMpegReaderImplementation::processReadFrame() {
	int64 duration = _frame->pkt_duration;
	int64 framePts = _frame->pts;
	crl::time frameMs = (framePts * 1000LL * _fmtContext->streams[_streamId]->time_base.num) / _fmtContext->streams[_streamId]->time_base.den;
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

ReaderImplementation::ReadResult FFMpegReaderImplementation::readFramesTill(crl::time frameMs, crl::time systemMs) {
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
	auto correctMs = (frameMs >= 0)
		? Player::mixer()->getExternalCorrectedTime(
			_audioMsgId,
			frameMs,
			systemMs)
		: frameMs;
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

crl::time FFMpegReaderImplementation::frameRealTime() const {
	return _frameMs;
}

crl::time FFMpegReaderImplementation::framePresentationTime() const {
	return qMax(_frameTime + _frameTimeCorrection, crl::time(0));
}

crl::time FFMpegReaderImplementation::durationMs() const {
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
			_swsContext = sws_getCachedContext(_swsContext, _frame->width, _frame->height, AVPixelFormat(_frame->format), toSize.width(), toSize.height(), AV_PIX_FMT_BGRA, 0, nullptr, nullptr, nullptr);
		}
		// AV_NUM_DATA_POINTERS defined in AVFrame struct
		uint8_t *toData[AV_NUM_DATA_POINTERS] = { to.bits(), nullptr };
		int toLinesize[AV_NUM_DATA_POINTERS] = { to.bytesPerLine(), 0 };
		sws_scale(_swsContext, _frame->data, _frame->linesize, 0, _frame->height, toData, toLinesize);
	}
	if (hasAlpha) {
		FFmpeg::PremultiplyInplace(to);
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

	FFmpeg::ClearFrameMemory(_frame.get());

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

bool FFMpegReaderImplementation::start(Mode mode, crl::time &positionMs) {
	_mode = mode;

	initDevice();
	if (!_device->open(QIODevice::ReadOnly)) {
		LOG(("Gif Error: Unable to open device %1").arg(logData()));
		return false;
	}
	_ioBuffer = (uchar*)av_malloc(AVBlockSize);
	_ioContext = avio_alloc_context(_ioBuffer, AVBlockSize, 0, static_cast<void*>(this), &FFMpegReaderImplementation::_read, nullptr, &FFMpegReaderImplementation::_seek);
	_fmtContext = avformat_alloc_context();
	if (!_fmtContext) {
		LOG(("Gif Error: Unable to avformat_alloc_context %1").arg(logData()));
		return false;
	}
	_fmtContext->pb = _ioContext;

	int res = 0;
	char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
	if ((res = avformat_open_input(&_fmtContext, nullptr, nullptr, nullptr)) < 0) {
		_ioBuffer = nullptr;

		LOG(("Gif Error: Unable to avformat_open_input %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		return false;
	}
	_opened = true;

	if ((res = avformat_find_stream_info(_fmtContext, nullptr)) < 0) {
		LOG(("Gif Error: Unable to avformat_find_stream_info %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		return false;
	}

	_streamId = av_find_best_stream(_fmtContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	if (_streamId < 0) {
		LOG(("Gif Error: Unable to av_find_best_stream %1, error %2, %3").arg(logData()).arg(_streamId).arg(av_make_error_string(err, sizeof(err), _streamId)));
		return false;
	}

	auto rotateTag = av_dict_get(_fmtContext->streams[_streamId]->metadata, "rotate", nullptr, 0);
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

	const auto codec = avcodec_find_decoder(_codecContext->codec_id);

	_audioStreamId = av_find_best_stream(_fmtContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
	if (_mode == Mode::Inspecting) {
		_hasAudioStream = (_audioStreamId >= 0);
		_audioStreamId = -1;
	} else if (_mode == Mode::Silent || !_audioMsgId.externalPlayId()) {
		_audioStreamId = -1;
	}

	if ((res = avcodec_open2(_codecContext, codec, nullptr)) < 0) {
		LOG(("Gif Error: Unable to avcodec_open2 %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		return false;
	}

	std::unique_ptr<ExternalSoundData> soundData;
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

		const auto audioCodec = avcodec_find_decoder(audioContext->codec_id);
		if ((res = avcodec_open2(audioContext, audioCodec, 0)) < 0) {
			avcodec_free_context(&audioContext);
			LOG(("Gif Error: Unable to avcodec_open2 %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			_audioStreamId = -1;
		} else {
			soundData = std::make_unique<ExternalSoundData>();
			soundData->codec = FFmpeg::CodecPointer(audioContext);
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

	FFmpeg::Packet packet;
	auto readResult = readPacket(packet);
	if (readResult == PacketResult::Ok && positionMs > 0) {
		positionMs = countPacketMs(packet);
	}

	if (hasAudio()) {
		Player::mixer()->play(_audioMsgId, std::move(soundData), positionMs);
	}

	if (readResult == PacketResult::Ok) {
		processPacket(std::move(packet));
	}

	return true;
}

bool FFMpegReaderImplementation::inspectAt(crl::time &positionMs) {
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

	FFmpeg::Packet packet;
	auto readResult = readPacket(packet);
	if (readResult == PacketResult::Ok && positionMs > 0) {
		positionMs = countPacketMs(packet);
	}

	if (readResult == PacketResult::Ok) {
		processPacket(std::move(packet));
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
}

FFMpegReaderImplementation::PacketResult FFMpegReaderImplementation::readPacket(FFmpeg::Packet &packet) {
	int res = 0;
	if ((res = av_read_frame(_fmtContext, &packet.fields())) < 0) {
		if (res == AVERROR_EOF) {
			if (_audioStreamId >= 0) {
				// queue terminating packet to audio player
				auto empty = FFmpeg::Packet();
				Player::mixer()->feedFromExternal({
					_audioMsgId,
					gsl::make_span(&empty, 1)
				});
			}
			return PacketResult::EndOfFile;
		}
		char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
		LOG(("Gif Error: Unable to av_read_frame() %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		return PacketResult::Error;
	}
	return PacketResult::Ok;
}

void FFMpegReaderImplementation::processPacket(FFmpeg::Packet &&packet) {
	const auto &native = packet.fields();
	auto videoPacket = (native.stream_index == _streamId);
	auto audioPacket = (_audioStreamId >= 0 && native.stream_index == _audioStreamId);
	if (audioPacket || videoPacket) {
		if (videoPacket) {
			_lastReadVideoMs = countPacketMs(packet);

			_packetQueue.push_back(std::move(packet));
		} else if (audioPacket) {
			_lastReadAudioMs = countPacketMs(packet);

			// queue packet to audio player
			Player::mixer()->feedFromExternal({
				_audioMsgId,
				gsl::make_span(&packet, 1)
			});
		}
	}
}

crl::time FFMpegReaderImplementation::countPacketMs(
		const FFmpeg::Packet &packet) const {
	const auto &native = packet.fields();
	int64 packetPts = (native.pts == AV_NOPTS_VALUE) ? native.dts : native.pts;
	crl::time packetMs = (packetPts * 1000LL * _fmtContext->streams[native.stream_index]->time_base.num) / _fmtContext->streams[native.stream_index]->time_base.den;
	return packetMs;
}

FFMpegReaderImplementation::PacketResult FFMpegReaderImplementation::readAndProcessPacket() {
	FFmpeg::Packet packet;
	auto result = readPacket(packet);
	if (result == PacketResult::Ok) {
		processPacket(std::move(packet));
	}
	return result;
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
