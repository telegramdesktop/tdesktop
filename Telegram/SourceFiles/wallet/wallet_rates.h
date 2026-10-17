/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "gram/api/gram_api_rates.h"
#include "mtproto/sender.h"
#include "wallet/wallet_fiat.h"

namespace Wallet {

class Rates final {
public:
	explicit Rates(not_null<Main::Session*> session);

	[[nodiscard]] FiatRate current();
	[[nodiscard]] rpl::producer<FiatRate> value();

	[[nodiscard]] std::vector<QString> currencies();
	void setCurrency(const QString &code);

private:
	void ensureStarted();
	void request();
	void applyRates(const MTPpayments_CurrencyRates &result);
	void recompute();
	void scheduleRefresh(bool afterFailure);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	mtpRequestId _requestId = 0;
	base::Timer _timer;
	std::optional<Gram::CurrencyRates> _rates;
	rpl::variable<FiatRate> _value;
	bool _started = false;
	rpl::lifetime _lifetime;

};

} // namespace Wallet
