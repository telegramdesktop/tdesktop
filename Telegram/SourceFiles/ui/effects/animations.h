/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
namespace Animations {

class Manager;

class Basic final {
public:
	Basic() = default;

	template <typename Callback>
	explicit Basic(Callback &&callback);

	template <typename Callback>
	void init(Callback &&callback);

	void start();
	void stop();

	[[nodiscard]] crl::time started() const;
	[[nodiscard]] bool animating() const;

	~Basic();

private:
	friend class Manager;

	template <typename Callback>
	[[nodiscard]] static Fn<bool(crl::time)> Prepare(Callback &&callback);

	[[nodiscard]] bool call(crl::time now) const;
	void restart();

	void markStarted();
	void markStopped();

	crl::time _started = -1;
	Fn<bool(crl::time)> _callback;

};

class Simple final {
public:
	template <typename Callback>
	void start(
		Callback &&callback,
		float64 from,
		float64 to,
		crl::time duration,
		anim::transition transition = anim::linear);
	void stop();
	[[nodiscard]] bool animating() const;
	[[nodiscard]] float64 value(float64 final) const;

private:
	struct Data {
		explicit Data(float64 initial) : value(initial) {
		}
		~Data() {
			if (markOnDelete) {
				*markOnDelete = true;
			}
		}

		Basic animation;
		anim::transition transition;
		float64 from = 0.;
		float64 delta = 0.;
		float64 value = 0.;
		float64 duration = 0.;
		bool *markOnDelete = nullptr;
		MTP::PauseHolder pause;
	};

	template <typename Callback>
	[[nodiscard]] static decltype(auto) Prepare(Callback &&callback);

	void prepare(float64 from, crl::time duration);
	void startPrepared(
		float64 to,
		crl::time duration,
		anim::transition transition);

	static constexpr auto kLongAnimationDuration = crl::time(1000);

	mutable std::unique_ptr<Data> _data;

};

class Manager final : private QObject {
public:
	Manager();

	void update();

private:
	class ActiveBasicPointer {
	public:
		ActiveBasicPointer(Basic *value = nullptr) : _value(value) {
			if (_value) {
				_value->markStarted();
			}
		}
		ActiveBasicPointer(ActiveBasicPointer &&other)
		: _value(base::take(other._value)) {
		}
		ActiveBasicPointer &operator=(ActiveBasicPointer &&other) {
			if (_value != other._value) {
				if (_value) {
					_value->markStopped();
				}
				_value = base::take(other._value);
			}
			return *this;
		}
		~ActiveBasicPointer() {
			if (_value) {
				_value->markStopped();
			}
		}

		[[nodiscard]] bool call(crl::time now) const {
			return _value && _value->call(now);
		}

		friend inline bool operator==(
				const ActiveBasicPointer &a,
				const ActiveBasicPointer &b) {
			return a._value == b._value;
		}

		Basic *get() const {
			return _value;
		}

	private:
		Basic *_value = nullptr;

	};

	friend class Basic;

	void timerEvent(QTimerEvent *e) override;

	void start(not_null<Basic*> animation);
	void stop(not_null<Basic*> animation);

	void schedule();
	void updateQueued();
	void stopTimer();

	crl::time _lastUpdateTime = 0;
	int _timerId = 0;
	bool _updating = false;
	bool _scheduled = false;
	std::vector<ActiveBasicPointer> _active;
	std::vector<ActiveBasicPointer> _starting;
	rpl::lifetime _lifetime;

};

template <typename Callback>
inline Fn<bool(crl::time)> Basic::Prepare(Callback &&callback) {
	if constexpr (rpl::details::is_callable_plain_v<Callback, crl::time>) {
		using Return = decltype(callback(crl::time(0)));
		if constexpr (std::is_convertible_v<Return, bool>) {
			return std::forward<Callback>(callback);
		} else if constexpr (std::is_same_v<Return, void>) {
			return [callback = std::forward<Callback>(callback)](
					crl::time time) {
				callback(time);
				return true;
			};
		} else {
			static_assert(false_t(callback), "Expected void or bool.");
		}
	} else if constexpr (rpl::details::is_callable_plain_v<Callback>) {
		using Return = decltype(callback());
		if constexpr (std::is_convertible_v<Return, bool>) {
			return [callback = std::forward<Callback>(callback)](crl::time) {
				return callback();
			};
		} else if constexpr (std::is_same_v<Return, void>) {
			return [callback = std::forward<Callback>(callback)](crl::time) {
				callback();
				return true;
			};
		} else {
			static_assert(false_t(callback), "Expected void or bool.");
		}
	} else {
		static_assert(false_t(callback), "Expected crl::time or no args.");
	}
}

template <typename Callback>
inline Basic::Basic(Callback &&callback)
: _callback(Prepare(std::forward<Callback>(callback))) {
}

template <typename Callback>
inline void Basic::init(Callback &&callback) {
	_callback = Prepare(std::forward<Callback>(callback));
}

TG_FORCE_INLINE crl::time Basic::started() const {
	return _started;
}

TG_FORCE_INLINE bool Basic::animating() const {
	return (_started >= 0);
}

TG_FORCE_INLINE bool Basic::call(crl::time now) const {
	const auto onstack = _callback;
	return onstack(now);
}

inline Basic::~Basic() {
	stop();
}

template <typename Callback>
decltype(auto) Simple::Prepare(Callback &&callback) {
	if constexpr (rpl::details::is_callable_plain_v<Callback, float64>) {
		using Return = decltype(callback(float64(0.)));
		if constexpr (std::is_convertible_v<Return, bool>) {
			return std::forward<Callback>(callback);
		} else if constexpr (std::is_same_v<Return, void>) {
			return [callback = std::forward<Callback>(callback)](
					float64 value) {
				callback(value);
				return true;
			};
		} else {
			static_assert(false_t(callback), "Expected void or float64.");
		}
	} else if constexpr (rpl::details::is_callable_plain_v<Callback>) {
		using Return = decltype(callback());
		if constexpr (std::is_convertible_v<Return, bool>) {
			return [callback = std::forward<Callback>(callback)](float64) {
				return callback();
			};
		} else if constexpr (std::is_same_v<Return, void>) {
			return [callback = std::forward<Callback>(callback)](float64) {
				callback();
				return true;
			};
		} else {
			static_assert(false_t(callback), "Expected void or bool.");
		}
	} else {
		static_assert(false_t(callback), "Expected float64 or no args.");
	}
}

template <typename Callback>
inline void Simple::start(
		Callback &&callback,
		float64 from,
		float64 to,
		crl::time duration,
		anim::transition transition) {
	prepare(from, duration);
	_data->animation.init([
		that = _data.get(),
		callback = Prepare(std::forward<Callback>(callback))
	](crl::time now) {
		const auto time = (now - that->animation.started());
		const auto finished = (time >= that->duration);
		const auto progress = finished
			? that->delta
			: that->transition(that->delta, time / that->duration);
		that->value = that->from + progress;

		if (finished) {
			that->animation.stop();
		}

		auto deleted = false;
		that->markOnDelete = &deleted;
		const auto result = callback(that->value) && !finished;
		if (!deleted) {
			that->markOnDelete = nullptr;
			if (!result) {
				that->pause.release();
			}
		}
		return result;
	});
	startPrepared(to, duration, transition);
}

inline void Simple::prepare(float64 from, crl::time duration) {
	const auto isLong = (duration > kLongAnimationDuration);
	if (!_data) {
		_data = std::make_unique<Data>(from);
	} else if (!isLong) {
		_data->pause.restart();
	}
	if (isLong) {
		_data->pause.release();
	}
}

inline void Simple::stop() {
	_data = nullptr;
}

inline bool Simple::animating() const {
	if (!_data) {
		return false;
	} else if (!_data->animation.animating()) {
		_data = nullptr;
		return false;
	}
	return true;
}

TG_FORCE_INLINE float64 Simple::value(float64 final) const {
	return animating() ? _data->value : final;
}

inline void Simple::startPrepared(
		float64 to,
		crl::time duration,
		anim::transition transition) {
	_data->from = _data->value;
	_data->delta = to - _data->from;
	_data->duration = duration;
	_data->transition = transition;
	_data->animation.start();
}

} // namespace Animations
} // namespace Ui
