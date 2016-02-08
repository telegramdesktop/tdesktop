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
				delete _clipThreads.at(i);
			}
			_clipThreads.clear();
			_clipManagers.clear();
		}
	}

}

void Animation::start() {
	if (!_manager) return;

	_cb.start();
	_manager->start(this);
	_animating = true;
}

void Animation::stop() {
	if (!_manager) return;

	_animating = false;
	_manager->stop(this);
}

AnimationManager::AnimationManager() : _timer(this), _iterating(false) {
	_timer.setSingleShot(false);
	connect(&_timer, SIGNAL(timeout()), this, SLOT(timeout()));
}

void AnimationManager::start(Animation *obj) {
	if (_iterating) {
		_starting.insert(obj, NullType());
		if (!_stopping.isEmpty()) {
			_stopping.remove(obj);
		}
	} else {
		if (_objects.isEmpty()) {
			_timer.start(AnimationTimerDelta);
		}
		_objects.insert(obj, NullType());
	}
}

void AnimationManager::stop(Animation *obj) {
	if (_iterating) {
		_stopping.insert(obj, NullType());
		if (!_starting.isEmpty()) {
			_starting.insert(obj, NullType());
		}
	} else {
		AnimatingObjects::iterator i = _objects.find(obj);
		if (i != _objects.cend()) {
			_objects.erase(i);
			if (_objects.isEmpty()) {
				_timer.stop();
			}
		}
	}
}

void AnimationManager::timeout() {
	_iterating = true;
	uint64 ms = getms();
	for (AnimatingObjects::const_iterator i = _objects.begin(), e = _objects.end(); i != e; ++i) {
		i.key()->step(ms, true);
	}
	_iterating = false;

	if (!_starting.isEmpty()) {
		for (AnimatingObjects::iterator i = _starting.begin(), e = _starting.end(); i != e; ++i) {
			_objects.insert(i.key(), NullType());
		}
		_starting.clear();
	}
	if (!_stopping.isEmpty()) {
		for (AnimatingObjects::iterator i = _stopping.begin(), e = _stopping.end(); i != e; ++i) {
			_objects.remove(i.key());
		}
		_stopping.clear();
	}
	if (!_objects.size()) {
		_timer.stop();
	}
}

void AnimationManager::clipCallback(ClipReader *reader, qint32 threadIndex, qint32 notification) {
	ClipReader::callback(reader, threadIndex, ClipReaderNotification(notification));
}

QPixmap _prepareFrame(const ClipFrameRequest &request, const QImage &original, bool hasAlpha, QImage &cache) {
	bool badSize = (original.width() != request.framew) || (original.height() != request.frameh);
	bool needOuter = (request.outerw != request.framew) || (request.outerh != request.frameh);
	if (badSize || needOuter || hasAlpha || request.rounded) {
		int32 factor(request.factor);
		bool newcache = (cache.width() != request.outerw || cache.height() != request.outerh);
		if (newcache) {
			cache = QImage(request.outerw, request.outerh, QImage::Format_ARGB32_Premultiplied);
			cache.setDevicePixelRatio(factor);
		}
		{
			Painter p(&cache);
			if (newcache) {
				if (request.framew < request.outerw) {
					p.fillRect(0, 0, (request.outerw - request.framew) / (2 * factor), cache.height() / factor, st::black);
					p.fillRect((request.outerw - request.framew) / (2 * factor) + (request.framew / factor), 0, (cache.width() / factor) - ((request.outerw - request.framew) / (2 * factor) + (request.framew / factor)), cache.height() / factor, st::black);
				}
				if (request.frameh < request.outerh) {
					p.fillRect(qMax(0, (request.outerw - request.framew) / (2 * factor)), 0, qMin(cache.width(), request.framew) / factor, (request.outerh - request.frameh) / (2 * factor), st::black);
					p.fillRect(qMax(0, (request.outerw - request.framew) / (2 * factor)), (request.outerh - request.frameh) / (2 * factor) + (request.frameh / factor), qMin(cache.width(), request.framew) / factor, (cache.height() / factor) - ((request.outerh - request.frameh) / (2 * factor) + (request.frameh / factor)), st::black);
				}
			}
			if (hasAlpha) {
				p.fillRect(qMax(0, (request.outerw - request.framew) / (2 * factor)), qMax(0, (request.outerh - request.frameh) / (2 * factor)), qMin(cache.width(), request.framew) / factor, qMin(cache.height(), request.frameh) / factor, st::white);
			}
			QPoint position((request.outerw - request.framew) / (2 * factor), (request.outerh - request.frameh) / (2 * factor));
			if (badSize) {
				p.setRenderHint(QPainter::SmoothPixmapTransform);
				QRect to(position, QSize(request.framew / factor, request.frameh / factor));
				QRect from(0, 0, original.width(), original.height());
				p.drawImage(to, original, from, Qt::ColorOnly);
			} else {
				p.drawImage(position, original);
			}
		}
		if (request.rounded) {
			imageRound(cache);
		}
		return QPixmap::fromImage(cache, Qt::ColorOnly);
	}
	return QPixmap::fromImage(original, Qt::ColorOnly);
}

ClipReader::ClipReader(const FileLocation &location, const QByteArray &data, Callback::Creator cb)
: _cb(cb)
, _state(ClipReading)
, _width(0)
, _height(0)
, _step(WaitingForDimensionsStep)
, _paused(0)
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

ClipReader::Frame *ClipReader::frameToShow(int32 *index) const { // 0 means not ready
	int32 step = _step.loadAcquire(), i;
	if (step == WaitingForDimensionsStep) {
		if (index) *index = 0;
		return 0;
	} else if (step == WaitingForRequestStep) {
		i = 0;
	} else if (step == WaitingForFirstFrameStep) {
		i = 0;
	} else {
		i = (step / 2) % 3;
	}
	if (index) *index = i;
	return _frames + i;
}

ClipReader::Frame *ClipReader::frameToWrite(int32 *index) const { // 0 means not ready
	int32 step = _step.loadAcquire(), i;
	if (step == WaitingForDimensionsStep) {
		i = 0;
	} else if (step == WaitingForRequestStep) {
		if (index) *index = 0;
		return 0;
	} else if (step == WaitingForFirstFrameStep) {
		i = 0;
	} else {
		i = ((step + 2) / 2) % 3;
	}
	if (index) *index = i;
	return _frames + i;
}

ClipReader::Frame *ClipReader::frameToWriteNext(bool checkNotWriting, int32 *index) const {
	int32 step = _step.loadAcquire(), i;
	if (step == WaitingForDimensionsStep || step == WaitingForRequestStep || (checkNotWriting && (step % 2))) {
		if (index) *index = 0;
		return 0;
	}
	i = ((step + 4) / 2) % 3;
	if (index) *index = i;
	return _frames + i;
}

void ClipReader::moveToNextShow() const {
	int32 step = _step.loadAcquire();
	if (step == WaitingForDimensionsStep) {
	} else if (step == WaitingForRequestStep) {
		_step.storeRelease(WaitingForFirstFrameStep);
	} else if (step == WaitingForFirstFrameStep) {
	} else if (!(step % 2)) {
		_step.storeRelease(step + 1);
	}
}

void ClipReader::moveToNextWrite() const {
	int32 step = _step.loadAcquire();
	if (step == WaitingForDimensionsStep) {
		_step.storeRelease(WaitingForRequestStep);
	} else if (step == WaitingForRequestStep) {
	} else if (step == WaitingForFirstFrameStep) {
		_step.storeRelease(0);
	} else if (step % 2) {
		_step.storeRelease((step + 1) % 6);
	}
}

void ClipReader::callback(ClipReader *reader, int32 threadIndex, ClipReaderNotification notification) {
	// check if reader is not deleted already
	if (_clipManagers.size() > threadIndex && _clipManagers.at(threadIndex)->carries(reader)) {
		reader->_cb.call(notification);
	}
}

void ClipReader::start(int32 framew, int32 frameh, int32 outerw, int32 outerh, bool rounded) {
	if (_clipManagers.size() <= _threadIndex) error();
	if (_state == ClipError) return;

	if (_step.loadAcquire() == WaitingForRequestStep) {
		int32 factor(cIntRetinaFactor());
		ClipFrameRequest request;
		request.factor = factor;
		request.framew = framew * factor;
		request.frameh = frameh * factor;
		request.outerw = outerw * factor;
		request.outerh = outerh * factor;
		request.rounded = rounded;
		_frames[0].request = _frames[1].request = _frames[2].request = request;
		moveToNextShow();
		_clipManagers.at(_threadIndex)->start(this);
	}
}

QPixmap ClipReader::current(int32 framew, int32 frameh, int32 outerw, int32 outerh, uint64 ms) {
	Frame *frame = frameToShow();
	t_assert(frame != 0);

	if (ms) {
		frame->displayed.storeRelease(1);
		if (_paused.loadAcquire()) {
			_paused.storeRelease(0);
			if (_clipManagers.size() <= _threadIndex) error();
			if (_state != ClipError) {
				_clipManagers.at(_threadIndex)->update(this);
			}
		}
	} else {
		frame->displayed.storeRelease(-1); // displayed, but should be paused
	}

	int32 factor(cIntRetinaFactor());
	if (frame->pix.width() == outerw * factor && frame->pix.height() == outerh * factor) {
		moveToNextShow();
		return frame->pix;
	}

	frame->request.framew = framew * factor;
	frame->request.frameh = frameh * factor;
	frame->request.outerw = outerw * factor;
	frame->request.outerh = outerh * factor;

	QImage cacheForResize;
	frame->pix = QPixmap();
	frame->pix = _prepareFrame(frame->request, frame->original, true, cacheForResize);

	Frame *other = frameToWriteNext(true);
	if (other) other->request = frame->request;

	moveToNextShow();

	if (_clipManagers.size() <= _threadIndex) error();
	if (_state != ClipError) {
		_clipManagers.at(_threadIndex)->update(this);
	}

	return frame->pix;
}

bool ClipReader::ready() const {
	if (_width && _height) return true;

	Frame *frame = frameToShow();
	if (frame) {
		_width = frame->original.width();
		_height = frame->original.height();
		return true;
	}
	return false;
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

	ClipReaderImplementation(FileLocation *location, QByteArray *data)
		: _location(location)
		, _data(data)
		, _device(0)
		, _dataSize(0) {
	}
	virtual bool readNextFrame() = 0;
	virtual bool renderFrame(QImage &to, bool &hasAlpha, const QSize &size) = 0;
	virtual int32 nextFrameDelay() = 0;
	virtual bool start(bool onlyGifv) = 0;
	virtual ~ClipReaderImplementation() {
	}
	int64 dataSize() const {
		return _dataSize;
	}

protected:
	FileLocation *_location;
	QByteArray *_data;
	QFile _file;
	QBuffer _buffer;
	QIODevice *_device;
	int64 _dataSize;

	void initDevice() {
		if (_data->isEmpty()) {
			if (_file.isOpen()) _file.close();
			_file.setFileName(_location->name());
			_dataSize = _file.size();
		} else {
			if (_buffer.isOpen()) _buffer.close();
			_buffer.setBuffer(_data);
			_dataSize = _data->size();
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

	bool readNextFrame() {
		if (_reader) _frameDelay = _reader->nextImageDelay();
		if (_framesLeft < 1 && !jumpToStart()) {
			return false;
		}

		_frame = QImage(); // QGifHandler always reads first to internal QImage and returns it
		if (!_reader->read(&_frame) || _frame.isNull()) {
			return false;
		}
		--_framesLeft;
		return true;
	}

	bool renderFrame(QImage &to, bool &hasAlpha, const QSize &size) {
		t_assert(!_frame.isNull());
		if (size.isEmpty() || size == _frame.size()) {
			int32 w = _frame.width(), h = _frame.height();
			if (to.width() == w && to.height() == h && to.format() == _frame.format()) {
				if (to.byteCount() != _frame.byteCount()) {
					int bpl = qMin(to.bytesPerLine(), _frame.bytesPerLine());
					for (int i = 0; i < h; ++i) {
						memcpy(to.scanLine(i), _frame.constScanLine(i), bpl);
					}
				} else {
					memcpy(to.bits(), _frame.constBits(), _frame.byteCount());
				}
			} else {
				to = _frame.copy();
			}
		} else {
			to = _frame.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		}
		hasAlpha = _frame.hasAlphaChannel();
		_frame = QImage();
		return true;
	}

	int32 nextFrameDelay() {
		return _frameDelay;
	}

	bool start(bool onlyGifv) {
		if (onlyGifv) return false;
		return jumpToStart();
	}

	~QtGifReaderImplementation() {
		deleteAndMark(_reader);
	}

private:
	QImageReader *_reader;
	int32 _framesLeft, _frameDelay;
	QImage _frame;

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
		, _frameRead(false)
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

	bool readNextFrame() {
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

	bool renderFrame(QImage &to, bool &hasAlpha, const QSize &size) {
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

	int32 nextFrameDelay() {
		return _currentFrameDelay;
	}

	QString logData() const {
		return qsl("for file '%1', data size '%2'").arg(_location ? _location->name() : QString()).arg(_data->size());
	}

	bool start(bool onlyGifv) {
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

		if (onlyGifv) {
			if (av_find_best_stream(_fmtContext, AVMEDIA_TYPE_AUDIO, -1, -1, 0, 0) >= 0) { // should be no audio stream
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

	int32 duration() const {
		if (_fmtContext->streams[_streamId]->duration == AV_NOPTS_VALUE) return 0;
		return (_fmtContext->streams[_streamId]->duration * _fmtContext->streams[_streamId]->time_base.num) / _fmtContext->streams[_streamId]->time_base.den;
	}

	~FFMpegReaderImplementation() {
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

private:
	uchar *_ioBuffer;
	AVIOContext *_ioContext;
	AVFormatContext *_fmtContext;
	AVCodec *_codec;
	AVCodecContext *_codecContext;
	int32 _streamId;
	AVFrame *_frame;
	bool _opened, _hadFrame, _frameRead;

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
	QSize _swsSize;

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
	, _frame(0)
	, _width(0)
	, _height(0)
	, _nextFrameWhen(0)
	, _paused(false) {
		if (_data.isEmpty() && !_location->accessEnable()) {
			error();
			return;
		}
		_accessed = true;
	}

	ClipProcessResult start(uint64 ms) {
		if (!_implementation && !init()) {
			return error();
		}
		if (frame() && frame()->original.isNull()) {
			if (!_implementation->readNextFrame()) {
				return error();
			}
			if (!_implementation->renderFrame(frame()->original, frame()->alpha, QSize())) {
				return error();
			}
			_width = frame()->original.width();
			_height = frame()->original.height();
			return ClipProcessStarted;
		}
		return ClipProcessWait;
	}

	ClipProcessResult process(uint64 ms) { // -1 - do nothing, 0 - update, 1 - reinit
		if (_state == ClipError) return ClipProcessError;

		if (!_request.valid()) {
			return start(ms);
		}

		if (!_paused && ms >= _nextFrameWhen) {
			return ClipProcessRepaint;
		}
		return ClipProcessWait;
	}

	ClipProcessResult finishProcess(uint64 ms) {
		if (!readNextFrame()) {
			return error();
		}
		if (ms >= _nextFrameWhen && !readNextFrame(true)) {
			return error();
		}
		if (!renderFrame()) {
			return error();
		}
		return ClipProcessCopyFrame;
	}

	uint64 nextFrameDelay() {
		int32 delay = _implementation->nextFrameDelay();
		return qMax(delay, 5);
	}

	bool readNextFrame(bool keepup = false) {
		if (!_implementation->readNextFrame()) {
			return false;
		}
		_nextFrameWhen += nextFrameDelay();
		if (keepup) {
			_nextFrameWhen = qMax(_nextFrameWhen, getms());
		}
		return true;
	}

	bool renderFrame() {
		t_assert(frame() != 0 && _request.valid());
		if (!_implementation->renderFrame(frame()->original, frame()->alpha, QSize(_request.framew, _request.frameh))) {
			return false;
		}
		frame()->original.setDevicePixelRatio(_request.factor);
		frame()->pix = QPixmap();
		frame()->pix = _prepareFrame(_request, frame()->original, frame()->alpha, frame()->cache);
		frame()->when = _nextFrameWhen;
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
		return _implementation->start(false);
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
		deleteAndMark(_location);
		deleteAndMark(_implementation);
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
	struct Frame {
		Frame() : alpha(true), when(0) {
		}
		QPixmap pix;
		QImage original, cache;
		bool alpha;
		uint64 when;
	};
	Frame _frames[3];
	int32 _frame;
	Frame *frame() {
		return _frames + _frame;
	}

	int32 _width, _height;

	uint64 _nextFrameWhen;

	bool _paused;

	friend class ClipReadManager;

};

ClipReadManager::ClipReadManager(QThread *thread) : _processingInThread(0), _needReProcess(false) {
	moveToThread(thread);
	connect(thread, SIGNAL(started()), this, SLOT(process()));
    connect(thread, SIGNAL(finished()), this, SLOT(finish()));
	connect(this, SIGNAL(processDelayed()), this, SLOT(process()), Qt::QueuedConnection);

	_timer.setSingleShot(true);
	_timer.moveToThread(thread);
	connect(&_timer, SIGNAL(timeout()), this, SLOT(process()));

	connect(this, SIGNAL(callback(ClipReader*,qint32,qint32)), _manager, SLOT(clipCallback(ClipReader*,qint32,qint32)));
}

void ClipReadManager::append(ClipReader *reader, const FileLocation &location, const QByteArray &data) {
	reader->_private = new ClipReaderPrivate(reader, location, data);
	_loadLevel.fetchAndAddRelaxed(AverageGifSize);
	update(reader);
}

void ClipReadManager::start(ClipReader *reader) {
	update(reader);
}

void ClipReadManager::update(ClipReader *reader) {
	QReadLocker lock(&_readerPointersMutex);
	ReaderPointers::const_iterator i = _readerPointers.constFind(reader);
	if (i == _readerPointers.cend()) {
		lock.unlock();

		QWriteLocker lock(&_readerPointersMutex);
		_readerPointers.insert(reader, MutableAtomicInt(1));
	} else {
		i->v.storeRelease(1);
	}
	emit processDelayed();
}

void ClipReadManager::stop(ClipReader *reader) {
	if (!carries(reader)) return;

	QWriteLocker lock(&_readerPointersMutex);
	_readerPointers.remove(reader);
	emit processDelayed();
}

bool ClipReadManager::carries(ClipReader *reader) const {
	QReadLocker lock(&_readerPointersMutex);
	return _readerPointers.contains(reader);
}

ClipReadManager::ReaderPointers::iterator ClipReadManager::unsafeFindReaderPointer(ClipReaderPrivate *reader) {
	ReaderPointers::iterator it = _readerPointers.find(reader->_interface);

	// could be a new reader which was realloced in the same address
	return (it == _readerPointers.cend() || it.key()->_private == reader) ? it : _readerPointers.end();
}

ClipReadManager::ReaderPointers::const_iterator ClipReadManager::constUnsafeFindReaderPointer(ClipReaderPrivate *reader) const {
	ReaderPointers::const_iterator it = _readerPointers.constFind(reader->_interface);

	// could be a new reader which was realloced in the same address
	return (it == _readerPointers.cend() || it.key()->_private == reader) ? it : _readerPointers.cend();
}

bool ClipReadManager::handleProcessResult(ClipReaderPrivate *reader, ClipProcessResult result, uint64 ms) {
	QReadLocker lock(&_readerPointersMutex);
	ReaderPointers::const_iterator it = constUnsafeFindReaderPointer(reader);
	if (result == ClipProcessError) {
		if (it != _readerPointers.cend()) {
			it.key()->error();
			emit callback(it.key(), it.key()->threadIndex(), ClipReaderReinit);

			lock.unlock();
			QWriteLocker lock(&_readerPointersMutex);
			ReaderPointers::iterator i = unsafeFindReaderPointer(reader);
			if (i != _readerPointers.cend()) _readerPointers.erase(i);
		}
		return false;
	}
	if (it == _readerPointers.cend()) {
		return false;
	}

	if (result == ClipProcessStarted) {
		_loadLevel.fetchAndAddRelaxed(reader->_width * reader->_height - AverageGifSize);
	}
	if (!reader->_paused && result == ClipProcessRepaint) {
		int32 ishowing, iprevious;
		ClipReader::Frame *showing = it.key()->frameToShow(&ishowing), *previous = it.key()->frameToWriteNext(false, &iprevious);
		t_assert(previous != 0 && showing != 0 && ishowing >= 0 && iprevious >= 0);
		if (reader->_frames[ishowing].when > 0 && showing->displayed.loadAcquire() <= 0) { // current frame was not shown
			if (reader->_frames[ishowing].when + WaitBeforeGifPause < ms || (reader->_frames[iprevious].when && previous->displayed.loadAcquire() <= 0)) {
				reader->_paused = true;
				it.key()->_paused.storeRelease(1);
				result = ClipProcessPaused;
			}
		}
	}
	if (result == ClipProcessStarted || result == ClipProcessCopyFrame) {
		t_assert(reader->_frame >= 0);
		ClipReader::Frame *frame = it.key()->_frames + reader->_frame;
		frame->clear();
		frame->pix = reader->frame()->pix;
		frame->original = reader->frame()->original;
		frame->displayed.storeRelease(0);
		if (result == ClipProcessStarted) {
			reader->_nextFrameWhen = ms;
			it.key()->moveToNextWrite();
			emit callback(it.key(), it.key()->threadIndex(), ClipReaderReinit);
		}
	} else if (result == ClipProcessPaused) {
		it.key()->moveToNextWrite();
		emit callback(it.key(), it.key()->threadIndex(), ClipReaderReinit);
	} else if (result == ClipProcessRepaint) {
		it.key()->moveToNextWrite();
		emit callback(it.key(), it.key()->threadIndex(), ClipReaderRepaint);
	}
	return true;
}

ClipReadManager::ResultHandleState ClipReadManager::handleResult(ClipReaderPrivate *reader, ClipProcessResult result, uint64 ms) {
	if (!handleProcessResult(reader, result, ms)) {
		_loadLevel.fetchAndAddRelaxed(-1 * (reader->_width > 0 ? reader->_width * reader->_height : AverageGifSize));
		delete reader;
		return ResultHandleRemove;
	}

	_processingInThread->eventDispatcher()->processEvents(QEventLoop::AllEvents);
	if (_processingInThread->isInterruptionRequested()) {
		return ResultHandleStop;
	}

	if (result == ClipProcessRepaint) {
		{
			QReadLocker lock(&_readerPointersMutex);
			ReaderPointers::const_iterator it = constUnsafeFindReaderPointer(reader);
			if (it != _readerPointers.cend()) {
				int32 index = 0;
				ClipReader *r = it.key();
				ClipReader::Frame *frame = it.key()->frameToWrite(&index);
				if (frame) {
					frame->clear();
				} else {
					t_assert(!reader->_request.valid());
				}
				reader->_frame = index;
			}
		}
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
		QReadLocker lock(&_readerPointersMutex);
		for (ReaderPointers::iterator it = _readerPointers.begin(), e = _readerPointers.end(); it != e; ++it) {
			if (it->v.loadAcquire()) {
				Readers::iterator i = _readers.find(it.key()->_private);
				if (i == _readers.cend()) {
					_readers.insert(it.key()->_private, 0);
				} else {
					i.value() = ms;
					if (i.key()->_paused && !it.key()->_paused.loadAcquire()) {
						i.key()->_paused = false;
					}
				}
				ClipReader::Frame *frame = it.key()->frameToWrite();
				if (frame) it.key()->_private->_request = frame->request;
				it->v.storeRelease(0);
			}
		}
	}

	for (Readers::iterator i = _readers.begin(), e = _readers.end(); i != e;) {
		ClipReaderPrivate *reader = i.key();
		if (i.value() <= ms) {
			ResultHandleState state = handleResult(reader, reader->process(ms), ms);
			if (state == ResultHandleRemove) {
				i = _readers.erase(i);
				continue;
			} else if (state == ResultHandleStop) {
				_processingInThread = 0;
				return;
			}
			ms = getms();
			i.value() = reader->_nextFrameWhen ? reader->_nextFrameWhen : (ms + 86400 * 1000ULL);
		}
		if (!reader->_paused && i.value() < minms) {
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

void ClipReadManager::finish() 	{
    _timer.stop();
    clear();
}

void ClipReadManager::clear() {
	{
		QWriteLocker lock(&_readerPointersMutex);
		for (ReaderPointers::iterator it = _readerPointers.begin(), e = _readerPointers.end(); it != e; ++it) {
			it.key()->_private = 0;
		}
		_readerPointers.clear();
	}

    for (Readers::iterator i = _readers.begin(), e = _readers.end(); i != e; ++i) {
        delete i.key();
    }
    _readers.clear();
}

ClipReadManager::~ClipReadManager() {
    clear();
}

MTPDocumentAttribute clipReadAnimatedAttributes(const QString &fname, const QByteArray &data, QImage &cover) {
	FileLocation localloc(StorageFilePartial, fname);
	QByteArray localdata(data);

	FFMpegReaderImplementation *reader = new FFMpegReaderImplementation(&localloc, &localdata);
	if (reader->start(true)) {
		bool hasAlpha = false;
		if (reader->readNextFrame() && reader->renderFrame(cover, hasAlpha, QSize())) {
			if (cover.width() > 0 && cover.height() > 0 && cover.width() < cover.height() * 10 && cover.height() < cover.width() * 10) {
				if (hasAlpha) {
					QImage cacheForResize;
					ClipFrameRequest request;
					request.framew = request.outerw = cover.width();
					request.frameh = request.outerh = cover.height();
					request.factor = 1;
					cover = _prepareFrame(request, cover, hasAlpha, cacheForResize).toImage();
				}
				int32 duration = reader->duration();
				delete reader;
				return MTP_documentAttributeVideo(MTP_int(duration), MTP_int(cover.width()), MTP_int(cover.height()));
			}
		}
	}
	delete reader;
	return MTP_documentAttributeFilename(MTP_string(fname));
}
