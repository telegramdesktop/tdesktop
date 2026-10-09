/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_rates.h"

#include "main/main_app_config.h"
#include "main/main_session.h"
#include "storage/storage_account.h"
#include "ui/text/format_values.h"

namespace Wallet {
namespace {

constexpr auto kRefreshTimeout = 5 * 60 * crl::time(1000);
constexpr auto kRetryTimeout = 30 * crl::time(1000);
constexpr auto kFiatCurrencyPref = "wallet_fiat_currency"_cs;
constexpr auto kDefaultFiatCurrency = "USD"_cs;

[[nodiscard]] float64 UsdPerGram(not_null<Main::Session*> session) {
	return session->appConfig().get<float64>(u"ton_usd_rate"_q, 0.);
}

} // namespace

Rates::Rates(not_null<Main::Session*> session)
: _session(session)
, _api(&session->mtp())
, _timer([=] { request(); }) {
}

FiatRate Rates::current() {
	ensureStarted();
	return _value.current();
}

rpl::producer<FiatRate> Rates::value() {
	ensureStarted();
	return _value.value();
}

std::vector<QString> Rates::currencies() {
	ensureStarted();
	const auto selected = _value.current().currency;
	if (!_rates) {
		return { selected };
	}
	auto result = std::vector<QString>();
	result.reserve(_rates->values.size() + 1);
	auto found = false;
	for (const auto &[code, value] : _rates->values) {
		if (!Ui::KnownCurrency(code)) {
			continue;
		} else if (code == selected) {
			found = true;
		}
		result.push_back(code);
	}
	if (!found) {
		result.push_back(selected);
	}
	ranges::sort(result);
	return result;
}

void Rates::setCurrency(const QString &code) {
	ensureStarted();
	const auto normalized = code.toUpper();
	if (normalized == _value.current().currency) {
		return;
	}
	_session->local().writePref<QString>(kFiatCurrencyPref, normalized);
	_value = _rates
		? ComputeFiatRate(normalized, *_rates, UsdPerGram(_session))
		: FiatRate{ normalized, 0. };
}

void Rates::ensureStarted() {
	if (_started) {
		return;
	}
	_started = true;
	const auto stored = _session->local().readPref<QString>(
		kFiatCurrencyPref,
		kDefaultFiatCurrency.utf16());
	auto code = stored.toUpper();
	if (code.isEmpty() || !Ui::KnownCurrency(code)) {
		code = kDefaultFiatCurrency.utf16();
	}
	_value = FiatRate{ code, 0. };
	_session->appConfig().refreshed(
	) | rpl::on_next([=] {
		recompute();
	}, _lifetime);
	request();
}

void Rates::request() {
	if (_requestId) {
		return;
	}
	_requestId = _api.request(MTPpayments_GetCurrencyRates(
	)).done([=](const MTPpayments_CurrencyRates &result) {
		_requestId = 0;
		applyRates(result);
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: payments.getCurrencyRates failed: %1"
			).arg(error.type()));
		_requestId = 0;
		scheduleRefresh(true);
	}).send();
}

void Rates::applyRates(const MTPpayments_CurrencyRates &result) {
	const auto &list = result.data().vrates().v;
	auto entries = std::vector<Gram::CurrencyRateEntry>();
	entries.reserve(list.size());
	for (const auto &rate : list) {
		const auto &data = rate.data();
		entries.push_back({ qs(data.vcurrency()), data.vrate().v });
	}
	auto parsed = Gram::MakeCurrencyRates(entries);
	if (!parsed) {
		scheduleRefresh(true);
		return;
	}
	_rates = std::move(*parsed);
	_value = ComputeFiatRate(
		_value.current().currency,
		*_rates,
		UsdPerGram(_session));
	scheduleRefresh(false);
}

void Rates::recompute() {
	if (!_rates) {
		return;
	}
	const auto updated = ComputeFiatRate(
		_value.current().currency,
		*_rates,
		UsdPerGram(_session));
	if (updated.available()) {
		_value = updated;
	}
}

void Rates::scheduleRefresh(bool afterFailure) {
	_timer.callOnce(afterFailure ? kRetryTimeout : kRefreshTimeout);
}

} // namespace Wallet
