/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/chat/attach/attach_prepare.h"
#include "ui/image/image_prepare.h"

#include <QtCore/QTimer>
#include <QtCore/QMutex>

namespace Core {
class FileLocation;
} // namespace Core

namespace Media {
namespace Clip {

enum class State {
	Reading,
	Error,
	Finished,
};

struct FrameRequest {
	bool valid() const {
		return factor > 0;
	}
	int factor = 0;
	int framew = 0;
	int frameh = 0;
	int outerw = 0;
	int outerh = 0;
	ImageRoundRadius radius = ImageRoundRadius::None;
	RectParts corners = RectPart::AllCorners;
};

enum ReaderSteps : int {
	WaitingForDimensionsStep = -3, // before ReaderPrivate read the first image and got the original frame size
	WaitingForRequestStep = -2, // before Reader got the original frame size and prepared the frame request
	WaitingForFirstFrameStep = -1, // before ReaderPrivate got the frame request and started waiting for the 1-2 delay
};

enum Notification : int {
	NotificationReinit,
	NotificationRepaint,
};

class ReaderPrivate;
class Reader {
public:
	using Callback = Fn<void(Notification)>;
	enum class Mode {
		Gif,
		Video,
	};

	Reader(const Core::FileLocation &location, const QByteArray &data, Callback &&callback);
	Reader(const QString &filepath, Callback &&callback);
	Reader(const QByteArray &data, Callback &&callback);

	// Reader can be already deleted.
	static void callback(Reader *reader, qint32 threadIndex, qint32 notification);

	void start(int framew, int frameh, int outerw, int outerh, ImageRoundRadius radius, RectParts corners);
	QPixmap current(int framew, int frameh, int outerw, int outerh, ImageRoundRadius radius, RectParts corners, crl::time ms);
	QPixmap frameOriginal() const {
		if (auto frame = frameToShow()) {
			auto result = QPixmap::fromImage(frame->original);
			result.detach();
			return result;
		}
		return QPixmap();
	}
	bool currentDisplayed() const {
		auto frame = frameToShow();
		return frame ? (frame->displayed.loadAcquire() != 0) : true;
	}
	bool autoPausedGif() const {
		return _autoPausedGif.loadAcquire();
	}
	bool videoPaused() const;
	int threadIndex() const {
		return _threadIndex;
	}

	int width() const;
	int height() const;

	State state() const;
	bool started() const {
		auto step = _step.loadAcquire();
		return (step == WaitingForFirstFrameStep) || (step >= 0);
	}
	bool ready() const;

	crl::time getPositionMs() const;
	crl::time getDurationMs() const;
	void pauseResumeVideo();

	void stop();
	void error();
	void finished();

	~Reader();

private:
	void init(const Core::FileLocation &location, const QByteArray &data);

	Callback _callback;
	State _state = State::Reading;

	crl::time _durationMs = 0;

	mutable int _width = 0;
	mutable int _height = 0;

	// -2, -1 - init, 0-5 - work, show ((state + 1) / 2) % 3 state, write ((state + 3) / 2) % 3
	mutable QAtomicInt _step = WaitingForDimensionsStep;
	struct Frame {
		void clear() {
			pix = QPixmap();
			original = QImage();
		}
		QPixmap pix;
		QImage original;
		FrameRequest request;
		QAtomicInt displayed = 0;

		// Should be counted from the end,
		// so that positionMs <= _durationMs.
		crl::time positionMs = 0;
	};
	mutable Frame _frames[3];
	Frame *frameToShow(int *index = nullptr) const; // 0 means not ready
	Frame *frameToWrite(int *index = nullptr) const; // 0 means not ready
	Frame *frameToWriteNext(bool check, int *index = nullptr) const;
	void moveToNextShow() const;
	void moveToNextWrite() const;

	QAtomicInt _autoPausedGif = 0;
	QAtomicInt _videoPauseRequest = 0;
	int32 _threadIndex;

	friend class Manager;

	ReaderPrivate *_private = nullptr;

};

class ReaderPointer {
public:
	ReaderPointer(std::nullptr_t = nullptr) {
	}
	explicit ReaderPointer(Reader *pointer) : _pointer(pointer) {
	}
	ReaderPointer(const ReaderPointer &other) = delete;
	ReaderPointer &operator=(const ReaderPointer &other) = delete;
	ReaderPointer(ReaderPointer &&other) : _pointer(base::take(other._pointer)) {
	}
	ReaderPointer &operator=(ReaderPointer &&other) {
		swap(other);
		return *this;
	}
	void swap(ReaderPointer &other) {
		qSwap(_pointer, other._pointer);
	}
	Reader *get() const {
		return valid() ? _pointer : nullptr;
	}
	Reader *operator->() const {
		return get();
	}
	void setBad() {
		reset();
		_pointer = BadPointer;
	}
	void reset() {
		ReaderPointer temp;
		swap(temp);
	}
	bool isBad() const {
		return (_pointer == BadPointer);
	}
	bool valid() const {
		return _pointer && !isBad();
	}
	explicit operator bool() const {
		return valid();
	}
	static inline ReaderPointer Bad() {
		ReaderPointer result;
		result.setBad();
		return result;
	}
	~ReaderPointer();

private:
	Reader *_pointer = nullptr;
	static Reader *const BadPointer;

};

template <typename ...Args>
inline ReaderPointer MakeReader(Args&&... args) {
	return ReaderPointer(new Reader(std::forward<Args>(args)...));
}

enum class ProcessResult {
	Error,
	Started,
	Finished,
	Paused,
	Repaint,
	CopyFrame,
	Wait,
};

class Manager : public QObject {
public:
	explicit Manager(QThread *thread);
	~Manager();

	int loadLevel() const {
		return _loadLevel.load();
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

[[nodiscard]] Ui::PreparedFileInformation::Video PrepareForSending(
	const QString &fname,
	const QByteArray &data);

void Finish();

} // namespace Clip
} // namespace Media
