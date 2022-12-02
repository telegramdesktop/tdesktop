/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/clip/media_clip_reader.h"

#include "media/clip/media_clip_ffmpeg.h"
#include "media/clip/media_clip_check_streaming.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/painter.h"
#include "core/file_location.h"
#include "base/random.h"
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

QImage PrepareFrame(
		const FrameRequest &request,
		const QImage &original,
		bool hasAlpha,
		QImage &cache) {
	const auto needResize = (original.size() != request.frame);
	const auto needOuterFill = request.outer.isValid()
		&& (request.outer != request.frame);
	const auto needRounding = (request.radius != ImageRoundRadius::None);
	const auto colorizing = (request.colored.alpha() != 0);
	if (!needResize
		&& !needOuterFill
		&& !hasAlpha
		&& !needRounding
		&& !colorizing) {
		return original;
	}

	const auto factor = request.factor;
	const auto size = request.outer.isValid() ? request.outer : request.frame;
	const auto needNewCache = (cache.size() != size);
	if (needNewCache) {
		cache = QImage(size, QImage::Format_ARGB32_Premultiplied);
		cache.setDevicePixelRatio(factor);
	}
	if (hasAlpha && request.keepAlpha) {
		cache.fill(Qt::transparent);
	}
	{
		auto p = QPainter(&cache);
		const auto framew = request.frame.width();
		const auto outerw = size.width();
		const auto frameh = request.frame.height();
		const auto outerh = size.height();
		if (needNewCache && (!hasAlpha || !request.keepAlpha)) {
			if (framew < outerw) {
				p.fillRect(0, 0, (outerw - framew) / (2 * factor), cache.height() / factor, st::imageBg);
				p.fillRect((outerw - framew) / (2 * factor) + (framew / factor), 0, (cache.width() / factor) - ((outerw - framew) / (2 * factor) + (framew / factor)), cache.height() / factor, st::imageBg);
			}
			if (frameh < outerh) {
				p.fillRect(qMax(0, (outerw - framew) / (2 * factor)), 0, qMin(cache.width(), framew) / factor, (outerh - frameh) / (2 * factor), st::imageBg);
				p.fillRect(qMax(0, (outerw - framew) / (2 * factor)), (outerh - frameh) / (2 * factor) + (frameh / factor), qMin(cache.width(), framew) / factor, (cache.height() / factor) - ((outerh - frameh) / (2 * factor) + (frameh / factor)), st::imageBg);
			}
		}
		if (hasAlpha && !request.keepAlpha) {
			p.fillRect(qMax(0, (outerw - framew) / (2 * factor)), qMax(0, (outerh - frameh) / (2 * factor)), qMin(cache.width(), framew) / factor, qMin(cache.height(), frameh) / factor, st::imageBgTransparent);
		}
		const auto position = QPoint((outerw - framew) / (2 * factor), (outerh - frameh) / (2 * factor));
		if (needResize) {
			PainterHighQualityEnabler hq(p);

			const auto dst = QRect(position, QSize(framew / factor, frameh / factor));
			const auto src = QRect(0, 0, original.width(), original.height());
			p.drawImage(dst, original, src, Qt::ColorOnly);
		} else {
			p.drawImage(position, original);
		}
	}
	if (needRounding) {
		cache = Images::Round(
			std::move(cache),
			request.radius,
			request.corners);
	}
	if (colorizing) {
		cache = Images::Colored(std::move(cache), request.colored);
	}
	return cache;
}

} // namespace

enum class ProcessResult {
	Error,
	Started,
	Finished,
	Paused,
	Repaint,
	CopyFrame,
	Wait,
};

class Manager final : public QObject {
public:
	explicit Manager(not_null<QThread*> thread);
	~Manager();

	int loadLevel() const {
		return _loadLevel;
	}
	void append(Reader *reader, const Core::FileLocation &location, const QByteArray &data);
	void start(Reader *reader);
	void update(Reader *reader);
	void stop(Reader *reader);
	bool carries(Reader *reader) const;

private:
	void process();
	void finish();
	void callback(Reader *reader, Notification notification);
	void clear();

	QAtomicInt _loadLevel;
	using ReaderPointers = QMap<Reader*, QAtomicInt>;
	ReaderPointers _readerPointers;
	mutable QMutex _readerPointersMutex;

	ReaderPointers::const_iterator constUnsafeFindReaderPointer(ReaderPrivate *reader) const;
	ReaderPointers::iterator unsafeFindReaderPointer(ReaderPrivate *reader);

	bool handleProcessResult(ReaderPrivate *reader, ProcessResult result, crl::time ms);

	enum ResultHandleState {
		ResultHandleRemove,
		ResultHandleStop,
		ResultHandleContinue,
	};
	ResultHandleState handleResult(ReaderPrivate *reader, ProcessResult result, crl::time ms);

	using Readers = QMap<ReaderPrivate*, crl::time>;
	Readers _readers;

	QTimer _timer;
	QThread *_processingInThread = nullptr;
	bool _needReProcess = false;

};

namespace {

struct Worker {
	Worker() : manager(&thread) {
		thread.start();
	}
	~Worker() {
		thread.quit();
		thread.wait();
	}

	QThread thread;
	Manager manager;
};

std::vector<std::unique_ptr<Worker>>  Workers;

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
	if (Workers.size() < kClipThreadsCount) {
		_threadIndex = Workers.size();
		Workers.push_back(std::make_unique<Worker>());
	} else {
		_threadIndex = base::RandomIndex(Workers.size());
		auto loadLevel = 0x7FFFFFFF;
		for (int i = 0, l = int(Workers.size()); i < l; ++i) {
			const auto level = Workers[i]->manager.loadLevel();
			if (level < loadLevel) {
				_threadIndex = i;
				loadLevel = level;
			}
		}
	}
	Workers[_threadIndex]->manager.append(this, location, data);
}

Reader::Frame *Reader::frameToShow(int32 *index) const { // 0 means not ready
	int step = _step.loadAcquire(), i;
	if (step == kWaitingForDimensionsStep) {
		if (index) *index = 0;
		return nullptr;
	} else if (step == kWaitingForRequestStep) {
		i = 0;
	} else if (step == kWaitingForFirstFrameStep) {
		i = 0;
	} else {
		i = (step / 2) % 3;
	}
	if (index) *index = i;
	return _frames + i;
}

Reader::Frame *Reader::frameToWrite(int32 *index) const { // 0 means not ready
	int32 step = _step.loadAcquire(), i;
	if (step == kWaitingForDimensionsStep) {
		i = 0;
	} else if (step == kWaitingForRequestStep) {
		if (index) *index = 0;
		return nullptr;
	} else if (step == kWaitingForFirstFrameStep) {
		i = 0;
	} else {
		i = ((step + 2) / 2) % 3;
	}
	if (index) *index = i;
	return _frames + i;
}

Reader::Frame *Reader::frameToWriteNext(bool checkNotWriting, int32 *index) const {
	int32 step = _step.loadAcquire(), i;
	if (step == kWaitingForDimensionsStep
		|| step == kWaitingForRequestStep
		|| (checkNotWriting && (step % 2))) {
		if (index) *index = 0;
		return nullptr;
	}
	i = ((step + 4) / 2) % 3;
	if (index) *index = i;
	return _frames + i;
}

bool Reader::moveToNextShow() const {
	const auto step = _step.loadAcquire();
	if (step == kWaitingForDimensionsStep) {
	} else if (step == kWaitingForRequestStep) {
		_step.storeRelease(kWaitingForFirstFrameStep);
		return true;
	} else if (step == kWaitingForFirstFrameStep) {
	} else if (!(step % 2)) {
		_step.storeRelease(step + 1);
		return true;
	}
	return false;
}

void Reader::moveToNextWrite() const {
	int32 step = _step.loadAcquire();
	if (step == kWaitingForDimensionsStep) {
		_step.storeRelease(kWaitingForRequestStep);
	} else if (step == kWaitingForRequestStep) {
	} else if (step == kWaitingForFirstFrameStep) {
		_step.storeRelease(0);

		// Force paint the first frame so moveToNextShow() is called.
		_frames[0].displayed.storeRelease(0);
	} else if (step % 2) {
		_step.storeRelease((step + 1) % 6);
	}
}

void Reader::SafeCallback(
		Reader *reader,
		int threadIndex,
		Notification notification) {
	// Check if reader is not deleted already
	if (Workers.size() > threadIndex
		&& Workers[threadIndex]->manager.carries(reader)
		&& reader->_callback) {
		reader->_callback(Notification(notification));
	}
}

void Reader::start(FrameRequest request) {
	if (Workers.size() <= _threadIndex) {
		error();
	}
	if (_state == State::Error
		|| (_step.loadAcquire() != kWaitingForRequestStep)) {
		return;
	}
	const auto factor = style::DevicePixelRatio();
	request.factor = factor;
	request.frame *= factor;
	if (request.outer.isValid()) {
		request.outer *= factor;
	}
	_frames[0].request = _frames[1].request = _frames[2].request = request;
	moveToNextShow();
	Workers[_threadIndex]->manager.start(this);
}

Reader::FrameInfo Reader::frameInfo(FrameRequest request, crl::time now) {
	Expects(!(request.outer.isValid()
		? request.outer
		: request.frame).isEmpty());

	const auto frame = frameToShow();
	Assert(frame != nullptr);

	const auto shouldBePaused = !now;
	if (!shouldBePaused) {
		frame->displayed.storeRelease(1);
		if (_autoPausedGif.loadAcquire()) {
			_autoPausedGif.storeRelease(0);
			if (Workers.size() <= _threadIndex) {
				error();
			} else if (_state != State::Error) {
				Workers[_threadIndex]->manager.update(this);
			}
		}
	} else {
		frame->displayed.storeRelease(-1);
	}

	const auto factor = style::DevicePixelRatio();
	request.factor = factor;
	request.frame *= factor;
	if (request.outer.isValid()) {
		request.outer *= factor;
	}
	const auto size = request.outer.isValid()
		? request.outer
		: request.frame;
	Assert(frame->request.radius == request.radius
		&& frame->request.corners == request.corners
		&& frame->request.keepAlpha == request.keepAlpha);
	if (frame->prepared.size() != size
		|| frame->preparedColored != request.colored) {
		frame->request.frame = request.frame;
		frame->request.outer = request.outer;
		frame->request.colored = request.colored;

		QImage cacheForResize;
		frame->original.setDevicePixelRatio(factor);
		frame->prepared = QImage();
		frame->prepared = PrepareFrame(
			frame->request,
			frame->original,
			true,
			cacheForResize);
		frame->preparedColored = request.colored;

		auto other = frameToWriteNext(true);
		if (other) other->request = frame->request;

		if (Workers.size() <= _threadIndex) {
			error();
		} else if (_state != State::Error) {
			Workers[_threadIndex]->manager.update(this);
		}
	}
	return { frame->prepared, frame->index };
}

bool Reader::ready() const {
	if (_width && _height) {
		return true;
	}

	const auto frame = frameToShow();
	if (frame) {
		_width = frame->original.width();
		_height = frame->original.height();
		return true;
	}
	return false;
}

crl::time Reader::getPositionMs() const {
	if (const auto frame = frameToShow()) {
		return frame->positionMs;
	}
	return 0;
}

crl::time Reader::getDurationMs() const {
	return ready() ? _durationMs : 0;
}

void Reader::pauseResumeVideo() {
	if (Workers.size() <= _threadIndex) {
		error();
	}
	if (_state == State::Error) return;

	_videoPauseRequest.storeRelease(1 - _videoPauseRequest.loadAcquire());
	Workers[_threadIndex]->manager.start(this);
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
	if (Workers.size() <= _threadIndex) {
		error();
	}
	if (_state != State::Error) {
		Workers[_threadIndex]->manager.stop(this);
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
						if (reader->renderFrame(frame()->original, frame()->alpha, frame()->index, QSize())) {
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
			if (!_implementation->renderFrame(frame()->original, frame()->alpha, frame()->index, QSize())) {
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

		if (!_implementation->renderFrame(frame()->original, frame()->alpha, frame()->index, _request.frame)) {
			return false;
		}
		frame()->original.setDevicePixelRatio(_request.factor);
		frame()->prepared = QImage();
		frame()->prepared = PrepareFrame(
			_request,
			frame()->original,
			frame()->alpha,
			frame()->cache);
		frame()->preparedColored = _request.colored;
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
		QImage prepared;
		QColor preparedColored = QColor(0, 0, 0, 0);
		QImage original;
		QImage cache;
		int index = 0;
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

Manager::Manager(not_null<QThread*> thread) {
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

auto Manager::unsafeFindReaderPointer(ReaderPrivate *reader)
-> ReaderPointers::iterator {
	const auto it = _readerPointers.find(reader->_interface);

	// could be a new reader which was realloced in the same address
	return (it == _readerPointers.cend() || it.key()->_private == reader)
		? it
		: _readerPointers.end();
}

auto Manager::constUnsafeFindReaderPointer(ReaderPrivate *reader) const
-> ReaderPointers::const_iterator {
	const auto it = _readerPointers.constFind(reader->_interface);

	// could be a new reader which was realloced in the same address
	return (it == _readerPointers.cend() || it.key()->_private == reader)
		? it
		: _readerPointers.cend();
}

void Manager::callback(Reader *reader, Notification notification) {
	crl::on_main([=, threadIndex = reader->threadIndex()] {
		Reader::SafeCallback(reader, threadIndex, notification);
	});
}

bool Manager::handleProcessResult(ReaderPrivate *reader, ProcessResult result, crl::time ms) {
	QMutexLocker lock(&_readerPointersMutex);
	auto it = unsafeFindReaderPointer(reader);
	if (result == ProcessResult::Error) {
		if (it != _readerPointers.cend()) {
			it.key()->error();
			callback(it.key(), Notification::Reinit);
			_readerPointers.erase(it);
		}
		return false;
	} else if (result == ProcessResult::Finished) {
		if (it != _readerPointers.cend()) {
			it.key()->finished();
			callback(it.key(), Notification::Reinit);
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
		frame->prepared = reader->frame()->prepared;
		frame->preparedColored = reader->frame()->preparedColored;
		frame->original = reader->frame()->original;
		frame->index = reader->frame()->index;
		frame->displayed.storeRelease(0);
		frame->positionMs = reader->frame()->positionMs;
		if (result == ProcessResult::Started) {
			reader->startedAt(ms);
			it.key()->moveToNextWrite();
			callback(it.key(), Notification::Reinit);
		}
	} else if (result == ProcessResult::Paused) {
		it.key()->moveToNextWrite();
		callback(it.key(), Notification::Reinit);
	} else if (result == ProcessResult::Repaint) {
		it.key()->moveToNextWrite();
		callback(it.key(), Notification::Repaint);
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

Ui::PreparedFileInformation PrepareForSending(
		const QString &fname,
		const QByteArray &data) {
	auto result = Ui::PreparedFileInformation::Video();
	auto localLocation = Core::FileLocation(fname);
	auto localData = QByteArray(data);

	auto seekPositionMs = crl::time(0);
	auto reader = std::make_unique<internal::FFMpegReaderImplementation>(&localLocation, &localData);
	if (reader->start(internal::ReaderImplementation::Mode::Inspecting, seekPositionMs)) {
		auto durationMs = reader->durationMs();
		if (durationMs > 0) {
			result.isGifv = reader->isGifv();
			result.isWebmSticker = reader->isWebmSticker();
			// Use first video frame as a thumbnail.
			// All other apps and server do that way.
			//if (!result.isGifv) {
			//	auto middleMs = durationMs / 2;
			//	if (!reader->inspectAt(middleMs)) {
			//		return result;
			//	}
			//}
			auto index = 0;
			auto hasAlpha = false;
			auto readResult = reader->readFramesTill(-1, crl::now());
			auto readFrame = (readResult == internal::ReaderImplementation::ReadResult::Success);
			if (readFrame && reader->renderFrame(result.thumbnail, hasAlpha, index, QSize())) {
				if (hasAlpha && !result.isWebmSticker) {
					result.thumbnail = Images::Opaque(std::move(result.thumbnail));
				}
				result.duration = static_cast<int>(durationMs / 1000);
			}

			result.supportsStreaming = CheckStreamingSupport(
				localLocation,
				localData);
		}
	}
	return { .media = result };
}

void Finish() {
	Workers.clear();
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
