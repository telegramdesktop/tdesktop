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

	template <int BumpRatioNumerator, int BumpRatioDenominator>
	float64 bumpy(const float64 &delta, const float64 &dt) {
		struct Bumpy {
			Bumpy()
				: bump(BumpRatioNumerator / float64(BumpRatioDenominator))
				, dt0(bump - sqrt(bump * (bump - 1.)))
				, k(1 / (2 * dt0 - 1)) {
			}
			float64 bump;
			float64 dt0;
			float64 k;
		};
		static Bumpy data;
		return delta * (data.bump - data.k * (dt - data.dt0) * (dt - data.dt0));
	}

	class fvalue { // float animated value
	public:
		using ValueType = float64;

		fvalue() = default;
		fvalue(float64 from) : _cur(from), _from(from) {
		}
		fvalue(float64 from, float64 to) : _cur(from), _from(from), _delta(to - from) {
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
		fvalue &update(float64 dt, transition func) {
			_cur = _from + (*func)(_delta, dt);
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
		ivalue &update(float64 dt, transition func) {
			_cur = qRound(_from + (*func)(_delta, dt));
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

	class cvalue { // QColor animated value
	public:
		using ValueType = QColor;

		cvalue() = default;
		explicit cvalue(QColor from)
			: _cur(from)
			, _from_r(from.redF())
			, _from_g(from.greenF())
			, _from_b(from.blueF())
			, _from_a(from.alphaF()) {
		}
		cvalue(QColor from, QColor to)
			: _cur(from)
			, _from_r(from.redF())
			, _from_g(from.greenF())
			, _from_b(from.blueF())
			, _from_a(from.alphaF())
			, _delta_r(to.redF() - from.redF())
			, _delta_g(to.greenF() - from.greenF())
			, _delta_b(to.blueF() - from.blueF())
			, _delta_a(to.alphaF() - from.alphaF()) {
		}
		void start(QColor to) {
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
		QColor from() const {
			QColor result;
			result.setRedF(_from_r);
			result.setGreenF(_from_g);
			result.setBlueF(_from_b);
			result.setAlphaF(_from_a);
			return result;
		}
		QColor current() const {
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
		float64 _from_r = 0.;
		float64 _from_g = 0.;
		float64 _from_b = 0.;
		float64 _from_a = 0.;
		float64 _delta_r = 0.;
		float64 _delta_g = 0.;
		float64 _delta_b = 0.;
		float64 _delta_a = 0.;

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
using ColorAnimation = SimpleAnimation<anim::cvalue>;

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
