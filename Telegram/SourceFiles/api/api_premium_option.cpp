/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_premium_option.h"

#include "ui/text/format_values.h"

namespace Api {

constexpr auto kDiscountDivider = 1.;

Data::PremiumSubscriptionOption CreateSubscriptionOption(
		int months,
		float64 monthlyAmount,
		int64 amount,
		const QString &currency,
		const QString &botUrl) {
	const auto baselineAmount = monthlyAmount * months;
	const auto discount = [&] {
		const auto percent = 1. - float64(amount) / baselineAmount;
		return base::SafeRound(percent * 100. / kDiscountDivider)
			* kDiscountDivider;
	}();
	const auto hasDiscount = (discount > 0);
	return {
		.months = months,
		.duration = Ui::FormatTTL(months * 86400 * 31),
		.discount = hasDiscount
			? QString::fromUtf8("\xe2\x88\x92%1%").arg(discount)
			: QString(),
		.costPerMonth = Ui::FillAmountAndCurrency(
			int64(base::SafeRound(amount / float64(months))),
			currency),
		.costNoDiscount = hasDiscount
			? Ui::FillAmountAndCurrency(
				int64(base::SafeRound(baselineAmount)),
				currency)
			: QString(),
		.costPerYear = Ui::FillAmountAndCurrency(
			int64(base::SafeRound(amount / float64(months / 12.))),
			currency),
		.botUrl = botUrl,
	};
}

} // namespace Api
