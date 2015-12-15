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
	const GifItems &items(App::gifItems());
	GifItems::const_iterator it = items.constFind(reader);
	if (it != items.cend()) {
		it.value()->initDimensions();
		if (App::main()) emit App::main()->itemResized(it.value(), true);
	}
}

void AnimationManager::clipRedraw(ClipReader *reader) {
	if (reader->currentDisplayed()) {
		return;
	}

	const GifItems &items(App::gifItems());
	GifItems::const_iterator it = items.constFind(reader);
	if (it != items.cend()) {
		Ui::redrawHistoryItem(it.value());
	}
}

void AnimatedGif::step_frame(float64 ms, bool timer) {
	int32 f = frame;
	while (f < images.size() && ms > delays[f]) {
		++f;
		if (f == images.size() && images.size() < framesCount) {
			if (reader->read(&img)) {
				int64 d = reader->nextImageDelay(), delay = delays[f - 1];
				if (!d) d = 1;
				delay += d;
				if (img.size() != QSize(w, h)) img = img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
				images.push_back(img);
				frames.push_back(QPixmap());
				delays.push_back(delay);
				for (int32 i = 0; i < images.size(); ++i) {
					if (!images[i].isNull() || !frames[i].isNull()) {
						images[i] = QImage();
						frames[i] = QPixmap();
						break;
					}
				}
			} else {
				framesCount = images.size();
			}
		}
		if (f == images.size()) {
			if (!duration) {
				duration = delays.isEmpty() ? 1 : delays.back();
			}

			f = 0;
			for (int32 i = 0, s = delays.size() - 1; i <= s; ++i) {
				delays[i] += duration;
			}
			if (images[f].isNull()) {
				QString fname = reader->fileName();
				delete reader;
				reader = new QImageReader(fname);
			}
		}
		if (images[f].isNull() && reader->read(&img)) {
			if (img.size() != QSize(w, h)) img = img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
			images[f] = img;
			frames[f] = QPixmap();
			for (int32 i = 0; i < f; ++i) {
				if (!images[i].isNull() || !frames[i].isNull()) {
					images[i] = QImage();
					frames[i] = QPixmap();
					break;
				}
			}
		}
	}
	if (frame != f) {
		frame = f;
		if (timer) {
			if (msg) {
				Ui::redrawHistoryItem(msg);
			} else {
				emit updated();
			}
		}
	}
}

void AnimatedGif::start(HistoryItem *row, const FileLocation &f) {
	stop();

	file = new FileLocation(f);
	if (!file->accessEnable()) {
		stop();
		return;
	}
	access = true;

	reader = new QImageReader(file->name());
	if (!reader->canRead() || !reader->supportsAnimation()) {
		stop();
		return;
	}

	QSize s = reader->size();
	w = s.width();
	h = s.height();
	framesCount = reader->imageCount();
	if (!w || !h || !framesCount) {
		stop();
		return;
	}

	frames.reserve(framesCount);
	images.reserve(framesCount);
	delays.reserve(framesCount);

	int32 sizeLeft = MediaViewImageSizeLimit, delay = 0;
	for (bool read = reader->read(&img); read; read = reader->read(&img)) {
		sizeLeft -= w * h * 4;
		if (img.size() != s) img = img.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		images.push_back(img);
		frames.push_back(QPixmap());
		int32 d = reader->nextImageDelay();
		if (!d) d = 1;
		delay += d;
		delays.push_back(delay);
		if (sizeLeft < 0) break;
	}

	msg = row;

	_a_frames.start();
	if (msg) {
		msg->initDimensions();
		if (App::main()) App::main()->itemResized(msg, true);
	}
}

void AnimatedGif::stop(bool onItemRemoved) {
	if (file) {
		if (access) {
			file->accessDisable();
		}
		delete file;
		file = 0;
	}
	access = false;

	if (isNull()) return;

	delete reader;
	reader = 0;
	HistoryItem *row = msg;
	msg = 0;
	frames.clear();
	images.clear();
	delays.clear();
	w = h = frame = framesCount = duration = 0;

	_a_frames.stop();
	if (row && !onItemRemoved) {
		row->initDimensions();
		if (App::main()) App::main()->itemResized(row, true);
	}
}

const QPixmap &AnimatedGif::current(int32 width, int32 height, bool rounded) {
	if (!width) width = w;
	if (!height) height = h;
	if ((frames[frame].isNull() || frames[frame].width() != width || frames[frame].height() != height) && !images[frame].isNull()) {
		QImage img = images[frame];
		if (img.width() != width || img.height() != height) img = img.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		if (rounded) imageRound(img);
		frames[frame] = QPixmap::fromImage(img, Qt::ColorOnly);
		frames[frame].setDevicePixelRatio(cRetinaFactor());
	}
	return frames[frame];
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
, _private(0) {
	if (_clipThreads.size() < ClipThreadsCount) {
		_threadIndex = _clipThreads.size();
		_clipThreads.push_back(new QThread());
		_clipManagers.push_back(new ClipReadManager(_clipThreads.back()));
		_clipThreads.back()->start();
	} else {
		_threadIndex = rand() % _clipThreads.size();
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

QPixmap ClipReader::current(int32 framew, int32 frameh, int32 outerw, int32 outerh) {
	_currentDisplayed.storeRelease(1);

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

class ClipReaderPrivate {
public:

	ClipReaderPrivate(ClipReader *reader, const FileLocation &location, const QByteArray &data) : _interface(reader)
	, _state(ClipReading)
	, _data(data)
	, _location(_data.isEmpty() ? new FileLocation(location) : 0)
	, _accessed(false)
	, _buffer(_data.isEmpty() ? 0 : &_data)
	, _reader(0)
	, _currentMs(0)
	, _nextUpdateMs(0) {

		if (_data.isEmpty() && !_location->accessEnable()) {
			error();
			return;
		}
		_accessed = true;
	}

	ClipProcessResult start(uint64 ms) {
		_nextUpdateMs = ms + 86400 * 1000ULL;
		if (!_reader && !restartReader(true)) {
			return error();
		}
		if (_currentOriginal.isNull()) {
			if (!readNextFrame(_currentOriginal)) {
				return error();
			}
			--_framesLeft;
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

			_currentMs = ms;
			_current = _prepareFrame(_request, _currentOriginal, _currentCache, true);

			if (!prepareNextFrame()) {
				return error();
			}
			return ClipProcessStarted;
		} else if (ms >= _nextUpdateMs) {
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
		return qMax(_reader->nextImageDelay(), 5);
	}

	void swapBuffers(uint64 ms = 0) {
		_currentMs = qMax(ms, _nextUpdateMs);
		qSwap(_currentOriginal, _nextOriginal);
		qSwap(_current, _next);
		qSwap(_currentCache, _nextCache);
	}

	bool readNextFrame(QImage &to) {
		QImage frame; // QGifHandler always reads first to internal QImage and returns it
		if (!_reader->read(&frame)) {
			return false;
		}
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

	bool prepareNextFrame() {
		_nextUpdateMs = _currentMs + nextFrameDelay();
		if (!_framesLeft) {
			if (_reader->jumpToImage(0)) {
				_framesLeft = _reader->imageCount();
			} else if (!restartReader()) {
				return false;
			}
		}
		if (!readNextFrame(_nextOriginal)) {
			return false;
		}
		_nextOriginal.setDevicePixelRatio(_request.factor);
		--_framesLeft;
		_next = QPixmap();
		_next = _prepareFrame(_request, _nextOriginal, _nextCache, true);
		return true;
	}

	bool restartReader(bool first = false) {
		if (first && _data.isEmpty() && QFileInfo(_location->name()).size() <= AnimationInMemory) {
			QFile f(_location->name());
			if (f.open(QIODevice::ReadOnly)) {
				_data = f.readAll();
				if (f.error() == QFile::NoError) {
					_buffer.setBuffer(&_data);
				} else {
					_data = QByteArray();
				}
			}
		} else if (!_data.isEmpty()) {
			_buffer.close();
		}
		delete _reader;

		if (_data.isEmpty()) {
			_reader = new QImageReader(_location->name());
		} else {
			_reader = new QImageReader(&_buffer);
		}
		if (!_reader->canRead() || !_reader->supportsAnimation()) {
			return false;
		}
		_framesLeft = _reader->imageCount();
		if (_framesLeft < 1) {
			return false;
		}
		return true;
	}

	ClipProcessResult error() {
		stop();
		_state = ClipError;
		return ClipProcessError;
	}

	void stop() {
		delete _reader;
		_reader = 0;

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
		setBadPointer(_reader);
	}

private:

	ClipReader *_interface;
	ClipState _state;

	QByteArray _data;
	FileLocation *_location;
	bool _accessed;

	QBuffer _buffer;
	QImageReader *_reader;

	ClipFrameRequest _request;
	QPixmap _current, _next;
	QImage _currentOriginal, _nextOriginal, _currentCache, _nextCache;

	int32 _framesLeft;
	uint64 _currentMs, _nextUpdateMs;

	friend class ClipReadManager;

};

ClipReadManager::ClipReadManager(QThread *thread) : _processingInThread(0) {
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

bool ClipReadManager::handleProcessResult(ClipReaderPrivate *reader, ClipProcessResult result) {
	QMutexLocker lock(&_readerPointersMutex);
	ReaderPointers::iterator it = _readerPointers.find(reader->_interface);
	if (result == ClipProcessError) {
		if (it != _readerPointers.cend()) {
			it.key()->error();
			_readerPointers.erase(it);
			it = _readerPointers.end();
		}
	}
	if (it == _readerPointers.cend()) {
		return false;
	}

	if (result == ClipProcessReinit || result == ClipProcessRedraw || result == ClipProcessStarted) {
		it.key()->_current = reader->_current;
		it.key()->_currentOriginal = reader->_currentOriginal;
		it.key()->_currentDisplayed.storeRelease(0);
		if (result == ClipProcessReinit) {
			emit reinit(it.key());
		} else if (result == ClipProcessRedraw) {
			emit redraw(it.key());
		}
	}
	return true;
}

ClipReadManager::ResultHandleState ClipReadManager::handleResult(ClipReaderPrivate *reader, ClipProcessResult result, uint64 ms) {
	if (!handleProcessResult(reader, result)) {
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
	if (_processingInThread) return;

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
		}
		if (i.value() < minms) {
			minms = i.value();
		}
		++i;
	}

	ms = getms();
	if (minms <= ms) {
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
