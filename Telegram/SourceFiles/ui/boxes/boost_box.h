/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace Ui {

void StartFireworks(not_null<QWidget*> parent);

class Show;
class RpWidget;
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

struct AskBoostBoxData {
	QString link;
	BoostCounters boost;
	int requiredLevel = 0;
};

void AskBoostBox(
	not_null<GenericBox*> box,
	AskBoostBoxData data,
	Fn<void()> openStatistics,
	Fn<void()> startGiveaway);

[[nodiscard]] object_ptr<RpWidget> MakeLinkLabel(
	not_null<QWidget*> parent,
	rpl::producer<QString> text,
	rpl::producer<QString> link,
	std::shared_ptr<Show> show,
	object_ptr<RpWidget> right);

void FillBoostLimit(
	rpl::producer<> showFinished,
	rpl::producer<bool> you,
	not_null<VerticalLayout*> container,
	BoostCounters data,
	style::margins limitLinePadding);

} // namespace Ui
