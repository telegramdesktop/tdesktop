/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace Data {

struct SubscriptionOption {
	QString duration;
	QString discount;
	QString costPerMonth;
	QString costTotal;
	QString total;
	QString botUrl;
};
using SubscriptionOptions = std::vector<SubscriptionOption>;

} // namespace Data
