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
#pragma once

#include "core/basic_types.h"
#include <QtCore/QTimer>
#include <QtGui/QColor>

namespace Media {
namespace Clip {

class Reader;
static Reader * const BadReader = SharedMemoryLocation<Reader, 0>();

class Manager;

enum Notification {
	NotificationReinit,
	NotificationRepaint,
};

} // namespace Clip
} // namespace Media

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
	void registerClipManager(Media::Clip::Manager *manager);

};

class Animation;

class AnimationImplementation {
public:
	virtual void start() {}
	virtual void step(Animation *a, uint64 ms, bool timer) = 0;
	virtual ~AnimationImplementation() {}

};

class AnimationCallbacks {
public:
	AnimationCallbacks(AnimationImplementation *implementation) : _implementation(implementation) {}
	AnimationCallbacks(const AnimationCallbacks &other) = delete;
	AnimationCallbacks &operator=(const AnimationCallbacks &other) = delete;
	AnimationCallbacks(AnimationCallbacks &&other) : _implementation(other._implementation) {
		other._implementation = nullptr;
	}
	AnimationCallbacks &operator=(AnimationCallbacks &&other) {
		std::swap(_implementation, other._implementation);
		return *this;
	}

	void start() { _implementation->start();  }
	void step(Animation *a, uint64 ms, bool timer) { _implementation->step(a, ms, timer); }
	~AnimationCallbacks() { deleteAndMark(_implementation); }

private:
	AnimationImplementation *_implementation;

};

class Animation {
public:
	Animation(AnimationCallbacks &&callbacks)
		: _callbacks(std_::move(callbacks))
		, _animating(false) {
	}

	void start();
	void stop();

	void step(uint64 ms, bool timer = false) {
		_callbacks.step(this, ms, timer);
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
	AnimationCallbacks _callbacks;
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
AnimationCallbacks animation(Type *obj, typename AnimationCallbacksRelative<Type>::Method method) {
	return AnimationCallbacks(new AnimationCallbacksRelative<Type>(obj, method));
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
AnimationCallbacks animation(Type *obj, typename AnimationCallbacksAbsolute<Type>::Method method) {
	return AnimationCallbacks(new AnimationCallbacksAbsolute<Type>(obj, method));
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
AnimationCallbacks animation(Param param, Type *obj, typename AnimationCallbacksRelativeWithParam<Type, Param>::Method method) {
	return AnimationCallbacks(new AnimationCallbacksRelativeWithParam<Type, Param>(param, obj, method));
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
AnimationCallbacks animation(Param param, Type *obj, typename AnimationCallbacksAbsoluteWithParam<Type, Param>::Method method) {
	return AnimationCallbacks(new AnimationCallbacksAbsoluteWithParam<Type, Param>(param, obj, method));
}

template <typename AnimType>
class SimpleAnimation {
public:
	using Callback = Function<void>;

	SimpleAnimation() {
	}

	bool animating(uint64 ms) {
		if (_data && _data->_a.animating()) {
			_data->_a.step(ms);
			return _data && _data->_a.animating();
		}
		return false;
	}

	bool isNull() const {
		return !_data;
	}

	typename AnimType::Type current() {
		return _data ? _data->a.current() : typename AnimType::Type();
	}

	typename AnimType::Type current(const typename AnimType::Type &def) {
		return _data ? _data->a.current() : def;
	}

	typename AnimType::Type current(uint64 ms, const typename AnimType::Type &def) {
		return animating(ms) ? current() : def;
	}

	void setup(const typename AnimType::Type &from, Callback &&update) {
		if (!_data) {
			_data = new Data(from, std_::move(update), animation(this, &SimpleAnimation<AnimType>::step));
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

	void finish() {
		if (isNull()) {
			return;
		}

		_data->a.finish();
		_data->_a.stop();
		delete _data;
		_data = nullptr;
	}

	~SimpleAnimation() {
		deleteAndMark(_data);
	}

private:
	struct Data {
		Data(const typename AnimType::Type &from, Callback &&update, AnimationCallbacks &&acb)
			: a(from, from)
			, _a(std_::move(acb))
			, update(std_::move(update))
			, duration(0)
			, transition(anim::linear) {
		}
		AnimType a;
		Animation _a;
		Callback update;
		float64 duration;
		anim::transition transition;
	};
	Data *_data = nullptr;

	void step(float64 ms, bool timer) {
		float64 dt = (ms >= _data->duration) ? 1 : (ms / _data->duration);
		if (dt >= 1) {
			_data->a.finish();
			_data->_a.stop();
		} else {
			_data->a.update(dt, _data->transition);
		}

		Callback callbackCache, *toCall = &_data->update;
		if (!_data->_a.animating()) {
			callbackCache = std_::move(_data->update);
			toCall = &callbackCache;

			delete _data;
			_data = nullptr;
		}
		if (timer) {
			toCall->call();
		}
	}

};

using FloatAnimation = SimpleAnimation<anim::fvalue>;
using IntAnimation = SimpleAnimation<anim::ivalue>;
using ColorAnimation = SimpleAnimation<anim::cvalue>;

// Macro allows us to lazily create updateCallback.
#define ENSURE_ANIMATION(animation, updateCallback, from) \
if ((animation).isNull()) { \
	(animation).setup((from), (updateCallback)); \
}

#define START_ANIMATION(animation, updateCallback, from, to, duration, transition) \
ENSURE_ANIMATION(animation, updateCallback, from); \
(animation).start((to), (duration), (transition))

class AnimationManager : public QObject {
Q_OBJECT

public:
	AnimationManager();

	void start(Animation *obj);
	void stop(Animation *obj);

public slots:
	void timeout();

	void clipCallback(Media::Clip::Reader *reader, qint32 threadIndex, qint32 notification);

private:
	typedef QMap<Animation*, NullType> AnimatingObjects;
	AnimatingObjects _objects, _starting, _stopping;
	QTimer _timer;
	bool _iterating;

};
