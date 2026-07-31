/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_fiat.h"

#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/controls/ton_common.h"

#include <QtCore/QLocale>

namespace Wallet {

float64 TonUsdRate(float64 raw) {
	return (raw > 0.) ? raw : 0.;
}

float64 TonUsdRate(not_null<Main::Session*> session) {
	return TonUsdRate(
		session->appConfig().get<float64>(u"ton_usd_rate"_q, 0.));
}

rpl::producer<float64> TonUsdRateValue(
		not_null<Main::Session*> session) {
	return session->appConfig().value() | rpl::map([=] {
		return TonUsdRate(session);
	}) | rpl::distinct_until_changed();
}

QString FormatUsd(
		int64 nanoAmount,
		float64 rate,
		int decimals,
		bool approximate) {
	if (rate <= 0.) {
		return QString();
	}
	const auto value = (double(nanoAmount) / Ui::kNanosInOne) * rate;
	const auto text = QChar('$')
		+ QLocale().toString(value, 'f', decimals);
	return approximate ? (QChar('~') + text) : text;
}

} // namespace Wallet
