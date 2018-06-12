/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
