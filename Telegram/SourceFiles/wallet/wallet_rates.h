/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "gram/api/gram_api_rates.h"
#include "wallet/wallet_fiat.h"

#include <QtCore/QPointer>
#include <QtNetwork/QNetworkAccessManager>

class QNetworkReply;

namespace Wallet {

class Rates final {
public:
	explicit Rates(not_null<Main::Session*> session);
	~Rates();

	[[nodiscard]] FiatRate current();
	[[nodiscard]] rpl::producer<FiatRate> value();

	[[nodiscard]] std::vector<QString> currencies();
	void setCurrency(const QString &code);

private:
	void ensureStarted();
	void request();
	void applyResponse(const QByteArray &body);
	void scheduleRefresh(bool afterFailure);
	void destroyReply();

	const not_null<Main::Session*> _session;
	QNetworkAccessManager _manager;
	QPointer<QNetworkReply> _reply;
	base::Timer _timer;
	std::optional<Gram::CurrencyRates> _rates;
	rpl::variable<FiatRate> _value;
	bool _started = false;

};

} // namespace Wallet
