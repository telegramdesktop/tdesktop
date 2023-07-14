/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/xmdnx/exteraGramDesktop/blob/dev/LEGAL
*/
#include "ui/power_saving.h"

namespace PowerSaving {
namespace {

Flags Data/* = {}*/;
rpl::event_stream<> Events;
bool AllForced/* = false*/;

} // namespace

void Set(Flags flags) {
	if (const auto diff = Data ^ flags) {
		Data = flags;
		if (!AllForced) {
			if (diff & kAnimations) {
				anim::SetDisabled(On(kAnimations));
			}
			Events.fire({});
		}
	}
}

Flags Current() {
	return Data;
}

void SetForceAll(bool force) {
	if (AllForced == force) {
		return;
	}
	AllForced = force;
	if (const auto diff = Data ^ kAll) {
		if (diff & kAnimations) {
			anim::SetDisabled(On(kAnimations));
		}
		Events.fire({});
	}
}

bool ForceAll() {
	return AllForced;
}

rpl::producer<> Changes() {
	return Events.events();
}

} // namespace PowerSaving
