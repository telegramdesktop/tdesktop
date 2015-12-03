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

class Animated;

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

	void start(Animated *obj);
	void step(Animated *obj);
	void stop(Animated *obj);

	void startManager();
	void stopManager();

};

class Animated {
public:

	Animated() : animStarted(0), animInProcess(false) {
	}

	virtual bool animStep(float64 ms) = 0;

	void animReset() {
		animStarted = float64(getms());
	}

	virtual ~Animated() {
		if (animating()) {
			anim::stop(this);
		}
	}

	bool animating() const {
		return animInProcess;
	}

private:

	float64 animStarted;
	bool animInProcess;
	friend class AnimationManager;

};

class AnimationFunc {
public:
	virtual bool animStep(float64 ms) = 0;
	virtual ~AnimationFunc() {
	}
};

template <typename Type>
class AnimationFuncOwned : public AnimationFunc {
public:
	typedef bool (Type::*Method)(float64);

	AnimationFuncOwned(Type *obj, Method method) : _obj(obj), _method(method) {
	}

	bool animStep(float64 ms) {
		return (_obj->*_method)(ms);
	}
	
private:
	Type *_obj;
	Method _method;
	
};

template <typename Type>
AnimationFunc *animFunc(Type *obj, typename AnimationFuncOwned<Type>::Method method) {
	return new AnimationFuncOwned<Type>(obj, method);
}

class Animation : public Animated {
public:

	Animation(AnimationFunc *func) : _func(func) {
	}

	void start() {
		anim::start(this);
	}
	void stop() {
		anim::stop(this);
	}

	//Animation
	bool animStep(float64 ms) {
		return _func->animStep(ms);
	}

	~Animation() {
		delete _func;
	}

private:
	AnimationFunc *_func;

};

class AnimationManager : public QObject {
Q_OBJECT

public:

	AnimationManager() : timer(this), iterating(false) {
		timer.setSingleShot(false);
		connect(&timer, SIGNAL(timeout()), this, SLOT(timeout()));
	}

	void start(Animated *obj) {
		obj->animReset();
		if (iterating) {
			toStart.insert(obj);
			if (!toStop.isEmpty()) {
				toStop.remove(obj);
			}
		} else {
			if (!objs.size()) {
				timer.start(AnimationTimerDelta);
			}
			objs.insert(obj);
		}
		obj->animInProcess = true;
	}

	void step(Animated *obj) {
		if (iterating) return;

		float64 ms = float64(getms());
		AnimObjs::iterator i = objs.find(obj);
		if (i != objs.cend()) {
			Animated *obj = *i;
			if (!obj->animStep(ms - obj->animStarted)) {
				objs.erase(i);
				if (!objs.size()) {
					timer.stop();
				}
				obj->animInProcess = false;
			}
		}
	}

	void stop(Animated *obj) {
		if (iterating) {
			toStop.insert(obj);
			if (!toStart.isEmpty()) {
				toStart.insert(obj);
			}
		} else {
			AnimObjs::iterator i = objs.find(obj);
			if (i != objs.cend()) {
				objs.erase(i);
				if (!objs.size()) {
					timer.stop();
				}
			}
		}
		obj->animInProcess = false;
	}

public slots:
	void timeout() {
		iterating = true;
		float64 ms = float64(getms());
		for (AnimObjs::iterator i = objs.begin(), e = objs.end(); i != e; ) {
			Animated *obj = *i;
			if (!obj->animStep(ms - obj->animStarted)) {
				i = objs.erase(i);
				obj->animInProcess = false;
			} else {
				++i;
			}
		}
		iterating = false;
		if (!toStart.isEmpty()) {
			for (AnimObjs::iterator i = toStart.begin(), e = toStart.end(); i != e; ++i) {
				objs.insert(*i);
			}
			toStart.clear();
		}
		if (!toStop.isEmpty()) {
			for (AnimObjs::iterator i = toStop.begin(), e = toStop.end(); i != e; ++i) {
				objs.remove(*i);
			}
			toStop.clear();
		}
		if (!objs.size()) {
			timer.stop();
		}
	}

private:

	typedef QSet<Animated*> AnimObjs;
	AnimObjs objs;
	AnimObjs toStart;
	AnimObjs toStop;
	QTimer timer;
	bool iterating;

};

class HistoryItem;
class FileLocation;
class AnimatedGif : public QObject, public Animated {
	Q_OBJECT

public:

	AnimatedGif() : msg(0), file(0), access(false), reader(0), w(0), h(0), frame(0), framesCount(0), duration(0) {
	}

	bool animStep(float64 ms);

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
};
