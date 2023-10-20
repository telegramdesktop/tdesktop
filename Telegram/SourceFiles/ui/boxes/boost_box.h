/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

class GenericBox;
class VerticalLayout;

struct BoostCounters {
	int level = 0;
	int boosts = 0;
	int thisLevelBoosts = 0;
	int nextLevelBoosts = 0; // Zero means no next level is available.
	bool mine = false;
};

struct BoostBoxData {
	QString name;
	BoostCounters boost;
};

void BoostBox(
	not_null<GenericBox*> box,
	BoostBoxData data,
	Fn<void(Fn<void(bool)>)> boost);

void FillBoostLimit(
	rpl::producer<> showFinished,
	rpl::producer<bool> you,
	not_null<VerticalLayout*> container,
	BoostBoxData data,
	style::margins limitLinePadding);

} // namespace Ui
