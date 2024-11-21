/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

inline constexpr auto kOneStarInNano = int64(1'000'000'000);

class StarsAmount {
public:
    StarsAmount() = default;
    explicit StarsAmount(int64 whole) : _whole(whole) {}
    StarsAmount(int64 whole, int64 nano) : _whole(whole), _nano(nano) {
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

    [[nodiscard]] bool empty() const {
        return !_whole && !_nano;
    }

	[[nodiscard]] inline bool operator!() const {
		return empty();
	}
    [[nodiscard]] inline explicit operator bool() const {
		return !empty();
    }

    inline StarsAmount &operator+=(StarsAmount other) {
        _whole += other._whole;
        _nano += other._nano;
        normalize();
        return *this;
    }
    inline StarsAmount &operator-=(StarsAmount other) {
        _whole -= other._whole;
        _nano -= other._nano;
        normalize();
        return *this;
    }
    inline StarsAmount &operator*=(int64 multiplier) {
        _whole *= multiplier;
        _nano *= multiplier;
        normalize();
        return *this;
    }

    friend inline auto operator<=>(StarsAmount, StarsAmount) = default;
    friend inline bool operator==(StarsAmount, StarsAmount) = default;

    [[nodiscard]] StarsAmount abs() const {
		return (_whole < 0) ? StarsAmount(-_whole, -_nano) : *this;
    }

private:
    int64 _whole = 0;
    int64 _nano = 0;

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
};

[[nodiscard]] inline StarsAmount operator+(StarsAmount a, StarsAmount b) {
    return a += b;
}

[[nodiscard]] inline StarsAmount operator-(StarsAmount a, StarsAmount b) {
    return a -= b;
}

[[nodiscard]] inline StarsAmount operator*(StarsAmount a, int64 b) {
    return a *= b;
}
