/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_premium_subscription_option.h"

namespace Api {

[[nodiscard]] Data::PremiumSubscriptionOption CreateSubscriptionOption(
	int months,
	int monthlyAmount,
	int64 amount,
	const QString &currency,
	const QString &botUrl);

template<typename Option>
[[nodiscard]] auto PremiumSubscriptionOptionsFromTL(
		const QVector<Option> &tlOpts) -> Data::PremiumSubscriptionOptions {
	if (tlOpts.isEmpty()) {
		return {};
	}
	auto monthlyAmountPerCurrency = base::flat_map<QString, int>();
	auto result = Data::PremiumSubscriptionOptions();
	const auto monthlyAmount = [&](const QString &currency) -> int {
		const auto it = monthlyAmountPerCurrency.find(currency);
		if (it != end(monthlyAmountPerCurrency)) {
			return it->second;
		}
		const auto &min = ranges::min_element(
			tlOpts,
			ranges::less(),
			[&](const Option &o) {
				return currency == qs(o.data().vcurrency())
					? o.data().vamount().v
					: std::numeric_limits<int64_t>::max();
			}
		)->data();
		const auto monthly = min.vamount().v / float64(min.vmonths().v);
		monthlyAmountPerCurrency.emplace(currency, monthly);
		return monthly;
	};
	result.reserve(tlOpts.size());
	for (const auto &tlOption : tlOpts) {
		const auto &option = tlOption.data();
		auto botUrl = QString();
		if constexpr (!std::is_same_v<Option, MTPPremiumGiftCodeOption>) {
			botUrl = qs(option.vbot_url());
		}
		const auto months = option.vmonths().v;
		const auto amount = option.vamount().v;
		const auto currency = qs(option.vcurrency());
		result.push_back(CreateSubscriptionOption(
			months,
			monthlyAmount(currency),
			amount,
			currency,
			botUrl));
	}
	return result;
}

} // namespace Api
