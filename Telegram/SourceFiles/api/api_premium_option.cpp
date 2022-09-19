/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_premium_option.h"

#include "ui/text/format_values.h"

namespace Api {

constexpr auto kDiscountDivider = 5.;

Data::SubscriptionOption CreateSubscriptionOption(
		int months,
		int monthlyAmount,
		int64 amount,
		const QString &currency,
		const QString &botUrl) {
	const auto discount = [&] {
		const auto percent = monthlyAmount * months / float64(amount) - 1.;
		return std::round(percent * 100. / kDiscountDivider)
			* kDiscountDivider;
	}();
	return {
		.duration = Ui::FormatTTL(months * 86400 * 31),
		.discount = discount
			? QString::fromUtf8("\xe2\x88\x92%1%").arg(discount)
			: QString(),
		.costPerMonth = Ui::FillAmountAndCurrency(
			amount / float64(months),
			currency),
		.costTotal = Ui::FillAmountAndCurrency(amount, currency),
		.botUrl = botUrl,
	};
}

} // namespace Api
