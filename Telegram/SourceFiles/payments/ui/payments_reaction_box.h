/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace style {
struct RoundCheckbox;
} // namespace style

namespace Main {
class Session;
} // namespace Main

namespace Ui {

class BoxContent;
class GenericBox;
class DynamicImage;

struct PaidReactionTop {
	QString name;
	std::shared_ptr<DynamicImage> photo;
	uint64 barePeerId = 0;
	int count = 0;
	Fn<void()> click;
	bool my = false;
};

struct PaidReactionBoxArgs {
	int chosen = 0;
	int max = 0;

	std::vector<PaidReactionTop> top;

	not_null<Main::Session*> session;
	QString channel;
	Fn<rpl::producer<TextWithEntities>(rpl::producer<int> amount)> submit;
	rpl::producer<CreditsAmount> balanceValue;
	Fn<void(int, uint64)> send;
};

void PaidReactionsBox(
	not_null<GenericBox*> box,
	PaidReactionBoxArgs &&args);

[[nodiscard]] object_ptr<BoxContent> MakePaidReactionBox(
	PaidReactionBoxArgs &&args);

[[nodiscard]] QImage GenerateSmallBadgeImage(
	QString text,
	const style::icon &icon,
	QColor bg,
	QColor fg,
	const style::RoundCheckbox *borderSt = nullptr);

} // namespace Ui
