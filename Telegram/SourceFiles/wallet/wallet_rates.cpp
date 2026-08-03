/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_rates.h"

#include "main/main_session.h"
#include "storage/storage_account.h"
#include "ui/text/format_values.h"

#include <QtCore/QUrl>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Wallet {
namespace {

constexpr auto kRatesUrl = "https://api.mywallet.io/currency-rates"_cs;
constexpr auto kRefreshTimeout = 5 * 60 * crl::time(1000);
constexpr auto kRetryTimeout = 30 * crl::time(1000);
constexpr auto kFiatCurrencyPref = "wallet_fiat_currency"_cs;
constexpr auto kDefaultFiatCurrency = "USD"_cs;

} // namespace

Rates::Rates(not_null<Main::Session*> session)
: _session(session)
, _timer([=] { request(); }) {
}

Rates::~Rates() {
	destroyReply();
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
		? ComputeFiatRate(normalized, *_rates)
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
	request();
}

void Rates::request() {
	destroyReply();
	const auto request = QNetworkRequest(QUrl(kRatesUrl.utf16()));
	_reply = _manager.get(request);
	QObject::connect(_reply, &QNetworkReply::finished, &_manager, [=] {
		if (!_reply) {
			return;
		}
		const auto body = _reply->readAll();
		destroyReply();
		applyResponse(body);
	});
	QObject::connect(_reply, &QNetworkReply::errorOccurred, &_manager, [=] {
		destroyReply();
		scheduleRefresh(true);
	});
}

void Rates::applyResponse(const QByteArray &body) {
	auto parsed = Gram::ParseCurrencyRates(body);
	if (!parsed) {
		scheduleRefresh(true);
		return;
	}
	_rates = std::move(*parsed);
	_value = ComputeFiatRate(_value.current().currency, *_rates);
	scheduleRefresh(false);
}

void Rates::scheduleRefresh(bool afterFailure) {
	_timer.callOnce(afterFailure ? kRetryTimeout : kRefreshTimeout);
}

void Rates::destroyReply() {
	const auto reply = base::take(_reply);
	if (!reply) {
		return;
	}
	reply->disconnect(reply, &QNetworkReply::finished, nullptr, nullptr);
	reply->disconnect(reply, &QNetworkReply::errorOccurred, nullptr, nullptr);
	reply->abort();
	reply->deleteLater();
}

} // namespace Wallet
