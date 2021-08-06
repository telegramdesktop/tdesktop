/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/clip/media_clip_reader.h"

#include "media/clip/media_clip_ffmpeg.h"
#include "media/clip/media_clip_check_streaming.h"
#include "core/file_location.h"
#include "base/openssl_help.h"
#include "base/invoke_queued.h"
#include "logs.h"

#include <QtCore/QBuffer>
#include <QtCore/QAbstractEventDispatcher>
#include <QtCore/QCoreApplication>
#include <QtCore/QThread>
#include <QtCore/QFileInfo>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
} // extern "C"

namespace Media {
namespace Clip {
namespace {

constexpr auto kClipThreadsCount = 8;
constexpr auto kAverageGifSize = 320 * 240;
constexpr auto kWaitBeforeGifPause = crl::time(200);

QVector<QThread*> threads;
QVector<Manager*> managers;

QImage PrepareFrameImage(const FrameRequest &request, const QImage &original, bool hasAlpha, QImage &cache) {
	auto needResize = (original.width() != request.framew) || (original.height() != request.frameh);
	auto needOuterFill = (request.outerw != request.framew) || (request.outerh != request.frameh);
	auto needRounding = (request.radius != ImageRoundRadius::None);
	if (!needResize && !needOuterFill && !hasAlpha && !needRounding) {
		return original;
	}

	auto factor = request.factor;
	auto needNewCache = (cache.width() != request.outerw || cache.height() != request.outerh);
	if (needNewCache) {
		cache = QImage(request.outerw, request.outerh, QImage::Format_ARGB32_Premultiplied);
		cache.setDevicePixelRatio(factor);
	}
	{
		Painter p(&cache);
		if (needNewCache) {
			if (request.framew < request.outerw) {
				p.fillRect(0, 0, (request.outerw - request.framew) / (2 * factor), cache.height() / factor, st::imageBg);
				p.fillRect((request.outerw - request.framew) / (2 * factor) + (request.framew / factor), 0, (cache.width() / factor) - ((request.outerw - request.framew) / (2 * factor) + (request.framew / factor)), cache.height() / factor, st::imageBg);
			}
			if (request.frameh < request.outerh) {
				p.fillRect(qMax(0, (request.outerw - request.framew) / (2 * factor)), 0, qMin(cache.width(), request.framew) / factor, (request.outerh - request.frameh) / (2 * factor), st::imageBg);
				p.fillRect(qMax(0, (request.outerw - request.framew) / (2 * factor)), (request.outerh - request.frameh) / (2 * factor) + (request.frameh / factor), qMin(cache.width(), request.framew) / factor, (cache.height() / factor) - ((request.outerh - request.frameh) / (2 * factor) + (request.frameh / factor)), st::imageBg);
			}
		}
		if (hasAlpha) {
			p.fillRect(qMax(0, (request.outerw - request.framew) / (2 * factor)), qMax(0, (request.outerh - request.frameh) / (2 * factor)), qMin(cache.width(), request.framew) / factor, qMin(cache.height(), request.frameh) / factor, st::imageBgTransparent);
		}
		auto position = QPoint((request.outerw - request.framew) / (2 * factor), (request.outerh - request.frameh) / (2 * factor));
		if (needResize) {
			PainterHighQualityEnabler hq(p);

			auto dst = QRect(position, QSize(request.framew / factor, request.frameh / factor));
			auto src = QRect(0, 0, original.width(), original.height());
			p.drawImage(dst, original, src, Qt::ColorOnly);
		} else {
			p.drawImage(position, original);
		}
	}
	if (needRounding) {
		Images::prepareRound(cache, request.radius, request.corners);
	}
	return cache;
}

QPixmap PrepareFrame(const FrameRequest &request, const QImage &original, bool hasAlpha, QImage &cache) {
	return QPixmap::fromImage(PrepareFrameImage(request, original, hasAlpha, cache), Qt::ColorOnly);
}

} // namespace

Reader::Reader(
	const Core::FileLocation &location,
	const QByteArray &data,
	Callback &&callback)
: _callback(std::move(callback)) {
	init(location, data);
}

Reader::Reader(const QString &filepath, Callback &&callback)
: _callback(std::move(callback)) {
	init(Core::FileLocation(filepath), QByteArray());
}

Reader::Reader(const QByteArray &data, Callback &&callback)
: _callback(std::move(callback)) {
	init(Core::FileLocation(QString()), data);
}

void Reader::init(const Core::FileLocation &location, const QByteArray &data) {
	if (threads.size() < kClipThreadsCount) {
		_threadIndex = threads.size();
		threads.push_back(new QThread());
		managers.push_back(new Manager(threads.back()));
		threads.back()->start();
	} else {
		_threadIndex = int32(openssl::RandomValue<uint32>() % threads.size());
		int32 loadLevel = 0x7FFFFFFF;
		for (int32 i = 0, l = threads.size(); i < l; ++i) {
			int32 level = managers.at(i)->loadLevel();
			if (level < loadLevel) {
				_threadIndex = i;
				loadLevel = level;
			}
		}
	}
	managers.at(_threadIndex)->append(this, location, data);
}

Reader::Frame *Reader::frameToShow(int32 *index) const { // 0 means not ready
	int step = _step.loadAcquire(), i;
	if (step == WaitingForDimensionsStep) {
		if (index) *index = 0;
		return nullptr;
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

Reader::Frame *Reader::frameToWrite(int32 *index) const { // 0 means not ready
	int32 step = _step.loadAcquire(), i;
	if (step == WaitingForDimensionsStep) {
		i = 0;
	} else if (step == WaitingForRequestStep) {
		if (index) *index = 0;
		return nullptr;
	} else if (step == WaitingForFirstFrameStep) {
		i = 0;
	} else {
		i = ((step + 2) / 2) % 3;
	}
	if (index) *index = i;
	return _frames + i;
}

Reader::Frame *Reader::frameToWriteNext(bool checkNotWriting, int32 *index) const {
	int32 step = _step.loadAcquire(), i;
	if (step == WaitingForDimensionsStep || step == WaitingForRequestStep || (checkNotWriting && (step % 2))) {
		if (index) *index = 0;
		return nullptr;
	}
	i = ((step + 4) / 2) % 3;
	if (index) *index = i;
	return _frames + i;
}

void Reader::moveToNextShow() const {
	int32 step = _step.loadAcquire();
	if (step == WaitingForDimensionsStep) {
	} else if (step == WaitingForRequestStep) {
		_step.storeRelease(WaitingForFirstFrameStep);
	} else if (step == WaitingForFirstFrameStep) {
	} else if (!(step % 2)) {
		_step.storeRelease(step + 1);
	}
}

void Reader::moveToNextWrite() const {
	int32 step = _step.loadAcquire();
	if (step == WaitingForDimensionsStep) {
		_step.storeRelease(WaitingForRequestStep);
	} else if (step == WaitingForRequestStep) {
	} else if (step == WaitingForFirstFrameStep) {
		_step.storeRelease(0);

		// Force paint the first frame so moveToNextShow() is called.
		_frames[0].displayed.storeRelease(0);
	} else if (step % 2) {
		_step.storeRelease((step + 1) % 6);
	}
}

void Reader::callback(Reader *reader, qint32 threadIndex, qint32 notification) {
	// Check if reader is not deleted already
	if (managers.size() > threadIndex && managers.at(threadIndex)->carries(reader) && reader->_callback) {
		reader->_callback(Notification(notification));
	}
}

void Reader::start(int32 framew, int32 frameh, int32 outerw, int32 outerh, ImageRoundRadius radius, RectParts corners) {
	if (managers.size() <= _threadIndex) error();
	if (_state == State::Error) return;

	if (_step.loadAcquire() == WaitingForRequestStep) {
		int factor = style::DevicePixelRatio();
		FrameRequest request;
		request.factor = factor;
		request.framew = framew * factor;
		request.frameh = frameh * factor;
		request.outerw = outerw * factor;
		request.outerh = outerh * factor;
		request.radius = radius;
		request.corners = corners;
		_frames[0].request = _frames[1].request = _frames[2].request = request;
		moveToNextShow();
		managers.at(_threadIndex)->start(this);
	}
}

QPixmap Reader::current(int32 framew, int32 frameh, int32 outerw, int32 outerh, ImageRoundRadius radius, RectParts corners, crl::time ms) {
	Expects(outerw > 0);
	Expects(outerh > 0);

	auto frame = frameToShow();
	Assert(frame != nullptr);

	auto shouldBePaused = !ms;
	if (!shouldBePaused) {
		frame->displayed.storeRelease(1);
		if (_autoPausedGif.loadAcquire()) {
			_autoPausedGif.storeRelease(0);
			if (managers.size() <= _threadIndex) error();
			if (_state != State::Error) {
				managers.at(_threadIndex)->update(this);
			}
		}
	} else {
		frame->displayed.storeRelease(-1);
	}

	auto factor = style::DevicePixelRatio();
	if (frame->pix.width() == outerw * factor
		&& frame->pix.height() == outerh * factor
		&& frame->request.radius == radius
		&& frame->request.corners == corners) {
		moveToNextShow();
		return frame->pix;
	}

	frame->request.framew = framew * factor;
	frame->request.frameh = frameh * factor;
	frame->request.outerw = outerw * factor;
	frame->request.outerh = outerh * factor;

	QImage cacheForResize;
	frame->original.setDevicePixelRatio(factor);
	frame->pix = QPixmap();
	frame->pix = PrepareFrame(frame->request, frame->original, true, cacheForResize);

	auto other = frameToWriteNext(true);
	if (other) other->request = frame->request;

	moveToNextShow();

	if (managers.size() <= _threadIndex) error();
	if (_state != State::Error) {
		managers.at(_threadIndex)->update(this);
	}

	return frame->pix;
}

bool Reader::ready() const {
	if (_width && _height) return true;

	auto frame = frameToShow();
	if (frame) {
		_width = frame->original.width();
		_height = frame->original.height();
		return true;
	}
	return false;
}

crl::time Reader::getPositionMs() const {
	if (auto frame = frameToShow()) {
		return frame->positionMs;
	}
	return 0;
}

crl::time Reader::getDurationMs() const {
	return ready() ? _durationMs : 0;
}

void Reader::pauseResumeVideo() {
	if (managers.size() <= _threadIndex) error();
	if (_state == State::Error) return;

	_videoPauseRequest.storeRelease(1 - _videoPauseRequest.loadAcquire());
	managers.at(_threadIndex)->start(this);
}

bool Reader::videoPaused() const {
	return _videoPauseRequest.loadAcquire() != 0;
}

int32 Reader::width() const {
	return _width;
}

int32 Reader::height() const {
	return _height;
}

State Reader::state() const {
	return _state;
}

void Reader::stop() {
	if (managers.size() <= _threadIndex) error();
	if (_state != State::Error) {
		managers.at(_threadIndex)->stop(this);
		_width = _height = 0;
	}
}

void Reader::error() {
	_state = State::Error;
	_private = nullptr;
}

void Reader::finished() {
	_state = State::Finished;
	_private = nullptr;
}

Reader::~Reader() {
	stop();
}

class ReaderPrivate {
public:
	ReaderPrivate(Reader *reader, const Core::FileLocation &location, const QByteArray &data)
	: _interface(reader)
	, _data(data) {
		if (_data.isEmpty()) {
			_location = std::make_unique<Core::FileLocation>(location);
			if (!_location->accessEnable()) {
				error();
				return;
			}
		}
		_accessed = true;
	}

	ProcessResult start(crl::time ms) {
		if (!_implementation && !init()) {
			return error();
		}
		if (frame()->original.isNull()) {
			auto readResult = _implementation->readFramesTill(-1, ms);
			if (readResult == internal::ReaderImplementation::ReadResult::EndOfFile && _seekPositionMs > 0) {
				// If seek was done to the end: try to read the first frame,
				// get the frame size and return a black frame with that size.

				auto firstFramePositionMs = crl::time(0);
				auto reader = std::make_unique<internal::FFMpegReaderImplementation>(_location.get(), &_data);
				if (reader->start(internal::ReaderImplementation::Mode::Silent, firstFramePositionMs)) {
					auto firstFrameReadResult = reader->readFramesTill(-1, ms);
					if (firstFrameReadResult == internal::ReaderImplementation::ReadResult::Success) {
						if (reader->renderFrame(frame()->original, frame()->alpha, QSize())) {
							frame()->original.fill(QColor(0, 0, 0));

							frame()->positionMs = _seekPositionMs;

							_width = frame()->original.width();
							_height = frame()->original.height();
							_durationMs = _implementation->durationMs();
							return ProcessResult::Started;
						}
					}
				}

				return error();
			} else if (readResult != internal::ReaderImplementation::ReadResult::Success) { // Read the first frame.
				return error();
			}
			if (!_implementation->renderFrame(frame()->original, frame()->alpha, QSize())) {
				return error();
			}
			frame()->positionMs = _implementation->frameRealTime();

			_width = frame()->original.width();
			_height = frame()->original.height();
			_durationMs = _implementation->durationMs();
			return ProcessResult::Started;
		}
		return ProcessResult::Wait;
	}

	ProcessResult process(crl::time ms) { // -1 - do nothing, 0 - update, 1 - reinit
		if (_state == State::Error) {
			return ProcessResult::Error;
		} else if (_state == State::Finished) {
			return ProcessResult::Finished;
		}

		if (!_request.valid()) {
			return start(ms);
		}
		if (!_started) {
			_started = true;
		}

		if (!_autoPausedGif && !_videoPausedAtMs && ms >= _nextFrameWhen) {
			return ProcessResult::Repaint;
		}
		return ProcessResult::Wait;
	}

	ProcessResult finishProcess(crl::time ms) {
		auto frameMs = _seekPositionMs + ms - _animationStarted;
		auto readResult = _implementation->readFramesTill(frameMs, ms);
		if (readResult == internal::ReaderImplementation::ReadResult::EndOfFile) {
			stop();
			_state = State::Finished;
			return ProcessResult::Finished;
		} else if (readResult == internal::ReaderImplementation::ReadResult::Error) {
			return error();
		}
		_nextFramePositionMs = _implementation->frameRealTime();
		_nextFrameWhen = _animationStarted + _implementation->framePresentationTime();
		if (_nextFrameWhen > _seekPositionMs) {
			_nextFrameWhen -= _seekPositionMs;
		} else {
			_nextFrameWhen = 1;
		}

		if (!renderFrame()) {
			return error();
		}
		return ProcessResult::CopyFrame;
	}

	bool renderFrame() {
		Expects(_request.valid());

		if (!_implementation->renderFrame(frame()->original, frame()->alpha, QSize(_request.framew, _request.frameh))) {
			return false;
		}
		frame()->original.setDevicePixelRatio(_request.factor);
		frame()->pix = QPixmap();
		frame()->pix = PrepareFrame(_request, frame()->original, frame()->alpha, frame()->cache);
		frame()->when = _nextFrameWhen;
		frame()->positionMs = _nextFramePositionMs;
		return true;
	}

	bool init() {
		if (_data.isEmpty() && QFileInfo(_location->name()).size() <= internal::kMaxInMemory) {
			QFile f(_location->name());
			if (f.open(QIODevice::ReadOnly)) {
				_data = f.readAll();
				if (f.error() != QFile::NoError) {
					_data = QByteArray();
				}
			}
		}

		_implementation = std::make_unique<internal::FFMpegReaderImplementation>(_location.get(), &_data);

		return _implementation->start(internal::ReaderImplementation::Mode::Silent, _seekPositionMs);
	}

	void startedAt(crl::time ms) {
		_animationStarted = _nextFrameWhen = ms;
	}

	void pauseVideo(crl::time ms) {
		if (_videoPausedAtMs) return; // Paused already.

		_videoPausedAtMs = ms;
	}

	void resumeVideo(crl::time ms) {
		if (!_videoPausedAtMs) return; // Not paused.

		auto delta = ms - _videoPausedAtMs;
		_animationStarted += delta;
		_nextFrameWhen += delta;

		_videoPausedAtMs = 0;
	}

	ProcessResult error() {
		stop();
		_state = State::Error;
		return ProcessResult::Error;
	}

	void stop() {
		_implementation = nullptr;
		if (_location) {
			if (_accessed) {
				_location->accessDisable();
			}
			_location = nullptr;
		}
		_accessed = false;
	}

	~ReaderPrivate() {
		stop();
		_data.clear();
	}

private:
	Reader *_interface;
	State _state = State::Reading;
	crl::time _seekPositionMs = 0;

	QByteArray _data;
	std::unique_ptr<Core::FileLocation> _location;
	bool _accessed = false;

	QBuffer _buffer;
	std::unique_ptr<internal::ReaderImplementation> _implementation;

	FrameRequest _request;
	struct Frame {
		QPixmap pix;
		QImage original, cache;
		bool alpha = true;
		crl::time when = 0;

		// Counted from the end, so that positionMs <= durationMs despite keep up delays.
		crl::time positionMs = 0;
	};
	Frame _frames[3];
	int _frame = 0;
	not_null<Frame*> frame() {
		return _frames + _frame;
	}

	int _width = 0;
	int _height = 0;

	crl::time _durationMs = 0;
	crl::time _animationStarted = 0;
	crl::time _nextFrameWhen = 0;
	crl::time _nextFramePositionMs = 0;

	bool _autoPausedGif = false;
	bool _started = false;
	crl::time _videoPausedAtMs = 0;

	friend class Manager;

};

Manager::Manager(QThread *thread) {
	moveToThread(thread);
	connect(thread, &QThread::started, this, [=] { process(); });
	connect(thread, &QThread::finished, this, [=] { finish(); });

	_timer.setSingleShot(true);
	_timer.moveToThread(thread);
	connect(&_timer, &QTimer::timeout, this, [=] { process(); });
}

void Manager::append(Reader *reader, const Core::FileLocation &location, const QByteArray &data) {
	reader->_private = new ReaderPrivate(reader, location, data);
	_loadLevel.fetchAndAddRelaxed(kAverageGifSize);
	update(reader);
}

void Manager::start(Reader *reader) {
	update(reader);
}

void Manager::update(Reader *reader) {
	QMutexLocker lock(&_readerPointersMutex);
	auto i = _readerPointers.find(reader);
	if (i == _readerPointers.cend()) {
		_readerPointers.insert(reader, QAtomicInt(1));
	} else {
		i->storeRelease(1);
	}
	InvokeQueued(this, [=] { process(); });
}

void Manager::stop(Reader *reader) {
	if (!carries(reader)) return;

	QMutexLocker lock(&_readerPointersMutex);
	_readerPointers.remove(reader);
	InvokeQueued(this, [=] { process(); });
}

bool Manager::carries(Reader *reader) const {
	QMutexLocker lock(&_readerPointersMutex);
	return _readerPointers.contains(reader);
}

Manager::ReaderPointers::iterator Manager::unsafeFindReaderPointer(ReaderPrivate *reader) {
	ReaderPointers::iterator it = _readerPointers.find(reader->_interface);

	// could be a new reader which was realloced in the same address
	return (it == _readerPointers.cend() || it.key()->_private == reader) ? it : _readerPointers.end();
}

Manager::ReaderPointers::const_iterator Manager::constUnsafeFindReaderPointer(ReaderPrivate *reader) const {
	ReaderPointers::const_iterator it = _readerPointers.constFind(reader->_interface);

	// could be a new reader which was realloced in the same address
	return (it == _readerPointers.cend() || it.key()->_private == reader) ? it : _readerPointers.cend();
}

void Manager::callback(Reader *reader, Notification notification) {
	crl::on_main([=, threadIndex = reader->threadIndex()] {
		Reader::callback(reader, threadIndex, notification);
	});
}

bool Manager::handleProcessResult(ReaderPrivate *reader, ProcessResult result, crl::time ms) {
	QMutexLocker lock(&_readerPointersMutex);
	auto it = unsafeFindReaderPointer(reader);
	if (result == ProcessResult::Error) {
		if (it != _readerPointers.cend()) {
			it.key()->error();
			callback(it.key(), NotificationReinit);
			_readerPointers.erase(it);
		}
		return false;
	} else if (result == ProcessResult::Finished) {
		if (it != _readerPointers.cend()) {
			it.key()->finished();
			callback(it.key(), NotificationReinit);
		}
		return false;
	}
	if (it == _readerPointers.cend()) {
		return false;
	}

	if (result == ProcessResult::Started) {
		_loadLevel.fetchAndAddRelaxed(reader->_width * reader->_height - kAverageGifSize);
		it.key()->_durationMs = reader->_durationMs;
	}
	// See if we need to pause GIF because it is not displayed right now.
	if (!reader->_autoPausedGif && result == ProcessResult::Repaint) {
		int32 ishowing, iprevious;
		auto showing = it.key()->frameToShow(&ishowing), previous = it.key()->frameToWriteNext(false, &iprevious);
		Assert(previous != nullptr && showing != nullptr && ishowing >= 0 && iprevious >= 0);
		if (reader->_frames[ishowing].when > 0 && showing->displayed.loadAcquire() <= 0) { // current frame was not shown
			if (reader->_frames[ishowing].when + kWaitBeforeGifPause < ms || (reader->_frames[iprevious].when && previous->displayed.loadAcquire() <= 0)) {
				reader->_autoPausedGif = true;
				it.key()->_autoPausedGif.storeRelease(1);
				result = ProcessResult::Paused;
			}
		}
	}
	if (result == ProcessResult::Started || result == ProcessResult::CopyFrame) {
		Assert(reader->_frame >= 0);
		auto frame = it.key()->_frames + reader->_frame;
		frame->clear();
		frame->pix = reader->frame()->pix;
		frame->original = reader->frame()->original;
		frame->displayed.storeRelease(0);
		frame->positionMs = reader->frame()->positionMs;
		if (result == ProcessResult::Started) {
			reader->startedAt(ms);
			it.key()->moveToNextWrite();
			callback(it.key(), NotificationReinit);
		}
	} else if (result == ProcessResult::Paused) {
		it.key()->moveToNextWrite();
		callback(it.key(), NotificationReinit);
	} else if (result == ProcessResult::Repaint) {
		it.key()->moveToNextWrite();
		callback(it.key(), NotificationRepaint);
	}
	return true;
}

Manager::ResultHandleState Manager::handleResult(ReaderPrivate *reader, ProcessResult result, crl::time ms) {
	if (!handleProcessResult(reader, result, ms)) {
		_loadLevel.fetchAndAddRelaxed(-1 * (reader->_width > 0 ? reader->_width * reader->_height : kAverageGifSize));
		delete reader;
		return ResultHandleRemove;
	}

	_processingInThread->eventDispatcher()->processEvents(QEventLoop::AllEvents);
	if (_processingInThread->isInterruptionRequested()) {
		return ResultHandleStop;
	}

	if (result == ProcessResult::Repaint) {
		{
			QMutexLocker lock(&_readerPointersMutex);
			auto it = constUnsafeFindReaderPointer(reader);
			if (it != _readerPointers.cend()) {
				int32 index = 0;
				Reader::Frame *frame = it.key()->frameToWrite(&index);
				if (frame) {
					frame->clear();
				} else {
					Assert(!reader->_request.valid());
				}
				reader->_frame = index;
			}
		}
		return handleResult(reader, reader->finishProcess(ms), ms);
	}

	return ResultHandleContinue;
}

void Manager::process() {
	if (_processingInThread) {
		_needReProcess = true;
		return;
	}

	_timer.stop();
	_processingInThread = thread();

	bool checkAllReaders = false;
	auto ms = crl::now(), minms = ms + 86400 * crl::time(1000);
	{
		QMutexLocker lock(&_readerPointersMutex);
		for (auto it = _readerPointers.begin(), e = _readerPointers.end(); it != e; ++it) {
			if (it->loadAcquire() && it.key()->_private != nullptr) {
				auto i = _readers.find(it.key()->_private);
				if (i == _readers.cend()) {
					_readers.insert(it.key()->_private, 0);
				} else {
					i.value() = ms;
					if (i.key()->_autoPausedGif && !it.key()->_autoPausedGif.loadAcquire()) {
						i.key()->_autoPausedGif = false;
					}
					if (it.key()->_videoPauseRequest.loadAcquire()) {
						i.key()->pauseVideo(ms);
					} else {
						i.key()->resumeVideo(ms);
					}
				}
				auto frame = it.key()->frameToWrite();
				if (frame) it.key()->_private->_request = frame->request;
				it->storeRelease(0);
			}
		}
		checkAllReaders = (_readers.size() > _readerPointers.size());
	}

	for (auto i = _readers.begin(), e = _readers.end(); i != e;) {
		ReaderPrivate *reader = i.key();
		if (i.value() <= ms) {
			ResultHandleState state = handleResult(reader, reader->process(ms), ms);
			if (state == ResultHandleRemove) {
				i = _readers.erase(i);
				continue;
			} else if (state == ResultHandleStop) {
				_processingInThread = nullptr;
				return;
			}
			ms = crl::now();
			if (reader->_videoPausedAtMs) {
				i.value() = ms + 86400 * 1000ULL;
			} else if (reader->_nextFrameWhen && reader->_started) {
				i.value() = reader->_nextFrameWhen;
			} else {
				i.value() = (ms + 86400 * 1000ULL);
			}
		} else if (checkAllReaders) {
			QMutexLocker lock(&_readerPointersMutex);
			auto it = constUnsafeFindReaderPointer(reader);
			if (it == _readerPointers.cend()) {
				_loadLevel.fetchAndAddRelaxed(-1 * (reader->_width > 0 ? reader->_width * reader->_height : kAverageGifSize));
				delete reader;
				i = _readers.erase(i);
				continue;
			}
		}
		if (!reader->_autoPausedGif && i.value() < minms) {
			minms = i.value();
		}
		++i;
	}

	ms = crl::now();
	if (_needReProcess || minms <= ms) {
		_needReProcess = false;
		_timer.start(1);
	} else {
		_timer.start(minms - ms);
	}

	_processingInThread = nullptr;
}

void Manager::finish() {
	_timer.stop();
	clear();
}

void Manager::clear() {
	{
		QMutexLocker lock(&_readerPointersMutex);
		for (auto it = _readerPointers.begin(), e = _readerPointers.end(); it != e; ++it) {
			it.key()->_private = nullptr;
		}
		_readerPointers.clear();
	}

	for (Readers::iterator i = _readers.begin(), e = _readers.end(); i != e; ++i) {
		delete i.key();
	}
	_readers.clear();
}

Manager::~Manager() {
	clear();
}

Ui::PreparedFileInformation::Video PrepareForSending(const QString &fname, const QByteArray &data) {
	auto result = Ui::PreparedFileInformation::Video();
	auto localLocation = Core::FileLocation(fname);
	auto localData = QByteArray(data);

	auto seekPositionMs = crl::time(0);
	auto reader = std::make_unique<internal::FFMpegReaderImplementation>(&localLocation, &localData);
	if (reader->start(internal::ReaderImplementation::Mode::Inspecting, seekPositionMs)) {
		auto durationMs = reader->durationMs();
		if (durationMs > 0) {
			result.isGifv = reader->isGifv();
			// Use first video frame as a thumbnail.
			// All other apps and server do that way.
			//if (!result.isGifv) {
			//	auto middleMs = durationMs / 2;
			//	if (!reader->inspectAt(middleMs)) {
			//		return result;
			//	}
			//}
			auto hasAlpha = false;
			auto readResult = reader->readFramesTill(-1, crl::now());
			auto readFrame = (readResult == internal::ReaderImplementation::ReadResult::Success);
			if (readFrame && reader->renderFrame(result.thumbnail, hasAlpha, QSize())) {
				if (hasAlpha) {
					auto cacheForResize = QImage();
					auto request = FrameRequest();
					request.framew = request.outerw = result.thumbnail.width();
					request.frameh = request.outerh = result.thumbnail.height();
					request.factor = 1;
					result.thumbnail = PrepareFrameImage(request, result.thumbnail, hasAlpha, cacheForResize);
				}
				result.duration = static_cast<int>(durationMs / 1000);
			}

			result.supportsStreaming = CheckStreamingSupport(
				localLocation,
				localData);
		}
	}
	return result;
}

void Finish() {
	if (!threads.isEmpty()) {
		for (int32 i = 0, l = threads.size(); i < l; ++i) {
			threads.at(i)->quit();
			DEBUG_LOG(("Waiting for clipThread to finish: %1").arg(i));
			threads.at(i)->wait();
			delete managers.at(i);
			delete threads.at(i);
		}
		threads.clear();
		managers.clear();
	}
}

Reader *const ReaderPointer::BadPointer = reinterpret_cast<Reader*>(1);

ReaderPointer::~ReaderPointer() {
	if (valid()) {
		delete _pointer;
	}
	_pointer = nullptr;
}

} // namespace Clip
} // namespace Media
