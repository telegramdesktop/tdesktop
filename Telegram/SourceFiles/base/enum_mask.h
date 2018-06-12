/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace base {

template <typename Enum>
class enum_mask {
	using Type = std::uint32_t;

public:
	static_assert(static_cast<int>(Enum::kCount) <= 32, "We have only 32 bit.");

	enum_mask() = default;
	enum_mask(Enum value) : _value(ToBit(value)) {
	}

	enum_mask added(enum_mask other) const {
		auto result = *this;
		result.set(other);
		return result;
	}
	void set(enum_mask other) {
		_value |= other._value;
	}
	bool test(Enum value) const {
		return _value & ToBit(value);
	}

	explicit operator bool() const {
		return _value != 0;
	}

private:
	inline static Type ToBit(Enum value) {
		return 1 << static_cast<Type>(value);
	}
	Type _value = 0;

};

} // namespace base
