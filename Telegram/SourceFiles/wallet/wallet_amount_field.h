/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text.h"

namespace Ui {
class TonAmountInput;
class VerticalLayout;
} // namespace Ui

namespace Wallet {

struct AmountFieldArgs {
	int64 value = 0;
	Fn<int()> fractionDigits;
	Fn<QString()> separator;
	rpl::producer<bool> entryFiat;
	rpl::producer<QString> currency;
	rpl::producer<TextWithEntities> equivalent;
	Ui::Text::MarkedContext equivalentContext;
	Fn<void()> swap;
	rpl::producer<bool> equivalentShown;
};

[[nodiscard]] not_null<Ui::TonAmountInput*> AddAmountField(
	not_null<Ui::VerticalLayout*> container,
	int topSkip,
	AmountFieldArgs &&args);

} // namespace Wallet
