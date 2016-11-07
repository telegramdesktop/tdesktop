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
#include "core/lambda_wrap.h"
#include <QtCore/QTimer>
#include <QtGui/QColor>

namespace Media {
namespace Clip {

class Reader;
class ReaderPointer {
public:
	ReaderPointer(std_::nullptr_t = nullptr) {
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
	~ReaderPointer();

private:
	Reader *_pointer = nullptr;
	static Reader *const BadPointer;

};

class Manager;

enum Notification {
	NotificationReinit,
	NotificationRepaint,
};

} // namespace Clip
} // namespace Media

namespace anim {

using transition = base::lambda_wrap<float64(float64 delta, float64 dt)>;

extern transition linear;
extern transition sineInOut;
extern transition halfSine;
extern transition easeOutBack;
extern transition easeInCirc;
extern transition easeOutCirc;
extern transition easeInCubic;
extern transition easeOutCirc;
extern transition easeInQuint;
extern transition easeOutQuint;

inline transition bumpy(float64 bump) {
	auto dt0 = (bump - sqrt(bump * (bump - 1.)));
	auto k = (1 / (2 * dt0 - 1));
	return [bump, dt0, k](float64 delta, float64 dt) {
		return delta * (bump - k * (dt - dt0) * (dt - dt0));
	};
}

// Basic animated value.
class value {
public:
	using ValueType = float64;

	value() = default;
	value(float64 from) : _cur(from), _from(from) {
	}
	value(float64 from, float64 to) : _cur(from), _from(from), _delta(to - from) {
	}
	void start(float64 to) {
		_from = _cur;
		_delta = to - _from;
	}
	void restart() {
		_delta = _from + _delta - _cur;
		_from = _cur;
	}

	float64 from() const {
		return _from;
	}
	float64 current() const {
		return _cur;
	}
	float64 to() const {
		return _from + _delta;
	}
	void add(float64 delta) {
		_from += delta;
		_cur += delta;
	}
	value &update(float64 dt, const transition &func) {
		_cur = _from + func(_delta, dt);
		return *this;
	}
	void finish() {
		_cur = _from + _delta;
		_from = _cur;
		_delta = 0;
	}

private:
	float64 _cur = 0.;
	float64 _from = 0.;
	float64 _delta = 0.;

};

using fvalue = value;

class ivalue { // int animated value
public:
	using ValueType = int;

	ivalue() = default;
	ivalue(int from) : _cur(from), _from(float64(from)) {
	}
	ivalue(int from, int to) : _cur(from), _from(float64(from)), _delta(float64(to - from)) {
	}
	void start(int32 to) {
		_from = float64(_cur);
		_delta = float64(to) - _from;
	}
	void restart() {
		_delta = _from + _delta - float64(_cur);
		_from = float64(_cur);
	}

	int from() const {
		return _from;
	}
	int current() const {
		return _cur;
	}
	int to() const {
		return qRound(_from + _delta);
	}
	void add(int delta) {
		_from += delta;
		_cur += delta;
	}
	ivalue &update(float64 dt, const transition &func) {
		_cur = qRound(_from + func(_delta, dt));
		return *this;
	}
	void finish() {
		_cur = qRound(_from + _delta);
		_from = _cur;
		_delta = 0;
	}

private:
	int _cur = 0;
	float64 _from = 0.;
	float64 _delta = 0.;

};

void startManager();
void stopManager();
void registerClipManager(Media::Clip::Manager *manager);

inline int interpolate(int a, int b, float64 b_ratio) {
	return qRound(a + float64(b - a) * b_ratio);
}

inline QColor color(QColor a, QColor b, float64 b_ratio) {
	auto bOpacity = snap(interpolate(0, 255, b_ratio), 0, 255) + 1;
	auto aOpacity = (256 - bOpacity);
	return {
		(a.red() * aOpacity + b.red() * bOpacity) >> 8,
		(a.green() * aOpacity + b.green() * bOpacity) >> 8,
		(a.blue() * aOpacity + b.blue() * bOpacity) >> 8,
		(a.alpha() * aOpacity + b.alpha() * bOpacity) >> 8
	};
}

inline QColor color(const style::color &a, QColor b, float64 b_ratio) {
	return color(a->c, b, b_ratio);
}

inline QColor color(QColor a, const style::color &b, float64 b_ratio) {
	return color(a, b->c, b_ratio);
}

inline QColor color(const style::color &a, const style::color &b, float64 b_ratio) {
	return color(a->c, b->c, b_ratio);
}

inline QPen pen(QColor a, QColor b, float64 b_ratio) {
	return color(a, b, b_ratio);
}

inline QPen pen(const style::color &a, QColor b, float64 b_ratio) {
	return (b_ratio > 0) ? pen(a->c, b, b_ratio) : a;
}

inline QPen pen(QColor a, const style::color &b, float64 b_ratio) {
	return (b_ratio < 1) ? pen(a, b->c, b_ratio) : b;
}

inline QPen pen(const style::color &a, const style::color &b, float64 b_ratio) {
	return (b_ratio > 0) ? ((b_ratio < 1) ? pen(a->c, b->c, b_ratio) : b) : a;
}

inline QBrush brush(QColor a, QColor b, float64 b_ratio) {
	return color(a, b, b_ratio);
}

inline QBrush brush(const style::color &a, QColor b, float64 b_ratio) {
	return (b_ratio > 0) ? brush(a->c, b, b_ratio) : a;
}

inline QBrush brush(QColor a, const style::color &b, float64 b_ratio) {
	return (b_ratio < 1) ? brush(a, b->c, b_ratio) : b;
}

inline QBrush brush(const style::color &a, const style::color &b, float64 b_ratio) {
	return (b_ratio > 0) ? ((b_ratio < 1) ? brush(a->c, b->c, b_ratio) : b) : a;
}

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
	~AnimationCallbacks() { delete base::take(_implementation); }

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
	using ValueType = typename AnimType::ValueType;
	using Callback = base::lambda_unique<void()>;

	void step(uint64 ms) {
		if (_data) {
			_data->a_animation.step(ms);
			if (_data && !_data->a_animation.animating()) {
				_data.reset();
			}
		}
	}

	bool animating() const {
		if (_data) {
			if (_data->a_animation.animating()) {
				return true;
			}
			_data.reset();
		}
		return false;
	}
	bool animating(uint64 ms) {
		step(ms);
		return animating();
	}

	ValueType current() const {
		t_assert(_data != nullptr);
		return _data->value.current();
	}
	ValueType current(const ValueType &def) const {
		return _data ? current() : def;
	}
	ValueType current(uint64 ms, const ValueType &def) {
		return animating(ms) ? current() : def;
	}

	template <typename Lambda>
	void start(Lambda &&updateCallback, const ValueType &from, const ValueType &to, float64 duration, anim::transition transition = anim::linear) {
		if (!_data) {
			_data = std_::make_unique<Data>(from, std_::forward<Lambda>(updateCallback));
		}
		_data->value.start(to);
		_data->duration = duration;
		_data->transition = transition;
		_data->a_animation.start();
	}

	void finish() {
		if (_data) {
			_data->value.finish();
			_data->a_animation.stop();
			_data.reset();
		}
	}

private:
	struct Data {
		template <typename Lambda, typename = std_::enable_if_t<std_::is_rvalue_reference<Lambda&&>::value>>
		Data(const ValueType &from, Lambda &&updateCallback)
			: value(from, from)
			, a_animation(animation(this, &Data::step))
			, updateCallback(std_::move(updateCallback)) {
		}
		Data(const ValueType &from, const base::lambda_wrap<void()> &updateCallback)
			: value(from, from)
			, a_animation(animation(this, &Data::step))
			, updateCallback(base::lambda_wrap<void()>(updateCallback)) {
		}
		void step(float64 ms, bool timer) {
			auto dt = (ms >= duration) ? 1. : (ms / duration);
			if (dt >= 1) {
				value.finish();
				a_animation.stop();
			} else {
				value.update(dt, transition);
			}
			updateCallback();
		}

		AnimType value;
		Animation a_animation;
		Callback updateCallback;
		float64 duration = 0.;
		anim::transition transition = anim::linear;
	};
	mutable std_::unique_ptr<Data> _data;

};

using FloatAnimation = SimpleAnimation<anim::fvalue>;
using IntAnimation = SimpleAnimation<anim::ivalue>;

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
	using AnimatingObjects = OrderedSet<Animation*>;
	AnimatingObjects _objects, _starting, _stopping;
	QTimer _timer;
	bool _iterating;

};
