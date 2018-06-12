/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/localimageloader.h"

class FileLocation;

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

enum ReaderSteps {
	WaitingForDimensionsStep = -3, // before ReaderPrivate read the first image and got the original frame size
	WaitingForRequestStep = -2, // before Reader got the original frame size and prepared the frame request
	WaitingForFirstFrameStep = -1, // before ReaderPrivate got the frame request and started waiting for the 1-2 delay
};

class ReaderPrivate;
class Reader {
public:
	using Callback = Fn<void(Notification)>;
	enum class Mode {
		Gif,
		Video,
	};

	Reader(const QString &filepath, Callback &&callback, Mode mode = Mode::Gif, TimeMs seekMs = 0);
	Reader(not_null<DocumentData*> document, FullMsgId msgId, Callback &&callback, Mode mode = Mode::Gif, TimeMs seekMs = 0);

	static void callback(Reader *reader, int threadIndex, Notification notification); // reader can be deleted

	void setAutoplay() {
		_autoplay = true;
	}
	bool autoplay() const {
		return _autoplay;
	}

	AudioMsgId audioMsgId() const {
		return _audioMsgId;
	}
	TimeMs seekPositionMs() const {
		return _seekPositionMs;
	}

	void start(int framew, int frameh, int outerw, int outerh, ImageRoundRadius radius, RectParts corners);
	QPixmap current(int framew, int frameh, int outerw, int outerh, ImageRoundRadius radius, RectParts corners, TimeMs ms);
	QPixmap current();
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

	bool hasAudio() const;
	TimeMs getPositionMs() const;
	TimeMs getDurationMs() const;
	void pauseResumeVideo();

	void stop();
	void error();
	void finished();

	Mode mode() const {
		return _mode;
	}

	~Reader();

private:
	void init(const FileLocation &location, const QByteArray &data);

	Callback _callback;
	Mode _mode;

	State _state = State::Reading;

	AudioMsgId _audioMsgId;
	bool _hasAudio = false;
	TimeMs _durationMs = 0;
	TimeMs _seekPositionMs = 0;

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
		TimeMs positionMs = 0;
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

	bool _autoplay = false;

	friend class Manager;

	ReaderPrivate *_private = nullptr;

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
	Q_OBJECT

public:

	Manager(QThread *thread);
	int32 loadLevel() const {
		return _loadLevel.load();
	}
	void append(Reader *reader, const FileLocation &location, const QByteArray &data);
	void start(Reader *reader);
	void update(Reader *reader);
	void stop(Reader *reader);
	bool carries(Reader *reader) const;
	~Manager();

signals:
	void processDelayed();

	void callback(Media::Clip::Reader *reader, qint32 threadIndex, qint32 notification);

public slots:
	void process();
	void finish();

private:

	void clear();

	QAtomicInt _loadLevel;
	using ReaderPointers = QMap<Reader*, QAtomicInt>;
	ReaderPointers _readerPointers;
	mutable QMutex _readerPointersMutex;

	ReaderPointers::const_iterator constUnsafeFindReaderPointer(ReaderPrivate *reader) const;
	ReaderPointers::iterator unsafeFindReaderPointer(ReaderPrivate *reader);

	bool handleProcessResult(ReaderPrivate *reader, ProcessResult result, TimeMs ms);

	enum ResultHandleState {
		ResultHandleRemove,
		ResultHandleStop,
		ResultHandleContinue,
	};
	ResultHandleState handleResult(ReaderPrivate *reader, ProcessResult result, TimeMs ms);

	typedef QMap<ReaderPrivate*, TimeMs> Readers;
	Readers _readers;

	QTimer _timer;
	QThread *_processingInThread;
	bool _needReProcess;

};

FileMediaInformation::Video PrepareForSending(const QString &fname, const QByteArray &data);

void Finish();

} // namespace Clip
} // namespace Media
