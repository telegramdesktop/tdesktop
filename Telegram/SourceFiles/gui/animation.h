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

	private:

		QColor _cur;
		float64 _from_r, _from_g, _from_b, _from_a, _delta_r, _delta_g, _delta_b, _delta_a;
	};

	void startManager();
	void stopManager();

};

class Animation;

class AnimationCallbacks {
public:
	virtual void start() {
	}

	virtual void step(Animation *a, uint64 ms, bool timer) = 0;

	virtual ~AnimationCallbacks() {
	}
};

class Animation {
public:

	Animation(AnimationCallbacks *cb) : _cb(cb), _animating(false) {
	}

	void start();
	void stop();

	void step(uint64 ms, bool timer = false) {
		_cb->step(this, ms, timer);
	}

	void step() {
		step(getms(), false);
	}

	bool animating() const {
		return _animating;
	}

	~Animation() {
		if (_animating) stop();
		delete _cb;
	}

private:
	AnimationCallbacks *_cb;
	bool _animating;

};

template <typename Type>
class AnimationCallbacksRelative : public AnimationCallbacks {
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
AnimationCallbacks *animation(Type *obj, typename AnimationCallbacksRelative<Type>::Method method) {
	return new AnimationCallbacksRelative<Type>(obj, method);
}

template <typename Type>
class AnimationCallbacksAbsolute : public AnimationCallbacks {
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
AnimationCallbacks *animation(Type *obj, typename AnimationCallbacksAbsolute<Type>::Method method) {
	return new AnimationCallbacksAbsolute<Type>(obj, method);
}

template <typename Type, typename Param>
class AnimationCallbacksRelativeWithParam : public AnimationCallbacks {
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
AnimationCallbacks *animation(Param param, Type *obj, typename AnimationCallbacksRelativeWithParam<Type, Param>::Method method) {
	return new AnimationCallbacksRelativeWithParam<Type, Param>(param, obj, method);
}

template <typename Type, typename Param>
class AnimationCallbacksAbsoluteWithParam : public AnimationCallbacks {
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
AnimationCallbacks *animation(Param param, Type *obj, typename AnimationCallbacksAbsoluteWithParam<Type, Param>::Method method) {
	return new AnimationCallbacksAbsoluteWithParam<Type, Param>(param, obj, method);
}

class ClipReader;

class AnimationManager : public QObject {
Q_OBJECT

public:

	AnimationManager() : _timer(this), _iterating(false) {
		_timer.setSingleShot(false);
		connect(&_timer, SIGNAL(timeout()), this, SLOT(timeout()));
	}

	void start(Animation *obj) {
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

	void stop(Animation *obj) {
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

public slots:

	void timeout() {
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

	void clipReinit(ClipReader *reader);
	void clipRedraw(ClipReader *reader);

private:

	typedef QMap<Animation*, NullType> AnimatingObjects;
	AnimatingObjects _objects, _starting, _stopping;
	QTimer _timer;
	bool _iterating;

};

class HistoryItem;
class FileLocation;
class AnimatedGif : public QObject {
	Q_OBJECT

public:

	AnimatedGif() : QObject()
		, msg(0)
		, file(0)
		, access(false)
		, reader(0)
		, w(0)
		, h(0)
		, frame(0)
		, framesCount(0)
		, duration(0)
		, _a_frames(animation(this, &AnimatedGif::step_frame)) {
	}

	void step_frame(float64 ms, bool timer);

	void start(HistoryItem *row, const FileLocation &file);
	void stop(bool onItemRemoved = false);

	bool isNull() const {
		return !reader;
	}

	~AnimatedGif() {
		stop(true);
	}

	const QPixmap &current(int32 width = 0, int32 height = 0, bool rounded = false);

signals:

	void updated();

public:

	HistoryItem *msg;
	QImage img;
	FileLocation *file;
	bool access;
	QImageReader *reader;
	int32 w, h, frame;

private:

	QVector<QPixmap> frames;
	QVector<QImage> images;
	QVector<int64> delays;
	int32 framesCount, duration;

	Animation _a_frames;

};

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

class ClipReaderPrivate;
class ClipReader {
public:

	ClipReader(const FileLocation &location, const QByteArray &data);

	void start(int32 framew, int32 frameh, int32 outerw, int32 outerh, bool rounded);
	QPixmap current(int32 framew, int32 frameh, int32 outerw, int32 outerh, uint64 ms);
	bool currentDisplayed() const {
		return _currentDisplayed.loadAcquire() > 0;
	}

	int32 width() const;
	int32 height() const;

	ClipState state() const;
	bool started() const {
		return _request.valid();
	}
	bool ready() const;

	void stop();
	void error();

	~ClipReader();

private:

	ClipState _state;

	ClipFrameRequest _request;

	mutable int32 _width, _height;

	QPixmap _current;
	QImage _currentOriginal, _cacheForResize;
	QAtomicInt _currentDisplayed;
	uint64 _lastDisplayMs;
	int32 _threadIndex;

	friend class ClipReadManager;

	ClipReaderPrivate *_private;

};

enum ClipProcessResult {
	ClipProcessError,
	ClipProcessStarted,
	ClipProcessReinit,
	ClipProcessRedraw,
	ClipProcessWait,
};

class ClipReadManager : public QObject {
	Q_OBJECT

public:

	ClipReadManager(QThread *thread);
	int32 loadLevel() const {
		return _loadLevel.loadAcquire();
	}
	void append(ClipReader *reader, const FileLocation &location, const QByteArray &data);
	void start(ClipReader *reader);
	void update(ClipReader *reader);
	void stop(ClipReader *reader);

signals:

	void processDelayed();

	void reinit(ClipReader *reader);
	void redraw(ClipReader *reader);

public slots:

	void process();

private:

	QAtomicInt _loadLevel;
	typedef QMap<ClipReader*, ClipReaderPrivate*> ReaderPointers;
	ReaderPointers _readerPointers;
	QMutex _readerPointersMutex;

	bool handleProcessResult(ClipReaderPrivate *reader, ClipProcessResult result);

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

};
