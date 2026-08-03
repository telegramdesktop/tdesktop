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
		{ u"rates_fixture_values"_q, [] {
			const auto name = u"api-currency-rates.json"_q;
			const auto bytes = ReadFixture(name);
			if (bytes.isEmpty()) {
				return u"fixture read failed: "_q + name;
			}
			const auto rates = ParseCurrencyRates(bytes);
			if (!rates) {
				return u"parse failed"_q;
			}
			struct Entry {
				QString code;
				float64 value = 0.;
			};
			const auto expected = std::vector<Entry>{
				{ u"USD"_q, 1. },
				{ u"EUR"_q, 0.86686 },
				{ u"RUB"_q, 79.306265 },
				{ u"CNY"_q, 6.751304 },
				{ u"BTC"_q, 0.000015689590375408 },
				{ u"TON"_q, 0.711227243 },
			};
			if (int(rates->values.size()) != int(expected.size())) {
				return u"count: got "_q
					+ QString::number(int(rates->values.size()));
			}
			for (const auto &entry : expected) {
				const auto i = rates->values.find(entry.code);
				if (i == rates->values.end()) {
					return u"missing: "_q + entry.code;
				} else if (!Near(i->second, entry.value)) {
					return entry.code
						+ u": got "_q
						+ Printed(i->second);
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
			const auto json = QByteArray(
				"{\"rates\":{"
					"\"USD\":\"1e300\","
					"\"TON\":\"1e-30\""
				"}}");
			const auto rates = ParseCurrencyRates(json);
			if (!rates) {
				return u"parse failed"_q;
			} else if (int(rates->values.size()) != 2) {
				return u"count: got "_q
					+ QString::number(int(rates->values.size()));
			} else if (ComputeRate(*rates, u"USD"_q, u"TON"_q)) {
				return u"overflowing ratio: expected nullopt"_q;
			}
			return QString();
		} },
		{ u"rates_entry_validation"_q, [] {
			const auto json = QByteArray(
				"{\"rates\":{"
					"\"USD\":\"1\","
					"\"EUR\":5,"
					"\"RUB\":\"abc\","
					"\"CNY\":\"0\","
					"\"BTC\":\"-1\","
					"\"JPY\":\"nan\","
					"\"GBP\":\"inf\","
					"\"TON\":\"0.5\""
				"}}");
			const auto rates = ParseCurrencyRates(json);
			if (!rates) {
				return u"parse failed"_q;
			} else if (int(rates->values.size()) != 2) {
				return u"count: got "_q
					+ QString::number(int(rates->values.size()));
			}
			const auto dropped = std::vector<QString>{
				u"EUR"_q,
				u"RUB"_q,
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
		{ u"rates_parse_negative"_q, [] {
			const auto bad = std::vector<QByteArray>{
				QByteArray(""),
				QByteArray("{\"rates\":{\"USD\":\"1\""),
				QByteArray("{not json}"),
				QByteArray("null"),
				QByteArray("42"),
				QByteArray("[]"),
				QByteArray("{}"),
				QByteArray("{\"rates\":5}"),
				QByteArray("{\"rates\":null}"),
				QByteArray("{\"rates\":{}}"),
				QByteArray("{\"rates\":{\"USD\":\"0\"}}"),
			};
			for (const auto &json : bad) {
				if (ParseCurrencyRates(json)) {
					return u"expected nullopt for: "_q
						+ QString::fromUtf8(json);
				}
			}
			return QString();
		} },
		{ u"rates_case_normalisation"_q, [] {
			const auto rates = ParseCurrencyRates(
				QByteArray("{\"rates\":{\"usd\":\"1\",\"ton\":\"0.5\"}}"));
			if (!rates) {
				return u"parse failed"_q;
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
