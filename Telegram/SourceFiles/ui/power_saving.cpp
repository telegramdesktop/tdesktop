/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/power_saving.h"

namespace PowerSaving {
namespace {

Flags Data/* = {}*/;
rpl::event_stream<> Events;

} // namespace

Flags Current() {
	return Data;
}

void Set(Flags flags) {
	if (const auto diff = Data ^ flags) {
		Data = flags;
		if (diff & kAnimations) {
			anim::SetDisabled(On(kAnimations));
		}
		Events.fire({});
	}
}

rpl::producer<Flags> Changes() {
	return Events.events() | rpl::map(Current);
}

rpl::producer<Flags> Value() {
	return rpl::single(Current()) | rpl::then(Changes());
}

rpl::producer<bool> Value(Flag flag) {
	return Value() | rpl::map([=](Flags flags) {
		return (flags & flag) != 0;
	}) | rpl::distinct_until_changed();
}

} // namespace PowerSaving
