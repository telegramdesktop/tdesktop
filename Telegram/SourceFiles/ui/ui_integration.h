/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

// Methods that must be implemented outside lib_ui.

namespace Ui {

void PostponeCall(FnMut<void()> &&callable);
void RegisterLeaveSubscription(not_null<QWidget*> widget);
void UnregisterLeaveSubscription(not_null<QWidget*> widget);

} // namespace Ui
