/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/generic_box.h"

namespace style {
struct FlatLabel;
struct InputField;
struct CalendarColors;
} // namespace style

namespace Ui {

class RoundButton;

struct ChooseDateTimeBoxDescriptor {
	QPointer<RoundButton> submit;
	Fn<TimeId()> collect;
	rpl::producer<TimeId> values;
};

struct ChooseDateTimeStyleArgs {
	ChooseDateTimeStyleArgs();
	const style::FlatLabel *labelStyle;
	const style::InputField *dateFieldStyle;
	const style::InputField *timeFieldStyle;
	const style::FlatLabel *separatorStyle;
	const style::FlatLabel *atStyle;
	const style::CalendarColors *calendarStyle;
};

struct ChooseDateTimeBoxArgs {
	rpl::producer<QString> title;
	rpl::producer<QString> submit;
	Fn<void(TimeId)> done;
	Fn<TimeId()> min;
	TimeId time = 0;
	Fn<TimeId()> max;
	rpl::producer<QString> description;
	ChooseDateTimeStyleArgs style;
};

ChooseDateTimeBoxDescriptor ChooseDateTimeBox(
	not_null<GenericBox*> box,
	ChooseDateTimeBoxArgs &&args);

} // namespace Ui
