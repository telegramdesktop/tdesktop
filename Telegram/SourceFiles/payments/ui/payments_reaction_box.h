/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace Ui {

class BoxContent;
class GenericBox;
class DynamicImage;

struct TextWithContext {
	TextWithEntities text;
	std::any context;
};

struct PaidReactionTop {
	QString name;
	std::shared_ptr<DynamicImage> photo;
	int count = 0;
	Fn<void()> click;
	bool my = false;
};

struct PaidReactionBoxArgs {
	int chosen = 0;
	int max = 0;

	std::vector<PaidReactionTop> top;

	QString channel;
	Fn<rpl::producer<TextWithContext>(rpl::producer<int> amount)> submit;
	rpl::producer<uint64> balanceValue;
	Fn<void(int, bool)> send;
};

void PaidReactionsBox(
	not_null<GenericBox*> box,
	PaidReactionBoxArgs &&args);

[[nodiscard]] object_ptr<BoxContent> MakePaidReactionBox(
	PaidReactionBoxArgs &&args);

} // namespace Ui
