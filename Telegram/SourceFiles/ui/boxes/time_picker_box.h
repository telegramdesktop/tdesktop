/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace Ui {

class GenericBox;

[[nodiscard]] std::vector<TimeId> DefaultTimePickerValues();

[[nodiscard]] Fn<TimeId()> TimePickerBox(
	not_null<GenericBox*> box,
	std::vector<TimeId> values,
	std::vector<QString> phrases,
	TimeId startValue);

} // namespace Ui
