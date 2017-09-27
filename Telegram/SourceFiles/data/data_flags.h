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

#include <rpl/event_stream.h>

namespace Data {

template <typename FlagsType>
using FlagsUnderlying = typename FlagsType::Type;

template <
	typename FlagsType,
	FlagsUnderlying<FlagsType> kEssential = FlagsUnderlying<FlagsType>(-1)>
class Flags {
public:
	using Type = FlagsType;
	using Enum = typename Type::Enum;

	struct Change {
		using Type = FlagsType;
		using Enum = typename Type::Enum;

		Change(Type diff, Type value)
		: diff(diff)
		, value(value) {
		}
		Type diff = 0;
		Type value = 0;
	};

	Flags() = default;
	Flags(Type value) : _value(value) {
	}

	void set(Type which) {
		if (auto diff = which ^ _value) {
			_value = which;
			updated(diff);
		}
	}
	void add(Type which) {
		if (auto diff = which & ~_value) {
			_value |= which;
			updated(diff);
		}
	}
	void remove(Type which) {
		if (auto diff = which & _value) {
			_value &= ~which;
			updated(diff);
		}
	}
	auto current() const {
		return _value;
	}
	auto changes() const {
		return _changes.events();
	}
	auto value() const {
		return _changes.events_starting_with({
			Type::from_raw(kEssential),
			_value });
	}

private:
	void updated(Type diff) {
		if ((diff &= Type::from_raw(kEssential))) {
			_changes.fire({ diff, _value });
		}
	}

	Type _value = 0;
	rpl::event_stream<Change> _changes;

};

} // namespace Data
