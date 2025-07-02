/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/algorithm.h"
#include "base/basic_types.h"

class MTPstarsAmount;

namespace tl {
template <typename bare>
class boxed;
} // namespace tl

using MTPStarsAmount = tl::boxed<MTPstarsAmount>;

inline constexpr auto kOneStarInNano = int64(1'000'000'000);

enum class CreditsType {
	Stars,
	Ton,
};

class CreditsAmount {
public:
	CreditsAmount() = default;
	explicit CreditsAmount(
		int64 whole,
		CreditsType type = CreditsType::Stars)
	: _ton((type == CreditsType::Ton) ? 1 : 0)
	, _whole(whole) {
	}
	CreditsAmount(
		int64 whole,
		int64 nano,
		CreditsType type = CreditsType::Stars)
	: _ton((type == CreditsType::Ton) ? 1 : 0)
	, _whole(whole)
	, _nano(nano) {
		normalize();
	}

	[[nodiscard]] int64 whole() const {
		return _whole;
	}

	[[nodiscard]] int64 nano() const {
		return _nano;
	}

	[[nodiscard]] double value() const {
		return double(_whole) + double(_nano) / kOneStarInNano;
	}

	[[nodiscard]] bool ton() const {
		return (_ton == 1);
	}
	[[nodiscard]] bool stars() const {
		return (_ton == 0);
	}
	[[nodiscard]] CreditsType type() const {
		return !_ton ? CreditsType::Stars : CreditsType::Ton;
	}

	[[nodiscard]] bool empty() const {
		return !_whole && !_nano;
	}

	[[nodiscard]] inline bool operator!() const {
		return empty();
	}
	[[nodiscard]] inline explicit operator bool() const {
		return !empty();
	}

	[[nodiscard]] CreditsAmount multiplied(float64 rate) const {
		const auto result = value() * rate;
		const auto abs = std::abs(result);
		const auto whole = std::floor(abs);
		const auto nano = base::SafeRound((abs - whole) * kOneStarInNano);
		return CreditsAmount(
			(result < 0) ? -whole : whole,
			(result < 0) ? -nano : nano,
			type());
	}

	inline CreditsAmount &operator+=(CreditsAmount other) {
		_whole += other._whole;
		_nano += other._nano;
		normalize();
		return *this;
	}
	inline CreditsAmount &operator-=(CreditsAmount other) {
		_whole -= other._whole;
		_nano -= other._nano;
		normalize();
		return *this;
	}
	inline CreditsAmount &operator*=(int64 multiplier) {
		_whole *= multiplier;
		_nano *= multiplier;
		normalize();
		return *this;
	}
	inline CreditsAmount operator-() const {
		auto result = *this;
		result *= -1;
		return result;
	}

// AppleClang :/
//	friend inline auto operator<=>(CreditsAmount, CreditsAmount)
//		= default;
	friend inline constexpr auto operator<=>(
			CreditsAmount a,
			CreditsAmount b) {
		if (const auto r1 = (a._whole <=> b._whole); r1 != 0) {
			return r1;
		} else if (const auto r2 = (a._nano <=> b._nano); r2 != 0) {
			return r2;
		}
		return (a._whole || a._nano)
			? (int(a._ton) <=> int(b._ton))
			: std::strong_ordering::equal;
	}

	friend inline bool operator==(CreditsAmount, CreditsAmount)
		= default;

	[[nodiscard]] CreditsAmount abs() const {
		return (_whole < 0) ? CreditsAmount(-_whole, -_nano) : *this;
	}

private:
	void normalize() {
		if (_nano < 0) {
			const auto shifts = (-_nano + kOneStarInNano - 1)
				/ kOneStarInNano;
			_nano += shifts * kOneStarInNano;
			_whole -= shifts;
		} else if (_nano >= kOneStarInNano) {
			const auto shifts = _nano / kOneStarInNano;
			_nano -= shifts * kOneStarInNano;
			_whole += shifts;
		}
	}

	int64 _ton : 2 = 0;
	int64 _whole : 62 = 0;
	int64 _nano = 0;

};

[[nodiscard]] inline CreditsAmount operator+(
		CreditsAmount a,
		CreditsAmount b) {
	return a += b;
}

[[nodiscard]] inline CreditsAmount operator-(
		CreditsAmount a,
		CreditsAmount b) {
	return a -= b;
}

[[nodiscard]] inline CreditsAmount operator*(CreditsAmount a, int64 b) {
	return a *= b;
}

[[nodiscard]] inline CreditsAmount operator*(int64 a, CreditsAmount b) {
	return b *= a;
}

[[nodiscard]] CreditsAmount CreditsAmountFromTL(
	const MTPStarsAmount &amount);
[[nodiscard]] CreditsAmount CreditsAmountFromTL(
	const MTPStarsAmount *amount);
[[nodiscard]] MTPStarsAmount StarsAmountToTL(CreditsAmount amount);
