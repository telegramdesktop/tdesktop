/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
class Birthday;
} // namespace Data

namespace Ui {

class GenericBox;

void EditBirthdayBox(
	not_null<Ui::GenericBox*> box,
	Data::Birthday current,
	Fn<void(Data::Birthday)> save);

} // namespace Ui
