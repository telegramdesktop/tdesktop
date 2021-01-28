/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/generic_box.h"

namespace Ui {

class RoundButton;

struct ChooseDateTimeBoxDescriptor {
	QPointer<RoundButton> submit;
	Fn<TimeId()> collect;
};

ChooseDateTimeBoxDescriptor ChooseDateTimeBox(
	not_null<GenericBox*> box,
	rpl::producer<QString> title,
	rpl::producer<QString> submit,
	Fn<void(TimeId)> done,
	TimeId time);

} // namespace Ui
