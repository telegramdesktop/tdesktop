/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include "ui/style/style_core.h"

namespace anim {

enum class type {
	normal,
	instant,
};

enum class activation {
	normal,
	background,
};

using transition = Fn<float64(float64 delta, float64 dt)>;

extern transition linear;
extern transition sineInOut;
extern transition halfSine;
extern transition easeOutBack;
extern transition easeInCirc;
extern transition easeOutCirc;
extern transition easeInCubic;
extern transition easeOutCubic;
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
	value &update(float64 dt, transition func) {
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

TG_FORCE_INLINE int interpolate(int a, int b, float64 b_ratio) {
	return qRound(a + float64(b - a) * b_ratio);
}

#ifdef ARCH_CPU_32_BITS
#define SHIFTED_USE_32BIT
#endif // ARCH_CPU_32_BITS

#ifdef SHIFTED_USE_32BIT

using ShiftedMultiplier = uint32;

struct Shifted {
	Shifted() = default;
	Shifted(uint32 low, uint32 high) : low(low), high(high) {
	}
	uint32 low = 0;
	uint32 high = 0;
};

TG_FORCE_INLINE Shifted operator+(Shifted a, Shifted b) {
	return Shifted(a.low + b.low, a.high + b.high);
}

TG_FORCE_INLINE Shifted operator*(Shifted shifted, ShiftedMultiplier multiplier) {
	return Shifted(shifted.low * multiplier, shifted.high * multiplier);
}

TG_FORCE_INLINE Shifted operator*(ShiftedMultiplier multiplier, Shifted shifted) {
	return Shifted(shifted.low * multiplier, shifted.high * multiplier);
}

TG_FORCE_INLINE Shifted shifted(uint32 components) {
	return Shifted(
		(components & 0x000000FFU) | ((components & 0x0000FF00U) << 8),
		((components & 0x00FF0000U) >> 16) | ((components & 0xFF000000U) >> 8));
}

TG_FORCE_INLINE uint32 unshifted(Shifted components) {
	return ((components.low & 0x0000FF00U) >> 8)
		| ((components.low & 0xFF000000U) >> 16)
		| ((components.high & 0x0000FF00U) << 8)
		| (components.high & 0xFF000000U);
}

TG_FORCE_INLINE Shifted reshifted(Shifted components) {
	return Shifted((components.low >> 8) & 0x00FF00FFU, (components.high >> 8) & 0x00FF00FFU);
}

TG_FORCE_INLINE Shifted shifted(QColor color) {
	// Make it premultiplied.
	auto alpha = static_cast<uint32>((color.alpha() & 0xFF) + 1);
	auto components = Shifted(static_cast<uint32>(color.blue() & 0xFF) | (static_cast<uint32>(color.green() & 0xFF) << 16),
		static_cast<uint32>(color.red() & 0xFF) | (static_cast<uint32>(255) << 16));
	return reshifted(components * alpha);
}

TG_FORCE_INLINE uint32 getPremultiplied(QColor color) {
	// Make it premultiplied.
	auto alpha = static_cast<uint32>((color.alpha() & 0xFF) + 1);
	auto components = Shifted(static_cast<uint32>(color.blue() & 0xFF) | (static_cast<uint32>(color.green() & 0xFF) << 16),
		static_cast<uint32>(color.red() & 0xFF) | (static_cast<uint32>(255) << 16));
	return unshifted(components * alpha);
}

TG_FORCE_INLINE uint32 getAlpha(Shifted components) {
	return (components.high & 0x00FF0000U) >> 16;
}

TG_FORCE_INLINE Shifted non_premultiplied(QColor color) {
	return Shifted(static_cast<uint32>(color.blue() & 0xFF) | (static_cast<uint32>(color.green() & 0xFF) << 16),
		static_cast<uint32>(color.red() & 0xFF) | (static_cast<uint32>(color.alpha() & 0xFF) << 16));
}

TG_FORCE_INLINE QColor color(QColor a, QColor b, float64 b_ratio) {
	auto bOpacity = std::clamp(interpolate(0, 255, b_ratio), 0, 255) + 1;
	auto aOpacity = (256 - bOpacity);
	auto components = (non_premultiplied(a) * aOpacity + non_premultiplied(b) * bOpacity);
	return {
		static_cast<int>((components.high >> 8) & 0xFF),
		static_cast<int>((components.low >> 24) & 0xFF),
		static_cast<int>((components.low >> 8) & 0xFF),
		static_cast<int>((components.high >> 24) & 0xFF),
	};
}

#else // SHIFTED_USE_32BIT

using ShiftedMultiplier = uint64;

struct Shifted {
	Shifted() = default;
	Shifted(uint32 value) : value(value) {
	}
	Shifted(uint64 value) : value(value) {
	}
	uint64 value = 0;
};

TG_FORCE_INLINE Shifted operator+(Shifted a, Shifted b) {
	return Shifted(a.value + b.value);
}

TG_FORCE_INLINE Shifted operator*(Shifted shifted, ShiftedMultiplier multiplier) {
	return Shifted(shifted.value * multiplier);
}

TG_FORCE_INLINE Shifted operator*(ShiftedMultiplier multiplier, Shifted shifted) {
	return Shifted(shifted.value * multiplier);
}

TG_FORCE_INLINE Shifted shifted(uint32 components) {
	auto wide = static_cast<uint64>(components);
	return (wide & 0x00000000000000FFULL)
		| ((wide & 0x000000000000FF00ULL) << 8)
		| ((wide & 0x0000000000FF0000ULL) << 16)
		| ((wide & 0x00000000FF000000ULL) << 24);
}

TG_FORCE_INLINE uint32 unshifted(Shifted components) {
	return static_cast<uint32>((components.value & 0x000000000000FF00ULL) >> 8)
		| static_cast<uint32>((components.value & 0x00000000FF000000ULL) >> 16)
		| static_cast<uint32>((components.value & 0x0000FF0000000000ULL) >> 24)
		| static_cast<uint32>((components.value & 0xFF00000000000000ULL) >> 32);
}

TG_FORCE_INLINE Shifted reshifted(Shifted components) {
	return (components.value >> 8) & 0x00FF00FF00FF00FFULL;
}

TG_FORCE_INLINE Shifted shifted(QColor color) {
	// Make it premultiplied.
	auto alpha = static_cast<uint64>((color.alpha() & 0xFF) + 1);
	auto components = static_cast<uint64>(color.blue() & 0xFF)
		| (static_cast<uint64>(color.green() & 0xFF) << 16)
		| (static_cast<uint64>(color.red() & 0xFF) << 32)
		| (static_cast<uint64>(255) << 48);
	return reshifted(components * alpha);
}

TG_FORCE_INLINE uint32 getPremultiplied(QColor color) {
	// Make it premultiplied.
	auto alpha = static_cast<uint64>((color.alpha() & 0xFF) + 1);
	auto components = static_cast<uint64>(color.blue() & 0xFF)
		| (static_cast<uint64>(color.green() & 0xFF) << 16)
		| (static_cast<uint64>(color.red() & 0xFF) << 32)
		| (static_cast<uint64>(255) << 48);
	return unshifted(components * alpha);
}

TG_FORCE_INLINE uint32 getAlpha(Shifted components) {
	return (components.value & 0x00FF000000000000ULL) >> 48;
}

TG_FORCE_INLINE Shifted non_premultiplied(QColor color) {
	return static_cast<uint64>(color.blue() & 0xFF)
		| (static_cast<uint64>(color.green() & 0xFF) << 16)
		| (static_cast<uint64>(color.red() & 0xFF) << 32)
		| (static_cast<uint64>(color.alpha() & 0xFF) << 48);
}

TG_FORCE_INLINE QColor color(QColor a, QColor b, float64 b_ratio) {
	auto bOpacity = std::clamp(interpolate(0, 255, b_ratio), 0, 255) + 1;
	auto aOpacity = (256 - bOpacity);
	auto components = (non_premultiplied(a) * aOpacity + non_premultiplied(b) * bOpacity);
	return {
		static_cast<int>((components.value >> 40) & 0xFF),
		static_cast<int>((components.value >> 24) & 0xFF),
		static_cast<int>((components.value >>  8) & 0xFF),
		static_cast<int>((components.value >> 56) & 0xFF),
	};
}

#endif // SHIFTED_USE_32BIT

TG_FORCE_INLINE QColor color(style::color a, QColor b, float64 b_ratio) {
	return color(a->c, b, b_ratio);
}

TG_FORCE_INLINE QColor color(QColor a, style::color b, float64 b_ratio) {
	return color(a, b->c, b_ratio);
}

TG_FORCE_INLINE QColor color(style::color a, style::color b, float64 b_ratio) {
	return color(a->c, b->c, b_ratio);
}

TG_FORCE_INLINE QPen pen(QColor a, QColor b, float64 b_ratio) {
	return color(a, b, b_ratio);
}

TG_FORCE_INLINE QPen pen(style::color a, QColor b, float64 b_ratio) {
	return (b_ratio > 0) ? pen(a->c, b, b_ratio) : a;
}

TG_FORCE_INLINE QPen pen(QColor a, style::color b, float64 b_ratio) {
	return (b_ratio < 1) ? pen(a, b->c, b_ratio) : b;
}

TG_FORCE_INLINE QPen pen(style::color a, style::color b, float64 b_ratio) {
	return (b_ratio > 0) ? ((b_ratio < 1) ? pen(a->c, b->c, b_ratio) : b) : a;
}

TG_FORCE_INLINE QBrush brush(QColor a, QColor b, float64 b_ratio) {
	return color(a, b, b_ratio);
}

TG_FORCE_INLINE QBrush brush(style::color a, QColor b, float64 b_ratio) {
	return (b_ratio > 0) ? brush(a->c, b, b_ratio) : a;
}

TG_FORCE_INLINE QBrush brush(QColor a, style::color b, float64 b_ratio) {
	return (b_ratio < 1) ? brush(a, b->c, b_ratio) : b;
}

TG_FORCE_INLINE QBrush brush(style::color a, style::color b, float64 b_ratio) {
	return (b_ratio > 0) ? ((b_ratio < 1) ? brush(a->c, b->c, b_ratio) : b) : a;
}

template <int N>
QPainterPath interpolate(QPointF (&from)[N], QPointF (&to)[N], float64 k) {
	static_assert(N > 1, "Wrong points count in path!");

	auto from_coef = 1. - k, to_coef = k;
	QPainterPath result;
	auto x = from[0].x() * from_coef + to[0].x() * to_coef;
	auto y = from[0].y() * from_coef + to[0].y() * to_coef;
	result.moveTo(x, y);
	for (int i = 1; i != N; ++i) {
		result.lineTo(from[i].x() * from_coef + to[i].x() * to_coef, from[i].y() * from_coef + to[i].y() * to_coef);
	}
	result.lineTo(x, y);
	return result;
}

template <int N>
QPainterPath path(QPointF (&from)[N]) {
	static_assert(N > 1, "Wrong points count in path!");

	QPainterPath result;
	auto x = from[0].x();
	auto y = from[0].y();
	result.moveTo(x, y);
	for (int i = 1; i != N; ++i) {
		result.lineTo(from[i].x(), from[i].y());
	}
	result.lineTo(x, y);
	return result;
}

bool Disabled();
void SetDisabled(bool disabled);

void DrawStaticLoading(
	QPainter &p,
	QRectF rect,
	int stroke,
	QPen pen,
	QBrush brush = Qt::NoBrush);

};
