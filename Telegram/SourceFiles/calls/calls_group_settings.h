/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/generic_box.h"

namespace Calls {

class GroupCall;

void GroupCallSettingsBox(
	not_null<Ui::GenericBox*> box,
	not_null<GroupCall*> call);

} // namespace Calls
