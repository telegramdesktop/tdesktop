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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"

#include "animation.h"

#include "mainwidget.h"
#include "window.h"

namespace {
	AnimationManager *_manager = 0;
	QVector<QThread*> _clipThreads;
	QVector<ClipReadManager*> _clipManagers;
};

namespace anim {

    float64 linear(const float64 &delta, const float64 &dt) {
		return delta * dt;
	}

	float64 sineInOut(const float64 &delta, const float64 &dt) {
		return -(delta / 2) * (cos(M_PI * dt) - 1);
	}

    float64 halfSine(const float64 &delta, const float64 &dt) {
		return delta * sin(M_PI * dt / 2);
	}

    float64 easeOutBack(const float64 &delta, const float64 &dt) {
		static const float64 s = 1.70158;

		const float64 t = dt - 1;
		return delta * (t * t * ((s + 1) * t + s) + 1);
	}

    float64 easeInCirc(const float64 &delta, const float64 &dt) {
		return -delta * (sqrt(1 - dt * dt) - 1);
	}

    float64 easeOutCirc(const float64 &delta, const float64 &dt) {
		const float64 t = dt - 1;
		return delta * sqrt(1 - t * t);
	}

    float64 easeInCubic(const float64 &delta, const float64 &dt) {
		return delta * dt * dt * dt;
	}

	float64 easeOutCubic(const float64 &delta, const float64 &dt) {
		const float64 t = dt - 1;
		return delta * (t * t * t + 1);
	}

    float64 easeInQuint(const float64 &delta, const float64 &dt) {
		const float64 t2 = dt * dt;
		return delta * t2 * t2 * dt;
	}

    float64 easeOutQuint(const float64 &delta, const float64 &dt) {
		const float64 t = dt - 1, t2 = t * t;
		return delta * (t2 * t2 * t + 1);
	}

	void startManager() {
		stopManager();

		_manager = new AnimationManager();

	}

	void stopManager() {
		delete _manager;
		_manager = 0;
		if (!_clipThreads.isEmpty()) {
			for (int32 i = 0, l = _clipThreads.size(); i < l; ++i) {
				_clipThreads.at(i)->quit();
				_clipThreads.at(i)->wait();
				delete _clipManagers.at(i);
			}
			_clipThreads.clear();
			_clipManagers.clear();
		}
	}

}

void Animation::start() {
	if (!_manager) return;

	_cb->start();
	_manager->start(this);
	_animating = true;
}

void Animation::stop() {
	if (!_manager) return;

	_animating = false;
	_manager->stop(this);
}

void AnimationManager::clipReinit(ClipReader *reader) {
	Notify::clipReinit(reader);
}

void AnimationManager::clipRedraw(ClipReader *reader) {
	Ui::clipRedraw(reader);
}

QPixmap _prepareFrame(const ClipFrameRequest &request, const QImage &original, QImage &cache, bool smooth) {
	bool badSize = (original.width() != request.framew) || (original.height() != request.frameh);
	bool needOuter = (request.outerw != request.framew) || (request.outerh != request.frameh);
	if (badSize || needOuter || request.rounded) {
		int32 factor(request.factor);
		bool fill = false;
		if (cache.width() != request.outerw || cache.height() != request.outerh) {
			cache = QImage(request.outerw, request.outerh, QImage::Format_ARGB32_Premultiplied);
			if (request.framew < request.outerw || request.frameh < request.outerh || original.hasAlphaChannel()) {
				fill = true;
			}
			cache.setDevicePixelRatio(factor);
		}
		{
			Painter p(&cache);
			if (fill) p.fillRect(0, 0, cache.width() / factor, cache.height() / factor, st::black);
			if (smooth && badSize) p.setRenderHint(QPainter::SmoothPixmapTransform);
			QRect to((request.outerw - request.framew) / (2 * factor), (request.outerh - request.frameh) / (2 * factor), request.framew / factor, request.frameh / factor);
			QRect from(0, 0, original.width(), original.height());
			p.drawImage(to, original, from, Qt::ColorOnly);
		}
		if (request.rounded) {
			imageRound(cache);
		}
		return QPixmap::fromImage(cache, Qt::ColorOnly);
	}
	return QPixmap::fromImage(original, Qt::ColorOnly);
}

ClipReader::ClipReader(const FileLocation &location, const QByteArray &data) : _state(ClipReading)
, _width(0)
, _height(0)
, _currentDisplayed(1)
, _paused(0)
, _lastDisplayMs(getms())
, _autoplay(false)
, _private(0) {
	if (_clipThreads.size() < ClipThreadsCount) {
		_threadIndex = _clipThreads.size();
		_clipThreads.push_back(new QThread());
		_clipManagers.push_back(new ClipReadManager(_clipThreads.back()));
		_clipThreads.back()->start();
	} else {
		_threadIndex = int32(MTP::nonce<uint32>() % _clipThreads.size());
		int32 loadLevel = 0x7FFFFFFF;
		for (int32 i = 0, l = _clipThreads.size(); i < l; ++i) {
			int32 level = _clipManagers.at(i)->loadLevel();
			if (level < loadLevel) {
				_threadIndex = i;
				loadLevel = level;
			}
		}
	}
	_clipManagers.at(_threadIndex)->append(this, location, data);
}

void ClipReader::start(int32 framew, int32 frameh, int32 outerw, int32 outerh, bool rounded) {
	if (_clipManagers.size() <= _threadIndex) error();
	if (_state == ClipError) return;

	int32 factor(cIntRetinaFactor());
	_request.factor = factor;
	_request.framew = framew * factor;
	_request.frameh = frameh * factor;
	_request.outerw = outerw * factor;
	_request.outerh = outerh * factor;
	_request.rounded = rounded;
	_clipManagers.at(_threadIndex)->start(this);
}

QPixmap ClipReader::current(int32 framew, int32 frameh, int32 outerw, int32 outerh, uint64 ms) {
	_lastDisplayMs.set(ms);
	_currentDisplayed.set(true);
	if (_paused.get()) {
		_paused.set(false);
		if (_clipManagers.size() <= _threadIndex) error();
		if (_state != ClipError) {
			_clipManagers.at(_threadIndex)->update(this);
		}
	}

	int32 factor(cIntRetinaFactor());
	QPixmap result(_current);
	if (result.width() == outerw * factor && result.height() == outerh * factor) {
		return result;
	}

	_request.framew = framew * factor;
	_request.frameh = frameh * factor;
	_request.outerw = outerw * factor;
	_request.outerh = outerh * factor;

	QImage current(_currentOriginal);
	result = _current = QPixmap();
	result = _current = _prepareFrame(_request, current, _cacheForResize, true);

	if (_clipManagers.size() <= _threadIndex) error();
	if (_state != ClipError) {
		_clipManagers.at(_threadIndex)->update(this);
	}

	return result;
}

bool ClipReader::ready() const {
	if (_width && _height) return true;

	QImage first(_currentOriginal);
	if (first.isNull()) return false;

	_width = first.width();
	_height = first.height();
	return true;
}

int32 ClipReader::width() const {
	return _width;
}

int32 ClipReader::height() const {
	return _height;
}

ClipState ClipReader::state() const {
	return _state;
}

void ClipReader::stop() {
	if (_clipManagers.size() <= _threadIndex) error();
	if (_state != ClipError) {
		_clipManagers.at(_threadIndex)->stop(this);
		_width = _height = 0;
	}
}

void ClipReader::error() {
	_private = 0;
	_state = ClipError;
}

ClipReader::~ClipReader() {
	stop();
}

class ClipReaderImplementation {
public:

	ClipReaderImplementation(FileLocation *location, QByteArray *data) : _location(location), _data(data), _device(0) {
	}
	virtual bool readNextFrame(QImage &to) = 0;
	virtual int32 nextFrameDelay() = 0;
	virtual bool start() = 0;
	virtual ~ClipReaderImplementation() {
	}

protected:
	FileLocation *_location;
	QByteArray *_data;
	QFile _file;
	QBuffer _buffer;
	QIODevice *_device;

	void initDevice() {
		if (_data->isEmpty()) {
			if (_file.isOpen()) _file.close();
			_file.setFileName(_location->name());
		} else {
			if (_buffer.isOpen()) _buffer.close();
			_buffer.setBuffer(_data);
		}
		_device = _data->isEmpty() ? static_cast<QIODevice*>(&_file) : static_cast<QIODevice*>(&_buffer);
	}

};

class QtGifReaderImplementation : public ClipReaderImplementation{
public:

	QtGifReaderImplementation(FileLocation *location, QByteArray *data) : ClipReaderImplementation(location, data)
	, _reader(0)
	, _framesLeft(0)
	, _frameDelay(0) {
	}

	bool readNextFrame(QImage &to) {
		if (_reader) _frameDelay = _reader->nextImageDelay();
		if (_framesLeft < 1 && !jumpToStart()) {
			return false;
		}

		QImage frame; // QGifHandler always reads first to internal QImage and returns it
		if (!_reader->read(&frame)) {
			return false;
		}
		--_framesLeft;

		int32 w = frame.width(), h = frame.height();
		if (to.width() == w && to.height() == h && to.format() == frame.format()) {
			if (to.byteCount() != frame.byteCount()) {
				int bpl = qMin(to.bytesPerLine(), frame.bytesPerLine());
				for (int i = 0; i < h; ++i) {
					memcpy(to.scanLine(i), frame.constScanLine(i), bpl);
				}
			} else {
				memcpy(to.bits(), frame.constBits(), frame.byteCount());
			}
		} else {
			to = frame.copy();
		}
		return true;
	}

	int32 nextFrameDelay() {
		return _frameDelay;
	}

	bool start() {
		return jumpToStart();
	}

	~QtGifReaderImplementation() {
		delete _reader;
		setBadPointer(_reader);
	}

private:
	QImageReader *_reader;
	int32 _framesLeft, _frameDelay;

	bool jumpToStart() {
		if (_reader && _reader->jumpToImage(0)) {
			_framesLeft = _reader->imageCount();
			return true;
		}

		delete _reader;
		initDevice();
		_reader = new QImageReader(_device);
#if QT_VERSION >= QT_VERSION_CHECK(5, 5, 0)
		_reader->setAutoTransform(true);
#endif
		if (!_reader->canRead() || !_reader->supportsAnimation()) {
			return false;
		}
		_framesLeft = _reader->imageCount();
		if (_framesLeft < 1) {
			return false;
		}
		return true;
	}

};

class FFMpegReaderImplementation : public ClipReaderImplementation {
public:

	FFMpegReaderImplementation(FileLocation *location, QByteArray *data) : ClipReaderImplementation(location, data)
		, _ioBuffer(0)
		, _ioContext(0)
		, _fmtContext(0)
		, _codec(0)
		, _codecContext(0)
		, _streamId(0)
		, _frame(0)
		, _opened(false)
		, _hadFrame(false)
		, _packetSize(0)
		, _packetData(0)
		, _packetWas(false)
		, _width(0)
		, _height(0)
		, _swsContext(0)
		, _frameMs(0)
		, _nextFrameDelay(0)
		, _currentFrameDelay(0) {
		_frame = av_frame_alloc();
		av_init_packet(&_avpkt);
		_avpkt.data = NULL;
		_avpkt.size = 0;
	}

	bool readNextFrame(QImage &to) {
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

					return false;
				}
				if (res > 0) decoded = res;
			}
			if (!finished) {
				_avpkt.data += decoded;
				_avpkt.size -= decoded;
				if (_avpkt.size <= 0) freePacket();
			}

			if (got_frame) {
				_hadFrame = true;

				if (!_width || !_height) {
					_width = _frame->width;
					_height = _frame->height;
					if (!_width || !_height) {
						LOG(("Gif Error: Bad frame size %1").arg(logData()));
						return false;
					}
				}

				if (to.isNull() || to.width() != _width || to.height() != _height) {
					to = QImage(_width, _height, QImage::Format_ARGB32);
				}
				if (_frame->width == _width && _frame->height == _height && (_frame->format == AV_PIX_FMT_BGRA || (_frame->format == -1 && _codecContext->pix_fmt == AV_PIX_FMT_BGRA))) {
					int32 sbpl = _frame->linesize[0], dbpl = to.bytesPerLine(), bpl = qMin(sbpl, dbpl);
					uchar *s = _frame->data[0], *d = to.bits();
					for (int32 i = 0, l = _frame->height; i < l; ++i) {
						memcpy(d + i * dbpl, s + i * sbpl, bpl);
					}
				} else {
					if (_frame->width != _width || _frame->height != _height || (_frame->format != -1 && _frame->format != _codecContext->pix_fmt) || !_swsContext) {
						_swsContext = sws_getCachedContext(_swsContext, _frame->width, _frame->height, AVPixelFormat(_frame->format), _width, _height, AV_PIX_FMT_BGRA, 0, 0, 0, 0);
					}
					uint8_t * toData[1] = { to.bits() };
					int	toLinesize[1] = { to.bytesPerLine() };
					if ((res = sws_scale(_swsContext, _frame->data, _frame->linesize, 0, _frame->height, toData, toLinesize)) != _height) {
						LOG(("Gif Error: Unable to sws_scale to good size %1, hieght %2, should be %3").arg(logData()).arg(res).arg(_height));
						return false;
					}
				}

				int64 duration = av_frame_get_pkt_duration(_frame);
				int64 framePts = (_frame->pkt_pts == AV_NOPTS_VALUE) ? _frame->pkt_dts : _frame->pkt_pts;
				int64 frameMs = (framePts * 1000LL * _fmtContext->streams[_streamId]->time_base.num) / _fmtContext->streams[_streamId]->time_base.den;
				_currentFrameDelay = _nextFrameDelay;
				if (_frameMs + _currentFrameDelay < frameMs) {
					_currentFrameDelay = int32(frameMs - _frameMs);
				}
				_frameMs = frameMs;
				if (duration == AV_NOPTS_VALUE) {
					_nextFrameDelay = 0;
				} else {
					_nextFrameDelay = (duration * 1000LL * _fmtContext->streams[_streamId]->time_base.num) / _fmtContext->streams[_streamId]->time_base.den;
				}

				av_frame_unref(_frame);
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

	int32 nextFrameDelay() {
		return _currentFrameDelay;
	}

	QString logData() const {
		return qsl("for file '%1', data size '%2'").arg(_location ? _location->name() : QString()).arg(_data->size());
	}

	bool start() {
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
		av_opt_set_int(_codecContext, "refcounted_frames", 1, 0);
		if ((res = avcodec_open2(_codecContext, _codec, 0)) < 0) {
			LOG(("Gif Error: Unable to avcodec_open2 %1, error %2, %3").arg(logData()).arg(res).arg(av_make_error_string(err, sizeof(err), res)));
			return false;
		}

		return true;
	}

	~FFMpegReaderImplementation() {
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

private:
	uchar *_ioBuffer;
	AVIOContext *_ioContext;
	AVFormatContext *_fmtContext;
	AVCodec *_codec;
	AVCodecContext *_codecContext;
	int32 _streamId;
	AVFrame *_frame;
	bool _opened, _hadFrame;

	AVPacket _avpkt;
	int _packetSize;
	uint8_t *_packetData;
	bool _packetWas;
	void rememberPacket() {
		if (!_packetWas) {
			_packetSize = _avpkt.size;
			_packetData = _avpkt.data;
			_packetWas = true;
		}
	}
	void freePacket() {
		if (_packetWas) {
			_avpkt.size = _packetSize;
			_avpkt.data = _packetData;
			_packetWas = false;
			av_packet_unref(&_avpkt);
		}
	}

	int32 _width, _height;
	SwsContext *_swsContext;

	int64 _frameMs;
	int32 _nextFrameDelay, _currentFrameDelay;

	static int _read(void *opaque, uint8_t *buf, int buf_size) {
		FFMpegReaderImplementation *l = reinterpret_cast<FFMpegReaderImplementation*>(opaque);
		return int(l->_device->read((char*)(buf), buf_size));
	}

	static int64_t _seek(void *opaque, int64_t offset, int whence) {
		FFMpegReaderImplementation *l = reinterpret_cast<FFMpegReaderImplementation*>(opaque);

		switch (whence) {
		case SEEK_SET: return l->_device->seek(offset) ? l->_device->pos() : -1;
		case SEEK_CUR: return l->_device->seek(l->_device->pos() + offset) ? l->_device->pos() : -1;
		case SEEK_END: return l->_device->seek(l->_device->size() + offset) ? l->_device->pos() : -1;
		}
		return -1;
	}

};

class ClipReaderPrivate {
public:

	ClipReaderPrivate(ClipReader *reader, const FileLocation &location, const QByteArray &data) : _interface(reader)
	, _state(ClipReading)
	, _data(data)
	, _location(_data.isEmpty() ? new FileLocation(location) : 0)
	, _accessed(false)
	, _implementation(0)
	, _previousMs(0)
	, _currentMs(0)
	, _nextUpdateMs(0)
	, _paused(false) {
		if (_data.isEmpty() && !_location->accessEnable()) {
			error();
			return;
		}
		_accessed = true;
	}

	ClipProcessResult start(uint64 ms) {
		_nextUpdateMs = ms + 86400 * 1000ULL;
		if (!_implementation && !init()) {
			return error();
		}
		if (_currentOriginal.isNull()) {
			if (!_implementation->readNextFrame(_currentOriginal)) {
				return error();
			}
			return ClipProcessReinit;
		}
		return ClipProcessWait;
	}

	ClipProcessResult process(uint64 ms) { // -1 - do nothing, 0 - update, 1 - reinit
		if (_state == ClipError) return ClipProcessError;

		if (!_request.valid()) {
			return start(ms);
		}

		if (_current.isNull()) { // first frame read, but not yet prepared
			_currentOriginal.setDevicePixelRatio(_request.factor);

			_previousMs = _currentMs;
			_currentMs = ms;
			_current = _prepareFrame(_request, _currentOriginal, _currentCache, true);

			if (!prepareNextFrame()) {
				return error();
			}
			return ClipProcessStarted;
		} else if (!_paused && ms >= _nextUpdateMs) {
			swapBuffers();
			return ClipProcessRedraw;
		}
		return ClipProcessWait;
	}

	ClipProcessResult finishProcess(uint64 ms) {
		if (!prepareNextFrame()) {
			return error();
		}

		if (ms >= _nextUpdateMs) { // we are late
			swapBuffers(ms); // keep up
			return ClipProcessRedraw;
		}
		return ClipProcessWait;
	}

	uint64 nextFrameDelay() {
		int32 delay = _implementation->nextFrameDelay();
		return qMax(delay, 5);
	}

	void swapBuffers(uint64 ms = 0) {
		_previousMs = _currentMs;
		_currentMs = qMax(ms, _nextUpdateMs);
		qSwap(_currentOriginal, _nextOriginal);
		qSwap(_current, _next);
		qSwap(_currentCache, _nextCache);
	}

	bool prepareNextFrame() {
		if (!_implementation->readNextFrame(_nextOriginal)) {
			return false;
		}
		_nextUpdateMs = _currentMs + nextFrameDelay();
		_nextOriginal.setDevicePixelRatio(_request.factor);
		_next = QPixmap();
		_next = _prepareFrame(_request, _nextOriginal, _nextCache, true);
		return true;
	}

	bool init() {
		if (_data.isEmpty() && QFileInfo(_location->name()).size() <= AnimationInMemory) {
			QFile f(_location->name());
			if (f.open(QIODevice::ReadOnly)) {
				_data = f.readAll();
				if (f.error() != QFile::NoError) {
					_data = QByteArray();
				}
			}
		}

		_implementation = new FFMpegReaderImplementation(_location, &_data);
//		_implementation = new QtGifReaderImplementation(_location, &_data);
		return _implementation->start();
	}

	ClipProcessResult error() {
		stop();
		_state = ClipError;
		return ClipProcessError;
	}

	void stop() {
		delete _implementation;
		_implementation = 0;

		if (_location) {
			if (_accessed) {
				_location->accessDisable();
			}
			delete _location;
			_location = 0;
		}
		_accessed = false;
	}

	~ClipReaderPrivate() {
		stop();
		setBadPointer(_location);
		setBadPointer(_implementation);
		_data.clear();
	}

private:

	ClipReader *_interface;
	ClipState _state;

	QByteArray _data;
	FileLocation *_location;
	bool _accessed;

	QBuffer _buffer;
	ClipReaderImplementation *_implementation;

	ClipFrameRequest _request;
	QPixmap _current, _next;
	QImage _currentOriginal, _nextOriginal, _currentCache, _nextCache;

	uint64 _previousMs, _currentMs, _nextUpdateMs;

	bool _paused;

	friend class ClipReadManager;

};

ClipReadManager::ClipReadManager(QThread *thread) : _processingInThread(0), _needReProcess(false) {
	moveToThread(thread);
	connect(thread, SIGNAL(started()), this, SLOT(process()));
	connect(this, SIGNAL(processDelayed()), this, SLOT(process()), Qt::QueuedConnection);

	_timer.setSingleShot(true);
	_timer.moveToThread(thread);
	connect(&_timer, SIGNAL(timeout()), this, SLOT(process()));

	connect(this, SIGNAL(reinit(ClipReader*)), _manager, SLOT(clipReinit(ClipReader*)));
	connect(this, SIGNAL(redraw(ClipReader*)), _manager, SLOT(clipRedraw(ClipReader*)));
}

void ClipReadManager::append(ClipReader *reader, const FileLocation &location, const QByteArray &data) {
	reader->_private = new ClipReaderPrivate(reader, location, data);
	_loadLevel.fetchAndAddRelease(AverageGifSize);
	update(reader);
}

void ClipReadManager::start(ClipReader *reader) {
	update(reader);
}

void ClipReadManager::update(ClipReader *reader) {
	QMutexLocker lock(&_readerPointersMutex);
	_readerPointers.insert(reader, reader->_private);
	emit processDelayed();
}

void ClipReadManager::stop(ClipReader *reader) {
	QMutexLocker lock(&_readerPointersMutex);
	_readerPointers.remove(reader);
	emit processDelayed();
}

bool ClipReadManager::handleProcessResult(ClipReaderPrivate *reader, ClipProcessResult result, uint64 ms) {
	QMutexLocker lock(&_readerPointersMutex);
	ReaderPointers::iterator it = _readerPointers.find(reader->_interface);
	if (result == ClipProcessError) {
		if (it != _readerPointers.cend()) {
			it.key()->error();
			emit reinit(it.key());

			_readerPointers.erase(it);
			it = _readerPointers.end();
		}
	}
	if (it == _readerPointers.cend()) {
		return false;
	}

	if (result == ClipProcessStarted) {
		_loadLevel.fetchAndAddRelease(reader->_currentOriginal.width() * reader->_currentOriginal.height() - AverageGifSize);
	}
	if (!reader->_paused && (result == ClipProcessRedraw || result == ClipProcessWait)) {
		if (it.key()->_lastDisplayMs.get() + WaitBeforeGifPause < qMax(reader->_previousMs, ms)) {
			reader->_paused = true;
			it.key()->_paused.set(true);
			if (it.key()->_lastDisplayMs.get() + WaitBeforeGifPause >= qMax(reader->_previousMs, ms)) {
				it.key()->_paused.set(false);
				reader->_paused = false;
			} else {
				result = ClipProcessReinit;
			}
		}
	}
	if (result == ClipProcessReinit || result == ClipProcessRedraw || result == ClipProcessStarted) {
		it.key()->_current = reader->_current;
		it.key()->_currentOriginal = reader->_currentOriginal;
		it.key()->_currentDisplayed.set(false);
		if (result == ClipProcessReinit) {
			emit reinit(it.key());
		} else if (result == ClipProcessRedraw) {
			emit redraw(it.key());
		}
	}
	return true;
}

ClipReadManager::ResultHandleState ClipReadManager::handleResult(ClipReaderPrivate *reader, ClipProcessResult result, uint64 ms) {
	if (!handleProcessResult(reader, result, ms)) {
		_loadLevel.fetchAndAddRelease(-1 * (reader->_currentOriginal.isNull() ? AverageGifSize : reader->_currentOriginal.width() * reader->_currentOriginal.height()));
		delete reader;
		return ResultHandleRemove;
	}

	_processingInThread->eventDispatcher()->processEvents(QEventLoop::AllEvents);
	if (_processingInThread->isInterruptionRequested()) {
		return ResultHandleStop;
	}

	if (result == ClipProcessRedraw) {
		return handleResult(reader, reader->finishProcess(ms), ms);
	}

	return ResultHandleContinue;
}

void ClipReadManager::process() {
	if (_processingInThread) {
		_needReProcess = true;
		return;
	}

	_timer.stop();
	_processingInThread = thread();

	uint64 ms = getms(), minms = ms + 86400 * 1000ULL;
	{
		QMutexLocker lock(&_readerPointersMutex);
		for (ReaderPointers::iterator i = _readerPointers.begin(), e = _readerPointers.end(); i != e; ++i) {
			if (i.value()) {
				Readers::iterator it = _readers.find(i.value());
				if (it == _readers.cend()) {
					_readers.insert(i.value(), 0);
				} else {
					it.value() = ms;
					if (it.key()->_paused && !i.key()->_paused.get()) {
						it.key()->_paused = false;
					}
				}
				i.value()->_request = i.key()->_request;
				i.value() = 0;
			}
		}
	}

	for (Readers::iterator i = _readers.begin(), e = _readers.end(); i != e;) {
		if (i.value() <= ms) {
			ClipProcessResult result = i.key()->process(ms);

			ResultHandleState state = handleResult(i.key(), result, ms);
			if (state == ResultHandleRemove) {
				i = _readers.erase(i);
				continue;
			} else if (state == ResultHandleStop) {
				_processingInThread = 0;
				return;
			}
			i.value() = i.key()->_nextUpdateMs;
			ms = getms();
		}
		if (!i.key()->_paused && i.value() < minms) {
			minms = i.value();
		}
		++i;
	}

	ms = getms();
	if (_needReProcess || minms <= ms) {
		_needReProcess = false;
		_timer.start(1);
	} else {
		_timer.start(minms - ms);
	}

	_processingInThread = 0;
}

ClipReadManager::~ClipReadManager() {
	{
		QMutexLocker lock(&_readerPointersMutex);
		for (ReaderPointers::iterator i = _readerPointers.begin(), e = _readerPointers.end(); i != e; ++i) {
			if (i.value()) {
				i.key()->_private = 0;
			}
		}
		_readerPointers.clear();

		for (Readers::iterator i = _readers.begin(), e = _readers.end(); i != e; ++i) {
			delete i.key();
		}
		_readers.clear();
	}
}
