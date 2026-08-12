/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/tests/gram_tests.h"

#include "gram/api/gram_api_rates.h"

#include <cmath>
#include <utility>
#include <vector>

namespace Gram::Tests {
namespace {

[[nodiscard]] bool Near(float64 got, float64 expected) {
	const auto scale = std::abs(expected);
	const auto limit = 1e-12 * ((scale > 1.) ? scale : 1.);
	return std::abs(got - expected) <= limit;
}

[[nodiscard]] QString Printed(float64 value) {
	return QString::number(value, 'g', 17);
}

[[nodiscard]] CurrencyRates MakeRates(
		std::vector<std::pair<QString, float64>> values) {
	auto result = CurrencyRates();
	for (const auto &[code, value] : values) {
		result.values.emplace(code, value);
	}
	return result;
}

} // namespace

std::vector<Check> RatesChecks() {
	return {
		{ u"rates_fixture_conversion"_q, [] {
			const auto rates = MakeCurrencyRates({
				{ u"USD"_q, 1. },
				{ u"TON"_q, 0.711227243 },
				{ u"EUR"_q, 0.86686 },
				{ u"RUB"_q, 79.306265 },
				{ u"CNY"_q, 6.751304 },
				{ u"XXX"_q, 0. },
			});
			if (!rates) {
				return u"make failed"_q;
			} else if (int(rates->values.size()) != 5) {
				return u"count: got "_q
					+ QString::number(int(rates->values.size()));
			} else if (rates->values.contains(u"XXX"_q)) {
				return u"expected dropped: XXX"_q;
			} else if (ComputeRate(*rates, u"XXX"_q, u"TON"_q)) {
				return u"XXX ratio: expected nullopt"_q;
			}
			struct Entry {
				QString code;
				float64 expected = 0.;
				float64 inverse = 0.;
			};
			const auto conversions = std::vector<Entry>{
				{ u"USD"_q, 1.4060203821523187, 0.711227243 },
				{ u"EUR"_q, 1.2188228284725591, 0.82046379230786981 },
				{ u"RUB"_q, 111.50622502237306, 0.0089681091777553765 },
				{ u"CNY"_q, 9.4924710301064792, 0.10534664755134711 },
			};
			for (const auto &entry : conversions) {
				const auto got = ComputeRate(*rates, entry.code, u"TON"_q);
				if (!got) {
					return entry.code + u": expected a value"_q;
				} else if (!Near(*got, entry.expected)) {
					return entry.code + u": got "_q + Printed(*got);
				} else if (Near(*got, entry.inverse)) {
					return entry.code + u": got the inverse"_q;
				}
			}
			return QString();
		} },
		{ u"rates_ratio_direction"_q, [] {
			const auto rates = MakeRates({
				{ u"USD"_q, 1. },
				{ u"EUR"_q, 0.8 },
				{ u"TON"_q, 0.5 },
			});
			const auto usd = ComputeRate(rates, u"USD"_q, u"TON"_q);
			if (!usd) {
				return u"USD: expected a value"_q;
			} else if (!Near(*usd, 2.)) {
				return u"USD: got "_q + Printed(*usd);
			} else if (Near(*usd, 0.5)) {
				return u"USD: got the inverse"_q;
			}
			const auto eur = ComputeRate(rates, u"EUR"_q, u"TON"_q);
			if (!eur) {
				return u"EUR: expected a value"_q;
			} else if (!Near(*eur, 1.6)) {
				return u"EUR: got "_q + Printed(*eur);
			} else if (Near(*eur, 0.625)) {
				return u"EUR: got the inverse"_q;
			}
			return QString();
		} },
		{ u"rates_ratio_missing"_q, [] {
			const auto rates = MakeRates({
				{ u"USD"_q, 1. },
				{ u"TON"_q, 0.5 },
			});
			if (ComputeRate(rates, u"EUR"_q, u"TON"_q)) {
				return u"absent code: expected nullopt"_q;
			} else if (ComputeRate(rates, u"USD"_q, u"BTC"_q)) {
				return u"absent base: expected nullopt"_q;
			} else if (ComputeRate(CurrencyRates(), u"USD"_q, u"TON"_q)) {
				return u"empty rates: expected nullopt"_q;
			}
			return QString();
		} },
		{ u"rates_ratio_overflow"_q, [] {
			const auto rates = MakeCurrencyRates({
				{ u"USD"_q, 1e300 },
				{ u"TON"_q, 1e-30 },
			});
			if (!rates) {
				return u"make failed"_q;
			} else if (int(rates->values.size()) != 2) {
				return u"count: got "_q
					+ QString::number(int(rates->values.size()));
			} else if (ComputeRate(*rates, u"USD"_q, u"TON"_q)) {
				return u"overflowing ratio: expected nullopt"_q;
			}
			return QString();
		} },
		{ u"rates_entries_validation"_q, [] {
			const auto rates = MakeCurrencyRates({
				{ u"USD"_q, 1. },
				{ u"CNY"_q, 0. },
				{ u"BTC"_q, -1. },
				{ u"JPY"_q, NAN },
				{ u"GBP"_q, INFINITY },
				{ u"TON"_q, 0.5 },
			});
			if (!rates) {
				return u"make failed"_q;
			} else if (int(rates->values.size()) != 2) {
				return u"count: got "_q
					+ QString::number(int(rates->values.size()));
			}
			const auto dropped = std::vector<QString>{
				u"CNY"_q,
				u"BTC"_q,
				u"JPY"_q,
				u"GBP"_q,
			};
			for (const auto &code : dropped) {
				if (rates->values.contains(code)) {
					return u"expected dropped: "_q + code;
				}
			}
			const auto ratio = ComputeRate(*rates, u"USD"_q, u"TON"_q);
			if (!ratio) {
				return u"ratio: expected a value"_q;
			} else if (!Near(*ratio, 2.)) {
				return u"ratio: got "_q + Printed(*ratio);
			}
			return QString();
		} },
		{ u"rates_entries_empty"_q, [] {
			if (MakeCurrencyRates({})) {
				return u"empty entries: expected nullopt"_q;
			} else if (MakeCurrencyRates({
				{ u"USD"_q, 0. },
				{ u"TON"_q, -2. },
				{ u"EUR"_q, NAN },
				{ u"RUB"_q, INFINITY },
			})) {
				return u"invalid entries: expected nullopt"_q;
			}
			return QString();
		} },
		{ u"rates_case_normalisation"_q, [] {
			const auto rates = MakeCurrencyRates({
				{ u"usd"_q, 1. },
				{ u"ton"_q, 0.5 },
			});
			if (!rates) {
				return u"make failed"_q;
			} else if (!rates->values.contains(u"USD"_q)) {
				return u"expected an uppercased key"_q;
			}
			const auto ratio = ComputeRate(*rates, u"USD"_q, u"TON"_q);
			if (!ratio) {
				return u"expected a value"_q;
			} else if (!Near(*ratio, 2.)) {
				return u"got "_q + Printed(*ratio);
			}
			const auto lower = ComputeRate(*rates, u"usd"_q, u"ton"_q);
			if (!lower || !Near(*lower, 2.)) {
				return u"lowercase arguments: expected 2"_q;
			}
			return QString();
		} },
	};
}

} // namespace Gram::Tests
