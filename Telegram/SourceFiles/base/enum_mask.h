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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
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
