/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_onramp.h"

#include "main/main_session.h"

namespace Wallet {
namespace {

const auto kCryptoCurrency = u"GRAM"_q;

[[nodiscard]] bool SupportsBaseCurrency(
		const MTPVector<MTPstring> &result,
		const QString &baseCurrency) {
	for (const auto &currency : result.v) {
		if (!qs(currency).compare(baseCurrency, Qt::CaseInsensitive)) {
			return true;
		}
	}
	return false;
}

void LogOnrampError(const QString &step, const MTP::Error &error) {
	LOG(("Wallet Error: an onramp request failed: %1 %2 (%3)"
		).arg(step).arg(error.type()).arg(error.code()));
}

} // namespace

Onramp::Onramp(not_null<Main::Session*> session)
: _api(&session->mtp()) {
}

void Onramp::requestSessionUrl(
		const QString &address,
		const QString &baseCurrency,
		Fn<void(QString)> done) {
	_api.request(base::take(_requestId)).cancel();
	_request = Request{
		.address = address,
		.baseCurrency = baseCurrency.toLower(),
		.done = std::move(done),
	};
	requestProviders();
}

void Onramp::requestProviders() {
	using Flag = MTPpayments_GetOnrampProviders::Flag;
	_requestId = _api.request(MTPpayments_GetOnrampProviders(
		MTP_flags(Flag::f_crypto_currency),
		MTP_string(kCryptoCurrency)
	)).done([=](const MTPVector<MTPOnrampProviderInfo> &result) {
		_requestId = 0;
		if (result.v.isEmpty()) {
			finish(QString());
			return;
		}
		const auto &data = result.v.front().data();
		_request->provider = qs(data.vid());
		if (_request->provider.isEmpty()) {
			finish(QString());
		} else if (_request->baseCurrency.isEmpty()
			|| !data.is_supports_base_currencies()) {
			createSession(false);
		} else {
			requestBaseCurrencies();
		}
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		LogOnrampError(u"providers"_q, error);
		finish(QString());
	}).send();
}

void Onramp::requestBaseCurrencies() {
	_requestId = _api.request(MTPpayments_GetOnrampBaseCurrencies(
		MTP_string(_request->provider),
		MTP_string(kCryptoCurrency)
	)).done([=](const MTPVector<MTPstring> &result) {
		_requestId = 0;
		createSession(SupportsBaseCurrency(result, _request->baseCurrency));
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		LogOnrampError(u"base currencies"_q, error);
		createSession(false);
	}).send();
}

void Onramp::createSession(bool withBaseCurrency) {
	using Flag = MTPpayments_CreateOnrampSession::Flag;
	const auto flags = withBaseCurrency ? Flag::f_base_currency : Flag();
	_requestId = _api.request(MTPpayments_CreateOnrampSession(
		MTP_flags(flags),
		MTP_string(_request->provider),
		MTP_string(kCryptoCurrency),
		MTP_string(_request->address),
		MTP_string(), // payment_method
		MTP_string(withBaseCurrency ? _request->baseCurrency : QString()),
		MTP_string(), // base_amount
		MTP_string(), // memo
		MTP_string(), // theme
		MTP_string(), // success_return_url
		MTP_string(), // fail_return_url
		MTP_string() // crypto_amount
	)).done([=](const MTPOnrampSession &result) {
		_requestId = 0;
		finish(qs(result.data().vurl()));
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		LogOnrampError(u"create session"_q, error);
		finish(QString());
	}).send();
}

void Onramp::finish(const QString &url) {
	if (const auto request = base::take(_request)) {
		request->done(url);
	}
}

} // namespace Wallet
