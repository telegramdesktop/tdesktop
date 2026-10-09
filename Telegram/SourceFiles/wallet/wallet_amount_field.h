/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text.h"

namespace Lottie {
class Icon;
} // namespace Lottie

namespace Ui {
class TonAmountInput;
class VerticalLayout;
} // namespace Ui

namespace Wallet {

struct AmountLabel {
	TextWithEntities prefix;
	QString amount;
	QString decimal;
	QString suffix;
	QString unit;

	friend inline bool operator==(
		const AmountLabel &,
		const AmountLabel &) = default;
};

struct AmountFieldArgs {
	int64 value = 0;
	Fn<int()> fractionDigits;
	Fn<QString()> separator;
	rpl::producer<bool> entryFiat;
	rpl::producer<QString> currency;
	rpl::producer<AmountLabel> equivalent;
	Ui::Text::MarkedContext equivalentContext;
	Fn<void()> swap;
	rpl::producer<bool> equivalentShown;
};

[[nodiscard]] not_null<Ui::TonAmountInput*> AddAmountField(
	not_null<Ui::VerticalLayout*> container,
	int topSkip,
	AmountFieldArgs &&args);

struct AmountDiamond {
	std::unique_ptr<Lottie::Icon> icon;
	QRectF global;
};
[[nodiscard]] AmountDiamond TakeAmountDiamond(
	not_null<Ui::TonAmountInput*> field);

} // namespace Wallet
