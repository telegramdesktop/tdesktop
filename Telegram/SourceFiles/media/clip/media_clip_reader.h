/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/image/image_prepare.h"

#include <QtCore/QTimer>
#include <QtCore/QMutex>

namespace Ui {
struct PreparedFileInformation;
} // namespace Ui

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
	[[nodiscard]] bool valid() const {
		return factor > 0;
	}

	QSize frame;
	QSize outer;
	int factor = 0;
	ImageRoundRadius radius = ImageRoundRadius::None;
	RectParts corners = RectPart::AllCorners;
	QColor colored = QColor(0, 0, 0, 0);
	bool keepAlpha = false;
};

// Before ReaderPrivate read the first image and got the original frame size.
inline constexpr auto kWaitingForDimensionsStep = -3;

// Before Reader got the original frame size and prepared the frame request.
inline constexpr auto kWaitingForRequestStep = -2;

// Before ReaderPrivate got the frame request
// and started waiting for the 1-2 delay.
inline constexpr auto kWaitingForFirstFrameStep = -1;

enum class Notification {
	Reinit,
	Repaint,
};

class Manager;
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
	static void SafeCallback(
		Reader *reader,
		int threadIndex,
		Notification notification);

	void start(FrameRequest request);

	struct FrameInfo {
		QImage image;
		int index = 0;
	};
	[[nodiscard]] FrameInfo frameInfo(FrameRequest request, crl::time now);
	[[nodiscard]] QImage current(FrameRequest request, crl::time now) {
		auto result = frameInfo(request, now).image;
		moveToNextFrame();
		return result;
	}
	[[nodiscard]] QImage frameOriginal() const {
		if (const auto frame = frameToShow()) {
			auto result = frame->original;
			result.detach();
			return result;
		}
		return QImage();
	}
	bool moveToNextFrame() {
		return moveToNextShow();
	}
	[[nodiscard]] bool currentDisplayed() const {
		const auto frame = frameToShow();
		return !frame || (frame->displayed.loadAcquire() != 0);
	}
	[[nodiscard]] bool autoPausedGif() const {
		return _autoPausedGif.loadAcquire();
	}
	[[nodiscard]] bool videoPaused() const;
	[[nodiscard]] int threadIndex() const {
		return _threadIndex;
	}

	[[nodiscard]] int width() const;
	[[nodiscard]] int height() const;

	[[nodiscard]] State state() const;
	[[nodiscard]] bool started() const {
		const auto step = _step.loadAcquire();
		return (step == kWaitingForFirstFrameStep) || (step >= 0);
	}
	[[nodiscard]] bool ready() const;

	[[nodiscard]] crl::time getPositionMs() const;
	[[nodiscard]] crl::time getDurationMs() const;
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
	mutable QAtomicInt _step = kWaitingForDimensionsStep;
	struct Frame {
		void clear() {
			prepared = QImage();
			preparedColored = QColor(0, 0, 0, 0);
			original = QImage();
		}

		QImage prepared;
		QColor preparedColored = QColor(0, 0, 0, 0);
		QImage original;
		FrameRequest request;
		QAtomicInt displayed = 0;
		int index = 0;

		// Should be counted from the end,
		// so that positionMs <= _durationMs.
		crl::time positionMs = 0;
	};
	mutable Frame _frames[3];
	Frame *frameToShow(int *index = nullptr) const; // 0 means not ready
	Frame *frameToWrite(int *index = nullptr) const; // 0 means not ready
	Frame *frameToWriteNext(bool check, int *index = nullptr) const;
	bool moveToNextShow() const;
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

[[nodiscard]] Ui::PreparedFileInformation PrepareForSending(
	const QString &fname,
	const QByteArray &data);

void Finish();

} // namespace Clip
} // namespace Media
