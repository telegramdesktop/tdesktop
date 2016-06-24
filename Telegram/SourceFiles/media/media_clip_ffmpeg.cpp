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

namespace Media {
namespace Clip {
namespace internal {

FFMpegReaderImplementation::FFMpegReaderImplementation(FileLocation *location, QByteArray *data) : ReaderImplementation(location, data) {
	_frame = av_frame_alloc();
	av_init_packet(&_avpkt);
	_avpkt.data = NULL;
	_avpkt.size = 0;
}

bool FFMpegReaderImplementation::readNextFrame() {
	if (_frameRead) {
		av_frame_unref(_frame);
		_frameRead = false;
	}

	int res;
	while (true) {
		if (_avpkt.size > 0) { // previous packet not finished
			res = 0;
		} else if ((res = av_read_frame(_fmtContext, &_avpkt)) < 0) {
			if (res != AVERROR_EOF || !_hadFrame) {
				char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
				LOG(("Gif Error: Unable to av_read_frame() %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
				return false;
			}
		}

		bool finished = (res < 0);
		if (finished) {
			_avpkt.data = NULL;
			_avpkt.size = 0;
		} else {
			rememberPacket();
		}

		int32 got_frame = 0;
		int32 decoded = _avpkt.size;
		if (_avpkt.stream_index == _streamId) {
			if ((res = avcodec_decode_video2(_codecContext, _frame, &got_frame, &_avpkt)) < 0) {
				char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
				LOG(("Gif Error: Unable to avcodec_decode_video2() %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));

				if (res == AVERROR_INVALIDDATA) { // try to skip bad packet
					freePacket();
					_avpkt.data = NULL;
					_avpkt.size = 0;
					continue;
				}

				if (res != AVERROR_EOF || !_hadFrame) { // try to skip end of file
					return false;
				}
				freePacket();
				_avpkt.data = NULL;
				_avpkt.size = 0;
				continue;
			}
			if (res > 0) decoded = res;
		} else if (_audioStreamId >= 0 && _avpkt.stream_index == _audioStreamId) {
			freePacket();
			continue;
		}
		if (!finished) {
			_avpkt.data += decoded;
			_avpkt.size -= decoded;
			if (_avpkt.size <= 0) freePacket();
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
			return true;
		}

		if (finished) {
			if ((res = avformat_seek_file(_fmtContext, _streamId, std::numeric_limits<int64_t>::min(), 0, std::numeric_limits<int64_t>::max(), 0)) < 0) {
				if ((res = av_seek_frame(_fmtContext, _streamId, 0, AVSEEK_FLAG_BYTE)) < 0) {
					if ((res = av_seek_frame(_fmtContext, _streamId, 0, AVSEEK_FLAG_FRAME)) < 0) {
						if ((res = av_seek_frame(_fmtContext, _streamId, 0, 0)) < 0) {
							char err[AV_ERROR_MAX_STRING_SIZE] = { 0 };
							LOG(("Gif Error: Unable to av_seek_frame() to the start %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
							return false;
						}
					}
				}
			}
			avcodec_flush_buffers(_codecContext);
			_hadFrame = false;
			_frameMs = 0;
		}
	}

	return false;
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

	av_frame_unref(_frame);
	return true;
}

int FFMpegReaderImplementation::nextFrameDelay() {
	return _currentFrameDelay;
}

bool FFMpegReaderImplementation::start(bool onlyGifv) {
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

	// Get a pointer to the codec context for the audio stream
	_codecContext = _fmtContext->streams[_streamId]->codec;
	_codec = avcodec_find_decoder(_codecContext->codec_id);

	_audioStreamId = av_find_best_stream(_fmtContext, AVMEDIA_TYPE_AUDIO, -1, -1, 0, 0);
	if (onlyGifv) {
		if (_audioStreamId >= 0) { // should be no audio stream
			return false;
		}
		if (dataSize() > AnimationInMemory) {
			return false;
		}
		if (_codecContext->codec_id != AV_CODEC_ID_H264) {
			return false;
		}
	}
	av_opt_set_int(_codecContext, "refcounted_frames", 1, 0);
	if ((res = avcodec_open2(_codecContext, _codec, 0)) < 0) {
		LOG(("Gif Error: Unable to avcodec_open2 %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
		return false;
	}

	return true;
}

QString FFMpegReaderImplementation::logData() const {
	return qsl("for file '%1', data size '%2'").arg(_location ? _location->name() : QString()).arg(_data->size());
}

int FFMpegReaderImplementation::duration() const {
	if (_fmtContext->streams[_streamId]->duration == AV_NOPTS_VALUE) return 0;
	return (_fmtContext->streams[_streamId]->duration * _fmtContext->streams[_streamId]->time_base.num) / _fmtContext->streams[_streamId]->time_base.den;
}

FFMpegReaderImplementation::~FFMpegReaderImplementation() {
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
	freePacket();
}

void FFMpegReaderImplementation::rememberPacket() {
	if (!_packetWas) {
		_packetSize = _avpkt.size;
		_packetData = _avpkt.data;
		_packetWas = true;
	}
}

void FFMpegReaderImplementation::freePacket() {
	if (_packetWas) {
		_avpkt.size = _packetSize;
		_avpkt.data = _packetData;
		_packetWas = false;
		av_packet_unref(&_avpkt);
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
