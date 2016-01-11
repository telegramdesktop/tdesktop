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
#pragma once

#include "types.h"
#include <QtCore/QTimer>
#include <QtGui/QColor>

namespace anim {

	typedef float64 (*transition)(const float64 &delta, const float64 &dt);

    float64 linear(const float64 &delta, const float64 &dt);
	float64 sineInOut(const float64 &delta, const float64 &dt);
    float64 halfSine(const float64 &delta, const float64 &dt);
    float64 easeOutBack(const float64 &delta, const float64 &dt);
    float64 easeInCirc(const float64 &delta, const float64 &dt);
    float64 easeOutCirc(const float64 &delta, const float64 &dt);
    float64 easeInCubic(const float64 &delta, const float64 &dt);
    float64 easeOutCubic(const float64 &delta, const float64 &dt);
    float64 easeInQuint(const float64 &delta, const float64 &dt);
    float64 easeOutQuint(const float64 &delta, const float64 &dt);

	class fvalue { // float animated value
	public:

		fvalue() {
		}
		fvalue(const float64 &from) : _cur(from), _from(from), _delta(0) {
		}
		fvalue(const float64 &from, const float64 &to) : _cur(from), _from(from), _delta(to - from) {
		}
		void start(const float64 &to) {
			_from = _cur;
			_delta = to - _from;
		}
		void restart() {
			_delta = _from + _delta - _cur;
			_from = _cur;
		}
		const float64 &current() const {
			return _cur;
		}
		float64 to() const {
			return _from + _delta;
		}
		fvalue &update(const float64 &dt, transition func) {
			_cur = _from + (*func)(_delta, dt);
			return *this;
		}
		void finish() {
			_cur = _from + _delta;
			_from = _cur;
			_delta = 0;
		}

		typedef float64 Type;

	private:

		float64 _cur, _from, _delta;
	};

	class ivalue { // int animated value
	public:

		ivalue() {
		}
		ivalue(int32 from) : _cur(from), _from(float64(from)), _delta(0) {
		}
		ivalue(int32 from, int32 to) : _cur(from), _from(float64(from)), _delta(float64(to - from)) {
		}
		void start(int32 to) {
			_from = float64(_cur);
			_delta = float64(to) - _from;
		}
		void restart() {
			_delta = _from + _delta - float64(_cur);
			_from = float64(_cur);
		}
		int32 current() const {
			return _cur;
		}
		int32 to() const {
			return _from + _delta;
		}
		ivalue &update(const float64 &dt, transition func) {
			_cur = qRound(_from + (*func)(_delta, dt));
			return *this;
		}
		void finish() {
			_cur = qRound(_from + _delta);
			_from = _cur;
			_delta = 0;
		}

		typedef int32 Type;

	private:

		int32 _cur;
		float64 _from, _delta;
	};

	class cvalue { // QColor animated value
	public:

		cvalue() {
		}
		cvalue(const QColor &from) : _cur(from), _from_r(from.redF()), _from_g(from.greenF()), _from_b(from.blueF()), _from_a(from.alphaF()), _delta_r(0), _delta_g(0), _delta_b(0), _delta_a(0) {
		}
		cvalue(const QColor &from, const QColor &to)
			: _cur(from)
			, _from_r(from.redF()), _from_g(from.greenF()), _from_b(from.blueF()), _from_a(from.alphaF())
			, _delta_r(to.redF() - from.redF()), _delta_g(to.greenF() - from.greenF()), _delta_b(to.blueF() - from.blueF()), _delta_a(to.alphaF() - from.alphaF())
		{
		}
		void start(const QColor &to) {
			_from_r = _cur.redF();
			_from_g = _cur.greenF();
			_from_b = _cur.blueF();
			_from_a = _cur.alphaF();
			_delta_r = to.redF() - _from_r;
			_delta_g = to.greenF() - _from_g;
			_delta_b = to.blueF() - _from_b;
			_delta_a = to.alphaF() - _from_a;
		}
		void restart() {
			_delta_r = _from_r + _delta_r - _cur.redF();
			_delta_g = _from_g + _delta_g - _cur.greenF();
			_delta_b = _from_b + _delta_b - _cur.blueF();
			_delta_a = _from_a + _delta_a - _cur.alphaF();
			_from_r = _cur.redF();
			_from_g = _cur.greenF();
			_from_b = _cur.blueF();
			_from_a = _cur.alphaF();
		}
		const QColor &current() const {
			return _cur;
		}
		QColor to() const {
			QColor result;
			result.setRedF(_from_r + _delta_r);
			result.setGreenF(_from_g + _delta_g);
			result.setBlueF(_from_b + _delta_b);
			result.setAlphaF(_from_a + _delta_a);
			return result;
		}
		cvalue &update(const float64 &dt, transition func) {
			_cur.setRedF(_from_r + (*func)(_delta_r, dt));
			_cur.setGreenF(_from_g + (*func)(_delta_g, dt));
			_cur.setBlueF(_from_b + (*func)(_delta_b, dt));
			_cur.setAlphaF(_from_a + (*func)(_delta_a, dt));
			return *this;
		}
		void finish() {
			_cur.setRedF(_from_r + _delta_r);
			_cur.setGreenF(_from_g + _delta_g);
			_cur.setBlueF(_from_b + _delta_b);
			_cur.setAlphaF(_from_a + _delta_a);
			_from_r = _cur.redF();
			_from_g = _cur.greenF();
			_from_b = _cur.blueF();
			_from_a = _cur.alphaF();
			_delta_r = _delta_g = _delta_b = _delta_a = 0;
		}

		typedef QColor Type;

	private:

		QColor _cur;
		float64 _from_r, _from_g, _from_b, _from_a, _delta_r, _delta_g, _delta_b, _delta_a;
	};

	void startManager();
	void stopManager();

};

class Animation;

class AnimationImplementation {
public:
	virtual void start() {}
	virtual void step(Animation *a, uint64 ms, bool timer) = 0;
	virtual ~AnimationImplementation() {}
};
class AnimationCreator {
public:
	AnimationCreator(AnimationImplementation *ptr) : _ptr(ptr) {}
	AnimationCreator(const AnimationCreator &other) : _ptr(other.create()) {}
	AnimationImplementation *create() const { return exchange(_ptr); }
	~AnimationCreator() { deleteAndMark(_ptr); }
private:
	AnimationCreator &operator=(const AnimationCreator &other);
	mutable AnimationImplementation *_ptr;
};
class AnimationCallbacks {
public:
	AnimationCallbacks(const AnimationCreator &creator) : _implementation(creator.create()) {}
	void start() { _implementation->start();  }
	void step(Animation *a, uint64 ms, bool timer) { _implementation->step(a, ms, timer); }
	~AnimationCallbacks() { deleteAndMark(_implementation); }
private:
	AnimationCallbacks(const AnimationCallbacks &other);
	AnimationCallbacks &operator=(const AnimationCallbacks &other);
	AnimationImplementation *_implementation;
};

class Animation {
public:
	Animation(AnimationCreator cb) : _cb(cb), _animating(false) {
	}

	void start();
	void stop();

	void step(uint64 ms, bool timer = false) {
		_cb.step(this, ms, timer);
	}

	void step() {
		step(getms(), false);
	}

	bool animating() const {
		return _animating;
	}

	~Animation() {
		if (_animating) stop();
	}

private:
	AnimationCallbacks _cb;
	bool _animating;

};

template <typename Type>
class AnimationCallbacksRelative : public AnimationImplementation {
public:
	typedef void (Type::*Method)(float64, bool);

	AnimationCallbacksRelative(Type *obj, Method method) : _started(0), _obj(obj), _method(method) {
	}

	void start() {
		_started = float64(getms());
	}

	void step(Animation *a, uint64 ms, bool timer) {
		(_obj->*_method)(ms - _started, timer);
	}

private:
	float64 _started;
	Type *_obj;
	Method _method;

};
template <typename Type>
AnimationCreator animation(Type *obj, typename AnimationCallbacksRelative<Type>::Method method) {
	return AnimationCreator(new AnimationCallbacksRelative<Type>(obj, method));
}

template <typename Type>
class AnimationCallbacksAbsolute : public AnimationImplementation {
public:
	typedef void (Type::*Method)(uint64, bool);

	AnimationCallbacksAbsolute(Type *obj, Method method) : _obj(obj), _method(method) {
	}

	void step(Animation *a, uint64 ms, bool timer) {
		(_obj->*_method)(ms, timer);
	}

private:
	Type *_obj;
	Method _method;

};
template <typename Type>
AnimationCreator animation(Type *obj, typename AnimationCallbacksAbsolute<Type>::Method method) {
	return AnimationCreator(new AnimationCallbacksAbsolute<Type>(obj, method));
}

template <typename Type, typename Param>
class AnimationCallbacksRelativeWithParam : public AnimationImplementation {
public:
	typedef void (Type::*Method)(Param, float64, bool);

	AnimationCallbacksRelativeWithParam(Param param, Type *obj, Method method) : _started(0), _param(param), _obj(obj), _method(method) {
	}

	void start() {
		_started = float64(getms());
	}

	void step(Animation *a, uint64 ms, bool timer) {
		(_obj->*_method)(_param, ms - _started, timer);
	}

private:
	float64 _started;
	Param _param;
	Type *_obj;
	Method _method;

};
template <typename Type, typename Param>
AnimationCreator animation(Param param, Type *obj, typename AnimationCallbacksRelativeWithParam<Type, Param>::Method method) {
	return AnimationCreator(new AnimationCallbacksRelativeWithParam<Type, Param>(param, obj, method));
}

template <typename Type, typename Param>
class AnimationCallbacksAbsoluteWithParam : public AnimationImplementation {
public:
	typedef void (Type::*Method)(Param, uint64, bool);

	AnimationCallbacksAbsoluteWithParam(Param param, Type *obj, Method method) : _param(param), _obj(obj), _method(method) {
	}

	void step(Animation *a, uint64 ms, bool timer) {
		(_obj->*_method)(_param, ms, timer);
	}

private:
	Param _param;
	Type *_obj;
	Method _method;

};
template <typename Type, typename Param>
AnimationCreator animation(Param param, Type *obj, typename AnimationCallbacksAbsoluteWithParam<Type, Param>::Method method) {
	return AnimationCreator(new AnimationCallbacksAbsoluteWithParam<Type, Param>(param, obj, method));
}

template <typename AnimType>
class SimpleAnimation {
public:

	typedef Function<void> Callback;

	SimpleAnimation() : _data(0) {
	}

	bool animating(uint64 ms) {
		if (_data && _data->_a.animating()) {
			_data->_a.step(ms);
			return _data && _data->_a.animating();
		}
		return false;
	}

	bool isNull() {
		return !_data;
	}

	typename AnimType::Type current() {
		return _data ? _data->a.current() : typename AnimType::Type();
	}

	typename AnimType::Type current(uint64 ms, const typename AnimType::Type &def) {
		return animating(ms) ? current() : def;
	}

	void setup(const typename AnimType::Type &from, Callback::Creator update) {
		if (!_data) {
			_data = new Data(from, update, animation(this, &SimpleAnimation<AnimType>::step));
		} else {
			_data->a = AnimType(from, from);
		}
	}

	void start(const typename AnimType::Type &to, float64 duration, anim::transition transition = anim::linear) {
		if (_data) {
			_data->a.start(to);
			_data->_a.start();
			_data->duration = duration;
			_data->transition = transition;
		}
	}

	~SimpleAnimation() {
		deleteAndMark(_data);
	}

private:
	typedef struct _Data {
		_Data(const typename AnimType::Type &from, Callback::Creator update, AnimationCreator acb)
			: a(from, from)
			, _a(acb)
			, update(update)
			, duration(0)
			, transition(anim::linear) {
		}
		AnimType a;
		Animation _a;
		Callback update;
		float64 duration;
		anim::transition transition;
	} Data;
	Data *_data;

	void step(float64 ms, bool timer) {
		float64 dt = (ms >= _data->duration) ? 1 : (ms / _data->duration);
		if (dt >= 1) {
			_data->a.finish();
			_data->_a.stop();
		} else {
			_data->a.update(dt, _data->transition);
		}
		if (timer) {
			_data->update.call();
		}
		if (!_data->_a.animating()) {
			delete _data;
			_data = 0;
		}
	}

};

typedef SimpleAnimation<anim::fvalue> FloatAnimation;
typedef SimpleAnimation<anim::ivalue> IntAnimation;
typedef SimpleAnimation<anim::cvalue> ColorAnimation;

#define EnsureAnimation(animation, from, callback) if ((animation).isNull()) { (animation).setup((from), (callback)); }

class ClipReader;

class AnimationManager : public QObject {
Q_OBJECT

public:
	AnimationManager();

	void start(Animation *obj);
	void stop(Animation *obj);

public slots:
	void timeout();

	void clipCallback(ClipReader *reader, qint32 threadIndex, qint32 notification);

private:
	typedef QMap<Animation*, NullType> AnimatingObjects;
	AnimatingObjects _objects, _starting, _stopping;
	QTimer _timer;
	bool _iterating;

};

class FileLocation;

enum ClipState {
	ClipReading,
	ClipError,
};

struct ClipFrameRequest {
	ClipFrameRequest() : factor(0), framew(0), frameh(0), outerw(0), outerh(0), rounded(false) {
	}
	bool valid() const {
		return factor > 0;
	}
	int32 factor;
	int32 framew, frameh;
	int32 outerw, outerh;
	bool rounded;
};

enum ClipReaderNotification {
	ClipReaderReinit,
	ClipReaderRepaint,
};

enum ClipReaderSteps {
	WaitingForDimensionsStep = -3, // before ClipReaderPrivate read the first image and got the original frame size
	WaitingForRequestStep = -2, // before ClipReader got the original frame size and prepared the frame request
	WaitingForFirstFrameStep = -1, // before ClipReaderPrivate got the frame request and started waiting for the 1-2 delay
};

class ClipReaderPrivate;
class ClipReader {
public:

	typedef Function1<void, ClipReaderNotification> Callback;

	ClipReader(const FileLocation &location, const QByteArray &data, Callback::Creator cb);
	static void callback(ClipReader *reader, int32 threadIndex, ClipReaderNotification notification); // reader can be deleted

	void setAutoplay() {
		_autoplay = true;
	}
	bool autoplay() const {
		return _autoplay;
	}

	void start(int32 framew, int32 frameh, int32 outerw, int32 outerh, bool rounded);
	QPixmap current(int32 framew, int32 frameh, int32 outerw, int32 outerh, uint64 ms);
	QPixmap frameOriginal() const {
		Frame *frame = frameToShow();
		if (!frame) return QPixmap();
		QPixmap result(frame ? QPixmap::fromImage(frame->original) : QPixmap());
		result.detach();
		return result;
	}
	bool currentDisplayed() const {
		Frame *frame = frameToShow();
		return frame ? (frame->displayed.loadAcquire() != 0) : true;
	}
	bool paused() const {
		return _paused.loadAcquire();
	}
	int32 threadIndex() const {
		return _threadIndex;
	}

	int32 width() const;
	int32 height() const;

	ClipState state() const;
	bool started() const {
		int32 step = _step.loadAcquire();
		return (step == WaitingForFirstFrameStep) || (step >= 0);
	}
	bool ready() const;

	void stop();
	void error();

	~ClipReader();

private:

	Callback _cb;

	ClipState _state;

	mutable int32 _width, _height;

	mutable QAtomicInt _step; // -2, -1 - init, 0-5 - work, show ((state + 1) / 2) % 3 state, write ((state + 3) / 2) % 3
	struct Frame {
		Frame() : displayed(false) {
		}
		void clear() {
			pix = QPixmap();
			original = QImage();
		}
		QPixmap pix;
		QImage original;
		ClipFrameRequest request;
		QAtomicInt displayed;
	};
	mutable Frame _frames[3];
	Frame *frameToShow(int32 *index = 0) const; // 0 means not ready
	Frame *frameToWrite(int32 *index = 0) const; // 0 means not ready
	Frame *frameToWriteNext(bool check, int32 *index = 0) const;
	void moveToNextShow() const;
	void moveToNextWrite() const;

	QAtomicInt _paused;
	int32 _threadIndex;

	bool _autoplay;

	friend class ClipReadManager;

	ClipReaderPrivate *_private;

};

static ClipReader * const BadClipReader = SharedMemoryLocation<ClipReader, 0>();

enum ClipProcessResult {
	ClipProcessError,
	ClipProcessStarted,
	ClipProcessPaused,
	ClipProcessRepaint,
	ClipProcessCopyFrame,
	ClipProcessWait,
};

class ClipReadManager : public QObject {
	Q_OBJECT

public:

	ClipReadManager(QThread *thread);
	int32 loadLevel() const {
		return _loadLevel.load();
	}
	void append(ClipReader *reader, const FileLocation &location, const QByteArray &data);
	void start(ClipReader *reader);
	void update(ClipReader *reader);
	void stop(ClipReader *reader);
	bool carries(ClipReader *reader) const;
	~ClipReadManager();

signals:

	void processDelayed();

	void callback(ClipReader *reader, qint32 threadIndex, qint32 notification);

public slots:

	void process();
    void finish();

private:

    void clear();

	QAtomicInt _loadLevel;
	struct MutableAtomicInt {
		MutableAtomicInt(int value) : v(value) {
		}
		mutable QAtomicInt v;
	};
	typedef QMap<ClipReader*, MutableAtomicInt> ReaderPointers;
	ReaderPointers _readerPointers;
	mutable QReadWriteLock _readerPointersMutex;

	ReaderPointers::const_iterator constUnsafeFindReaderPointer(ClipReaderPrivate *reader) const;
	ReaderPointers::iterator unsafeFindReaderPointer(ClipReaderPrivate *reader);

	bool handleProcessResult(ClipReaderPrivate *reader, ClipProcessResult result, uint64 ms);

	enum ResultHandleState {
		ResultHandleRemove,
		ResultHandleStop,
		ResultHandleContinue,
	};
	ResultHandleState handleResult(ClipReaderPrivate *reader, ClipProcessResult result, uint64 ms);

	typedef QMap<ClipReaderPrivate*, uint64> Readers;
	Readers _readers;

	QTimer _timer;
	QThread *_processingInThread;
	bool _needReProcess;

};

MTPDocumentAttribute clipReadAnimatedAttributes(const QString &fname, const QByteArray &data, QImage &cover);
