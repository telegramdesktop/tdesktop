/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "media/media_clip_ffmpeg.h"

#include "media/media_audio.h"
#include "media/media_child_ffmpeg_loader.h"

namespace Media {
namespace Clip {
namespace internal {

FFMpegReaderImplementation::FFMpegReaderImplementation(FileLocation *location, QByteArray *data, uint64 playId) : ReaderImplementation(location, data)
, _playId(playId) {
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

	while (true) {
		while (_packetQueue.isEmpty()) {
			auto packetResult = readPacket();
			if (packetResult == PacketResult::Error) {
				return ReadResult::Error;
			} else if (packetResult == PacketResult::EndOfFile) {
				break;
			}
		}
		bool eofReached = _packetQueue.isEmpty();

		startPacket();

		int got_frame = 0;
		int decoded = 0;
		auto packet = &_packetNull;
		if (!_packetQueue.isEmpty()) {
			packet = &_packetQueue.head();
			decoded = packet->size;
		}

		int res = 0;
		if ((res = avcodec_decode_video2(_codecContext, _frame, &got_frame, packet)) < 0) {
			char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
			LOG(("Gif Error: Unable to avcodec_decode_video2() %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));

			if (res == AVERROR_INVALIDDATA) { // try to skip bad packet
				finishPacket();
				continue;
			}

			eofReached = (res == AVERROR_EOF);
			if (!eofReached || !_hadFrame) { // try to skip end of file
				return ReadResult::Error;
			}
		}
		if (res > 0) decoded = res;

		if (!_packetQueue.isEmpty()) {
			packet->data += decoded;
			packet->size -= decoded;
			if (packet->size <= 0) {
				finishPacket();
			}
		}

		if (got_frame) {
			int64 duration = av_frame_get_pkt_duration(_frame);
			int64 framePts = (_frame->pkt_pts == AV_NOPTS_VALUE) ? _frame->pkt_dts : _frame->pkt_pts;
			int64 frameMs = (framePts * 1000LL * _fmtContext->streams[_streamId]->time_base.num) / _fmtContext->streams[_streamId]->time_base.den;
			_currentFrameDelay = _nextFrameDelay;
			if (_frameMs + _currentFrameDelay < frameMs) {
				_currentFrameDelay = int32(frameMs - _frameMs);
			}
			if (duration == AV_NOPTS_VALUE) {
				_nextFrameDelay = 0;
			} else {
				_nextFrameDelay = (duration * 1000LL * _fmtContext->streams[_streamId]->time_base.num) / _fmtContext->streams[_streamId]->time_base.den;
			}
			_frameMs = frameMs;

			_hadFrame = _frameRead = true;
			_frameTime += _currentFrameDelay;
			return ReadResult::Success;
		}

		if (eofReached) {
			clearPacketQueue();
			if (_mode == Mode::Normal) {
				return ReadResult::Eof;
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
			_lastReadPacketMs = 0;
		}
	}

	return ReadResult::Error;
}

ReaderImplementation::ReadResult FFMpegReaderImplementation::readFramesTill(int64 ms) {
	if (_audioStreamId < 0) { // just keep up
		if (_frameRead && _frameTime > ms) {
			return ReadResult::Success;
		}
		auto readResult = readNextFrame();
		if (readResult != ReadResult::Success || _frameTime > ms) {
			return readResult;
		}
		readResult = readNextFrame();
		if (_frameTime <= ms) {
			_frameTime = ms + 5; // keep up
		}
		return readResult;
	}

	// sync by audio stream
	auto correctMs = audioPlayer()->getVideoCorrectedTime(_playId, ms);

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
	_frameTimeCorrection = ms - correctMs;
	return ReadResult::Success;
}

int64 FFMpegReaderImplementation::frameRealTime() const {
	return _frameMs;
}

uint64 FFMpegReaderImplementation::framePresentationTime() const {
	return static_cast<uint64>(qMax(_frameTime + _frameTimeCorrection, 0LL));
}

int64 FFMpegReaderImplementation::durationMs() const {
	if (_fmtContext->streams[_streamId]->duration == AV_NOPTS_VALUE) return 0;
	return (_fmtContext->streams[_streamId]->duration * 1000LL * _fmtContext->streams[_streamId]->time_base.num) / _fmtContext->streams[_streamId]->time_base.den;
}

void FFMpegReaderImplementation::pauseAudio() {
	if (_audioStreamId >= 0) {
		audioPlayer()->pauseFromVideo(_playId);
	}
}

void FFMpegReaderImplementation::resumeAudio() {
	if (_audioStreamId >= 0) {
		audioPlayer()->resumeFromVideo(_playId);
	}
}

bool FFMpegReaderImplementation::renderFrame(QImage &to, bool &hasAlpha, const QSize &size) {
	t_assert(_frameRead);
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
	if (to.isNull() || to.size() != toSize) {
		to = QImage(toSize, QImage::Format_ARGB32);
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
		uint8_t * toData[1] = { to.bits() };
		int	toLinesize[1] = { to.bytesPerLine() }, res;
		if ((res = sws_scale(_swsContext, _frame->data, _frame->linesize, 0, _frame->height, toData, toLinesize)) != _swsSize.height()) {
			LOG(("Gif Error: Unable to sws_scale to good size %1, height %2, should be %3").arg(logData()).arg(res).arg(_swsSize.height()));
			return false;
		}
	}

	// Read some future packets for audio stream.
	if (_audioStreamId) {
		while (_frameMs + 5000 > _lastReadPacketMs) {
			auto packetResult = readPacket();
			if (packetResult != PacketResult::Ok) {
				break;
			}
		}
	}

	av_frame_unref(_frame);
	return true;
}

bool FFMpegReaderImplementation::start(Mode mode) {
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

	// Get a pointer to the codec context for the video stream
	_codecContext = _fmtContext->streams[_streamId]->codec;
	_codec = avcodec_find_decoder(_codecContext->codec_id);

	_audioStreamId = av_find_best_stream(_fmtContext, AVMEDIA_TYPE_AUDIO, -1, -1, 0, 0);
	if (_mode == Mode::OnlyGifv) {
		if (_audioStreamId >= 0) { // should be no audio stream
			return false;
		}
		if (dataSize() > AnimationInMemory) {
			return false;
		}
		if (_codecContext->codec_id != AV_CODEC_ID_H264) {
			return false;
		}
	} else if (_mode == Mode::Silent || !audioPlayer()) {
		_audioStreamId = -1;
	}
	av_opt_set_int(_codecContext, "refcounted_frames", 1, 0);
	if ((res = avcodec_open2(_codecContext, _codec, 0)) < 0) {
		LOG(("Gif Error: Unable to avcodec_open2 %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		return false;
	}

	if (_audioStreamId >= 0) {
		// Get a pointer to the codec context for the audio stream
		auto audioContextOriginal = _fmtContext->streams[_audioStreamId]->codec;
		auto audioCodec = avcodec_find_decoder(audioContextOriginal->codec_id);

		AVCodecContext *audioContext = avcodec_alloc_context3(audioCodec);
		if ((res = avcodec_copy_context(audioContext, audioContextOriginal)) != 0) {
			LOG(("Gif Error: Unable to avcodec_open2 %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			return false;
		}
		av_opt_set_int(audioContext, "refcounted_frames", 1, 0);
		if ((res = avcodec_open2(audioContext, audioCodec, 0)) < 0) {
			avcodec_free_context(&audioContext);
			LOG(("Gif Error: Unable to avcodec_open2 %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			return false;
		}

		auto soundData = std_::make_unique<VideoSoundData>();
		soundData->context = audioContext;
		soundData->frequency = audioContextOriginal->sample_rate;
		if (_fmtContext->streams[_audioStreamId]->duration == AV_NOPTS_VALUE) {
			soundData->length = (_fmtContext->duration * soundData->frequency) / AV_TIME_BASE;
		} else {
			soundData->length = (_fmtContext->streams[_audioStreamId]->duration * soundData->frequency * _fmtContext->streams[_audioStreamId]->time_base.num) / _fmtContext->streams[_audioStreamId]->time_base.den;
		}
		audioPlayer()->initFromVideo(AudioMsgId(AudioMsgId::Type::Video), _playId, std_::move(soundData), 0);
	}

	return true;
}

QString FFMpegReaderImplementation::logData() const {
	return qsl("for file '%1', data size '%2'").arg(_location ? _location->name() : QString()).arg(_data->size());
}

FFMpegReaderImplementation::~FFMpegReaderImplementation() {
	if (_audioStreamId >= 0) {
		audioPlayer()->stopFromVideo(_playId);
	}
	if (_frameRead) {
		av_frame_unref(_frame);
		_frameRead = false;
	}
	if (_ioContext) av_free(_ioContext);
	if (_codecContext) avcodec_close(_codecContext);
	if (_swsContext) sws_freeContext(_swsContext);
	if (_opened) {
		avformat_close_input(&_fmtContext);
	} else if (_ioBuffer) {
		av_free(_ioBuffer);
	}
	if (_fmtContext) avformat_free_context(_fmtContext);
	av_frame_free(&_frame);

	clearPacketQueue();
}

FFMpegReaderImplementation::PacketResult FFMpegReaderImplementation::readPacket() {
	AVPacket packet;
	av_init_packet(&packet);
	packet.data = nullptr;
	packet.size = 0;

	int res = 0;
	if ((res = av_read_frame(_fmtContext, &packet)) < 0) {
		if (res == AVERROR_EOF) {
			if (_audioStreamId >= 0) {
				// queue terminating packet to audio player
				VideoSoundPart part;
				part.packet = &_packetNull;
				part.videoPlayId = _playId;
				audioPlayer()->feedFromVideo(std_::move(part));
			}
			return PacketResult::EndOfFile;
		}
		char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
		LOG(("Gif Error: Unable to av_read_frame() %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		return PacketResult::Error;
	}

	bool videoPacket = (packet.stream_index == _streamId);
	bool audioPacket = (_audioStreamId >= 0 && packet.stream_index == _audioStreamId);
	if (audioPacket || videoPacket) {
		int64 packetPts = (packet.pts == AV_NOPTS_VALUE) ? packet.dts : packet.pts;
		int64 packetMs = (packetPts * 1000LL * _fmtContext->streams[packet.stream_index]->time_base.num) / _fmtContext->streams[packet.stream_index]->time_base.den;
		_lastReadPacketMs = packetMs;

		if (videoPacket) {
			_packetQueue.enqueue(packet);
		} else if (audioPacket) {
			// queue packet to audio player
			VideoSoundPart part;
			part.packet = &packet;
			part.videoPlayId = _playId;
			audioPlayer()->feedFromVideo(std_::move(part));
		}
	} else {
		av_packet_unref(&packet);
	}
	return PacketResult::Ok;
}

void FFMpegReaderImplementation::startPacket() {
	if (!_packetStarted && !_packetQueue.isEmpty()) {
		_packetStartedSize = _packetQueue.head().size;
		_packetStartedData = _packetQueue.head().data;
		_packetStarted = true;
	}
}

void FFMpegReaderImplementation::finishPacket() {
	if (_packetStarted) {
		_packetQueue.head().size = _packetStartedSize;
		_packetQueue.head().data = _packetStartedData;
		_packetStarted = false;
		av_packet_unref(&_packetQueue.dequeue());
	}
}

void FFMpegReaderImplementation::clearPacketQueue() {
	finishPacket();
	auto packets = createAndSwap(_packetQueue);
	for (auto &packet : packets) {
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
	}
	return -1;
}

} // namespace internal
} // namespace Clip
} // namespace Media
