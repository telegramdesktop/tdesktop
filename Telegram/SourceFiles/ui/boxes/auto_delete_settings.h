/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/generic_box.h"

namespace Ui {

void AutoDeleteSettingsBox(
	not_null<Ui::GenericBox*> box,
	TimeId ttlPeriod,
	rpl::producer<QString> about,
	Fn<void(TimeId)> callback);

} // namespace Ui
