/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_fiat.h"

#include "countries/countries_instance.h"
#include "data/data_user.h"
#include "lang/lang_instance.h"
#include "main/main_session.h"
#include "platform/platform_specific.h"
#include "ui/controls/ton_common.h"
#include "ui/text/format_values.h"
#include "wallet/wallet_rates.h"
#include "wallet/wallet_session.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QInputMethod>

#include <cmath>

namespace Wallet {
namespace {

[[nodiscard]] QString FiatDigits(
		int64 nanoAmount,
		const FiatRate &rate,
		int decimals,
		const Ui::CurrencyRule &rule) {
	if (!rate.available()) {
		return QString(QChar(0x2026));
	}
	const auto value = (double(nanoAmount) / Ui::kNanosInOne) * rate.perGram;
	return Ui::FormatWithSeparators(
		value,
		(decimals == kFiatCurrencyDecimals) ? rule.exponent : decimals,
		rule.decimal,
		rule.thousands);
}

[[nodiscard]] QString CloudLanguageLocaleId() {
	const auto &lang = Lang::GetInstance();
	const auto base = lang.cloudLangCode(Lang::Pack::Base);
	auto result = base.isEmpty()
		? lang.cloudLangCode(Lang::Pack::Current)
		: base;
	return result.replace(QChar('-'), QChar('_'));
}

[[nodiscard]] QString CurrencyByCountry(const QString &iso2) {
	if (iso2.isEmpty()) {
		return QString();
	}
	const auto suffix = QChar('_') + iso2;
	const auto locales = QLocale::matchingLocales(
		QLocale::AnyLanguage,
		QLocale::AnyScript,
		QLocale::AnyCountry);
	for (const auto &locale : locales) {
		if (locale.name().endsWith(suffix)) {
			return locale.currencySymbol(QLocale::CurrencyIsoCode);
		}
	}
	return QString();
}

} // namespace

FiatRate ComputeFiatRate(
		const QString &currency,
		const Gram::CurrencyRates &rates,
		float64 usdPerGram) {
	const auto perGram = Gram::ComputePerGram(rates, currency, usdPerGram);
	return { currency, perGram.value_or(0.) };
}

QString FormatFiat(
		int64 nanoAmount,
		const FiatRate &rate,
		int decimals,
		bool approximate) {
	const auto rule = Ui::LookupCurrencyRule(rate.currency);
	const auto name = Ui::CurrencyName(rate.currency);
	const auto digits = FiatDigits(nanoAmount, rate, decimals, rule);
	auto result = approximate ? QString(QChar('~')) : QString();
	if (rule.left) {
		result += name;
		if (rule.space) {
			result += QChar(' ');
		}
		result += digits;
	} else {
		result += digits;
		if (rule.space) {
			result += QChar(' ');
		}
		result += name;
	}
	return result;
}

CurrencyNames LookupCurrencyNames(const QString &currency) {
	auto result = CurrencyNames{
		.english = Ui::CurrencyEnglishName(currency),
	};
	const auto languageId = CloudLanguageLocaleId();
	if (languageId.isEmpty()
		|| languageId == u"en"_q
		|| languageId.startsWith(u"en_"_q)) {
		return result;
	}
	auto localized = Platform::LocalizedCurrencyName(currency, languageId);
	if (localized.isEmpty()) {
		return result;
	}
	localized[0] = localized[0].toUpper();
	if (localized != currency && localized != result.english) {
		result.localized = std::move(localized);
	}
	return result;
}

std::vector<QString> TopCurrencies(not_null<Main::Session*> session) {
	auto result = std::vector<QString>{ u"USD"_q, u"EUR"_q };
	const auto add = [&](const QString &code) {
		if (!code.isEmpty() && !ranges::contains(result, code)) {
			result.push_back(code);
		}
	};
	const auto byLocale = [](const QLocale &locale) {
		return locale.currencySymbol(QLocale::CurrencyIsoCode);
	};
	add(session->wallet().rates().current().currency);
	add(byLocale(QLocale(CloudLanguageLocaleId())));
	add(byLocale(QLocale::system()));
	add(byLocale(QGuiApplication::inputMethod()->locale()));
	add(CurrencyByCountry(
		Countries::Instance().countryISO2ByPhone(session->user()->phone())));
	return result;
}

int64 FiatMinorUnitNanos(const QString &currency) {
	const auto exponent = Ui::LookupCurrencyRule(currency).exponent;
	const auto units = std::max(
		int64(std::llround(std::pow(10., exponent))),
		int64(1));
	return std::max(Ui::kNanosInOne / units, int64(1));
}

rpl::producer<FiatRate> FiatRateValue(not_null<Main::Session*> session) {
	return session->wallet().rates().value();
}

} // namespace Wallet
