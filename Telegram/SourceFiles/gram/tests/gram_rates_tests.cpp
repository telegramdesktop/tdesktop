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

constexpr auto kUsdPerGram = 1.34507;

struct Conversion {
	QString code;
	float64 expected = 0.;
	float64 divided = 0.;
};

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
			} else if (ComputePerGram(*rates, u"XXX"_q, kUsdPerGram)) {
				return u"XXX: expected nullopt"_q;
			}
			const auto conversions = std::vector<Conversion>{
				{ u"USD"_q, 1.34507, 0.74345573092850192 },
				{ u"TON"_q, 0.95665042774200992, 0.52876596980082813 },
				{ u"EUR"_q, 1.1659873802, 0.64447203491268112 },
				{ u"RUB"_q, 106.67247786355, 58.96069721278446 },
				{ u"CNY"_q, 9.0809764712799996, 5.0192956500405188 },
			};
			for (const auto &entry : conversions) {
				const auto got = ComputePerGram(
					*rates,
					entry.code,
					kUsdPerGram);
				if (!got) {
					return entry.code + u": expected a value"_q;
				} else if (!Near(*got, entry.expected)) {
					return entry.code + u": got "_q + Printed(*got);
				} else if (Near(*got, entry.divided)) {
					return entry.code + u": divided by usdPerGram"_q;
				}
			}
			return QString();
		} },
		{ u"rates_ton_entry_ignored"_q, [] {
			const auto rates = MakeCurrencyRates({
				{ u"USD"_q, 1. },
				{ u"TON"_q, 0.711227243 },
				{ u"EUR"_q, 0.86686 },
			});
			const auto noTon = MakeCurrencyRates({
				{ u"USD"_q, 1. },
				{ u"EUR"_q, 0.86686 },
			});
			if (!rates || !noTon) {
				return u"make failed"_q;
			}
			for (const auto &code : { u"USD"_q, u"EUR"_q }) {
				const auto got = ComputePerGram(*rates, code, kUsdPerGram);
				const auto same = ComputePerGram(*noTon, code, kUsdPerGram);
				if (!got || !same) {
					return code + u": expected a value"_q;
				} else if (*got != *same) {
					return code + u": got "_q
						+ Printed(*got)
						+ u" with TON, "_q
						+ Printed(*same)
						+ u" without TON"_q;
				}
			}
			const auto ton = ComputePerGram(*rates, u"TON"_q, kUsdPerGram);
			if (!ton) {
				return u"TON: expected an ordinary value"_q;
			} else if (!Near(*ton, 0.95665042774200992)) {
				return u"TON: got "_q + Printed(*ton);
			} else if (ComputePerGram(*noTon, u"TON"_q, kUsdPerGram)) {
				return u"TON: expected nullopt when absent"_q;
			}
			return QString();
		} },
		{ u"rates_per_gram_direction"_q, [] {
			const auto rates = MakeRates({
				{ u"USD"_q, 1. },
				{ u"EUR"_q, 0.5 },
				{ u"TON"_q, 0.25 },
			});
			const auto conversions = std::vector<Conversion>{
				{ u"USD"_q, 8., 0.125 },
				{ u"EUR"_q, 4., 0.0625 },
				{ u"TON"_q, 2., 0.03125 },
			};
			for (const auto &entry : conversions) {
				const auto got = ComputePerGram(rates, entry.code, 8.);
				if (!got) {
					return entry.code + u": expected a value"_q;
				} else if (!Near(*got, entry.expected)) {
					return entry.code + u": got "_q + Printed(*got);
				} else if (Near(*got, entry.divided)) {
					return entry.code + u": divided by usdPerGram"_q;
				}
			}
			return QString();
		} },
		{ u"rates_per_gram_guards"_q, [] {
			const auto rates = MakeRates({
				{ u"USD"_q, 1. },
				{ u"EUR"_q, 0.8 },
			});
			const auto rejected = std::vector<float64>{
				0.,
				-1.,
				NAN,
				INFINITY,
				-INFINITY,
			};
			for (const auto usdPerGram : rejected) {
				if (ComputePerGram(rates, u"USD"_q, usdPerGram)) {
					return u"usdPerGram "_q
						+ Printed(usdPerGram)
						+ u": expected nullopt"_q;
				}
			}
			const auto empty = CurrencyRates();
			const auto negative = MakeRates({ { u"USD"_q, -2. } });
			if (ComputePerGram(rates, u"RUB"_q, kUsdPerGram)) {
				return u"absent code: expected nullopt"_q;
			} else if (ComputePerGram(empty, u"USD"_q, kUsdPerGram)) {
				return u"empty rates: expected nullopt"_q;
			} else if (ComputePerGram(negative, u"USD"_q, -3.)) {
				return u"two negatives: expected nullopt"_q;
			}
			return QString();
		} },
		{ u"rates_per_gram_product"_q, [] {
			const auto huge = MakeCurrencyRates({ { u"USD"_q, 1e300 } });
			const auto tiny = MakeCurrencyRates({ { u"USD"_q, 1e-300 } });
			if (!huge || !tiny) {
				return u"make failed"_q;
			} else if (ComputePerGram(*huge, u"USD"_q, 1e300)) {
				return u"overflowing product: expected nullopt"_q;
			} else if (ComputePerGram(*tiny, u"USD"_q, 1e-300)) {
				return u"underflowing product: expected nullopt"_q;
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
			const auto perGram = ComputePerGram(*rates, u"USD"_q, 3.);
			if (!perGram) {
				return u"USD: expected a value"_q;
			} else if (!Near(*perGram, 3.)) {
				return u"USD: got "_q + Printed(*perGram);
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
			const auto upper = ComputePerGram(*rates, u"USD"_q, 3.);
			if (!upper) {
				return u"expected a value"_q;
			} else if (!Near(*upper, 3.)) {
				return u"got "_q + Printed(*upper);
			}
			const auto lower = ComputePerGram(*rates, u"usd"_q, 3.);
			if (!lower || !Near(*lower, 3.)) {
				return u"lowercase argument: expected 3"_q;
			}
			return QString();
		} },
	};
}

} // namespace Gram::Tests
