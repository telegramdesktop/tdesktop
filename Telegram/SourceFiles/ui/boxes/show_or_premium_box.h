/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

class GenericBox;

enum class ShowOrPremium : uchar {
	LastSeen,
	ReadTime,
};
void ShowOrPremiumBox(
	not_null<GenericBox*> box,
	ShowOrPremium type,
	QString shortName,
	Fn<void()> justShow,
	Fn<void()> toPremium);

} // namespace Ui
