/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Countries {
enum class Naming : uchar;
} // namespace Countries

namespace Ui {

class GenericBox;

void SelectCountriesBox(
	not_null<Ui::GenericBox*> box,
	const std::vector<QString> &selected,
	Fn<void(std::vector<QString>)> doneCallback,
	Fn<bool(int)> checkErrorCallback,
	Countries::Naming naming);

} // namespace Ui
