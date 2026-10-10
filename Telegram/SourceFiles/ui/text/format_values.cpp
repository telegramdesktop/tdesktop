/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/text/format_values.h"

#include "base/unixtime.h"
#include "lang/lang_keys.h"
#include "countries/countries_instance.h"

#include <QtCore/QLocale>
#include <locale>
#include <sstream>
#include <iostream>

namespace Ui {
namespace {

constexpr auto kSecondsInYear = 365 * 24 * 60 * 60; // 31536000

[[nodiscard]] QString FormatTextWithReadyAndTotal(
		tr::phrase<lngtag_ready, lngtag_total, lngtag_mb> phrase,
		qint64 ready,
		qint64 total) {
	QString readyStr, totalStr, mb;
	if (total >= 1024 * 1024) { // more than 1 mb
		const qint64 readyTenthMb = (ready * 10 / (1024 * 1024));
		const qint64 totalTenthMb = (total * 10 / (1024 * 1024));
		readyStr = QString::number(readyTenthMb / 10)
			+ '.'
			+ QString::number(readyTenthMb % 10);
		totalStr = QString::number(totalTenthMb / 10)
			+ '.'
			+ QString::number(totalTenthMb % 10);
		mb = u"MB"_q;
	} else if (total >= 1024) {
		qint64 readyKb = (ready / 1024), totalKb = (total / 1024);
		readyStr = QString::number(readyKb);
		totalStr = QString::number(totalKb);
		mb = u"KB"_q;
	} else {
		readyStr = QString::number(ready);
		totalStr = QString::number(total);
		mb = u"B"_q;
	}
	return phrase(tr::now, lt_ready, readyStr, lt_total, totalStr, lt_mb, mb);
}

} // namespace

QString FormatSizeText(qint64 size) {
	if (size >= 1024 * 1024) { // more than 1 mb
		const qint64 sizeTenthMb = (size * 10 / (1024 * 1024));
		return QString::number(sizeTenthMb / 10)
			+ '.'
			+ QString::number(sizeTenthMb % 10) + u" MB"_q;
	}
	if (size >= 1024) {
		const qint64 sizeTenthKb = (size * 10 / 1024);
		return QString::number(sizeTenthKb / 10)
			+ '.'
			+ QString::number(sizeTenthKb % 10) + u" KB"_q;
	}
	return QString::number(size) + u" B"_q;
}

QString FormatDownloadText(qint64 ready, qint64 total) {
	return FormatTextWithReadyAndTotal(
		tr::lng_save_downloaded,
		ready,
		total);
}

QString FormatProgressText(qint64 ready, qint64 total) {
	return FormatTextWithReadyAndTotal(
		tr::lng_media_save_progress,
		ready,
		total);
}

QString FormatDateTime(QDateTime date) {
	const auto now = QDateTime::currentDateTime();
	if (date.date() == now.date()) {
		return tr::lng_mediaview_today(
			tr::now,
			lt_time,
			QLocale().toString(date.time(), QLocale::ShortFormat));
	} else if (date.date().addDays(1) == now.date()) {
		return tr::lng_mediaview_yesterday(
			tr::now,
			lt_time,
			QLocale().toString(date.time(), QLocale::ShortFormat));
	} else {
		return tr::lng_mediaview_date_time(
			tr::now,
			lt_date,
			QLocale().toString(date.date(), QLocale::ShortFormat),
			lt_time,
			QLocale().toString(date.time(), QLocale::ShortFormat));
	}
}

QString FormatDateTimeSavedFrom(QDateTime dateTime) {
	const auto current = QDate::currentDate();
	const auto date = dateTime.date();
	const auto timeStr = QLocale().toString(
		dateTime.time(),
		QLocale::ShortFormat);

	if (date == current) {
		return tr::lng_mediaview_today(tr::now, lt_time, timeStr);
	} else if (date == current.addDays(-1)) {
		return tr::lng_mediaview_yesterday(tr::now, lt_time, timeStr);
	}
	const auto diff = std::abs(
		base::unixtime::now() - base::unixtime::serialize(dateTime));
	const auto dateStr = (diff < kSecondsInYear)
		? tr::lng_month_day(
			tr::now,
			lt_month,
			Lang::MonthSmall(date.month())(tr::now),
			lt_day,
			QString::number(date.day()))
		: langDayOfMonthFull(date);

	return tr::lng_mediaview_date_time(
		tr::now,
		lt_date,
		dateStr,
		lt_time,
		timeStr);
}

QString FormatDurationText(qint64 duration) {
	qint64 hours = (duration / 3600), minutes = (duration % 3600) / 60, seconds = duration % 60;
	return (hours ? QString::number(hours) + ':' : QString()) + (minutes >= 10 ? QString() : QString('0')) + QString::number(minutes) + ':' + (seconds >= 10 ? QString() : QString('0')) + QString::number(seconds);
}

QString FormatDurationWords(qint64 duration) {
	if (duration > 59) {
		auto minutes = (duration / 60);
		auto minutesCount = tr::lng_duration_minsec_minutes(tr::now, lt_count, minutes);
		auto seconds = (duration % 60);
		auto secondsCount = tr::lng_duration_minsec_seconds(tr::now, lt_count, seconds);
		return tr::lng_duration_minutes_seconds(tr::now, lt_minutes_count, minutesCount, lt_seconds_count, secondsCount);
	}
	return tr::lng_seconds(tr::now, lt_count, duration);
}

QString FormatDurationWordsSlowmode(qint64 duration) {
	if (duration > 59) {
		auto minutes = (duration / 60);
		auto minutesCount = tr::lng_duration_minsec_minutes(tr::now, lt_count, minutes);
		auto seconds = (duration % 60);
		auto secondsCount = tr::lng_duration_minsec_seconds(tr::now, lt_count, seconds);
		return tr::lng_duration_minutes_seconds(tr::now, lt_minutes_count, minutesCount, lt_seconds_count, secondsCount);
	}
	return tr::lng_slowmode_seconds(tr::now, lt_count, duration);
}

QString FormatDurationAndSizeText(qint64 duration, qint64 size) {
	return tr::lng_duration_and_size(tr::now, lt_duration, FormatDurationText(duration), lt_size, FormatSizeText(size));
}

QString FormatGifAndSizeText(qint64 size) {
	return tr::lng_duration_and_size(tr::now, lt_duration, u"GIF"_q, lt_size, FormatSizeText(size));
}

QString FormatPlayedText(qint64 played, qint64 duration) {
	return tr::lng_duration_played(tr::now, lt_played, FormatDurationText(played), lt_duration, FormatDurationText(duration));
}

QString FillAmountAndCurrency(
		int64 amount,
		const QString &currency,
		bool forceStripDotZero) {
	// std::abs doesn't work on that one :/
	Expects(amount != std::numeric_limits<int64>::min());

	if (currency == kCreditsCurrency) {
		return QChar(0x2B50) + Lang::FormatCountDecimal(std::abs(amount));
	}

	const auto rule = LookupCurrencyRule(currency);
	const auto prefix = (amount < 0)
		? QString::fromUtf8("\xe2\x88\x92")
		: QString();
	const auto value = std::abs(amount) / std::pow(10., rule.exponent);
	const auto name = CurrencyName(currency);
	auto result = prefix;
	if (rule.left) {
		result.append(name);
		if (rule.space) result.append(' ');
	}
	const auto precision = ((!rule.stripDotZero && !forceStripDotZero)
		|| std::floor(value) != value)
		? rule.exponent
		: 0;
	result.append(FormatWithSeparators(
		value,
		precision,
		rule.decimal,
		rule.thousands));
	if (!rule.left) {
		if (rule.space) result.append(' ');
		result.append(name);
	}
	return result;
}

namespace {

[[nodiscard]] const base::flat_map<QString, CurrencyRule> &CurrencyRulesMap() {
	static const auto kRules = std::vector<std::pair<QString, CurrencyRule>>{
		{ u"AED"_q, { "", ',', '.', true, true } },
		{ u"AFN"_q, {} },
		{ u"ALL"_q, { "", '.', ',', false } },
		{ u"AMD"_q, { "", ',', '.', false, true } },
		{ u"ARS"_q, { "", '.', ',', true, true } },
		{ u"AUD"_q, { "AU$" } },
		{ u"AZN"_q, { "", ' ', ',', false, true } },
		{ u"BAM"_q, { "", '.', ',', false, true } },
		{ u"BDT"_q, { "", ',', '.', true, true } },
		{ u"BGN"_q, { "", ' ', ',', false, true } },
		{ u"BHD"_q, { "", ',', '.', true, true, 3 } },
		{ u"BND"_q, { "", '.', ',' } },
		{ u"BOB"_q, { "", '.', ',', true, true } },
		{ u"BRL"_q, { "R$", '.', ',', true, true } },
		{ u"BYN"_q, { "", ' ', ',', false, true } },
		{ u"CAD"_q, { "CA$" } },
		{ u"CHF"_q, { "", '\'', '.', false, true } },
		{ u"CLP"_q, { "", '.', ',', true, true, 0 } },
		{ u"CNY"_q, { "\x43\x4E\xC2\xA5" } },
		{ u"COP"_q, { "", '.', ',', true, true } },
		{ u"CRC"_q, { "", '.', ',' } },
		{ u"CZK"_q, { "", ' ', ',', false, true } },
		{ u"DKK"_q, { "", '\0', ',', false, true } },
		{ u"DOP"_q, {} },
		{ u"DZD"_q, { "", ',', '.', true, true } },
		{ u"EGP"_q, { "", ',', '.', true, true } },
		{ u"ETB"_q, {} },
		{ u"EUR"_q, { "\xE2\x82\xAC", ' ', ',', false, true } },
		{ u"GBP"_q, { "\xC2\xA3" } },
		{ u"GEL"_q, { "", ' ', ',', false, true } },
		{ u"GHS"_q, {} },
		{ u"GTQ"_q, {} },
		{ u"HKD"_q, { "HK$" } },
		{ u"HNL"_q, { "", ',', '.', true, true } },
		{ u"HRK"_q, { "", '.', ',', false, true } },
		{ u"HUF"_q, { "", ' ', ',', false, true } },
		{ u"IDR"_q, { "", '.', ',' } },
		{ u"ILS"_q, { "\xE2\x82\xAA", ',', '.', true, true } },
		{ u"INR"_q, { "\xE2\x82\xB9" } },
		{ u"IQD"_q, { "", ',', '.', true, true, 3 } },
		{ u"IRR"_q, { "", ',', '/', false, true } },
		{ u"ISK"_q, { "", '.', ',', false, true, 0 } },
		{ u"JMD"_q, {} },
		{ u"JOD"_q, { "", ',', '.', true, false, 3 } },
		{ u"JPY"_q, { "\xC2\xA5", ',', '.', true, false, 0 } },
		{ u"KES"_q, {} },
		{ u"KGS"_q, { "", ' ', '-', false, true } },
		{ u"KRW"_q, { "\xE2\x82\xA9", ',', '.', true, false, 0 } },
		{ u"KZT"_q, { "", ' ', '-' } },
		{ u"LBP"_q, { "", ',', '.', true, true } },
		{ u"LKR"_q, { "", ',', '.', true, true } },
		{ u"MAD"_q, { "", ',', '.', true, true } },
		{ u"MDL"_q, { "", ',', '.', false, true } },
		{ u"MMK"_q, {} },
		{ u"MNT"_q, { "", ' ', ',' } },
		{ u"MOP"_q, {} },
		{ u"MUR"_q, {} },
		{ u"MVR"_q, { "", ',', '.', false, true } },
		{ u"MXN"_q, { "MX$" } },
		{ u"MYR"_q, {} },
		{ u"MZN"_q, {} },
		{ u"NGN"_q, {} },
		{ u"NIO"_q, { "", ',', '.', true, true } },
		{ u"NOK"_q, { "", ' ', ',', true, true } },
		{ u"NPR"_q, {} },
		{ u"NZD"_q, { "NZ$" } },
		{ u"PAB"_q, { "", ',', '.', true, true } },
		{ u"PEN"_q, { "", ',', '.', true, true } },
		{ u"PHP"_q, {} },
		{ u"PKR"_q, {} },
		{ u"PLN"_q, { "", ' ', ',', false, true } },
		{ u"PYG"_q, { "", '.', ',', true, true, 0 } },
		{ u"QAR"_q, { "", ',', '.', true, true } },
		{ u"RON"_q, { "", '.', ',', false, true } },
		{ u"RSD"_q, { "", '.', ',', false, true } },
		{ u"RUB"_q, { "", ' ', ',', false, true } },
		{ u"SAR"_q, { "", ',', '.', true, true } },
		{ u"SEK"_q, { "", '.', ',', false, true } },
		{ u"SGD"_q, {} },
		{ u"SYP"_q, { "", ',', '.', true, true } },
		{ u"THB"_q, { "\xE0\xB8\xBF" } },
		{ u"TJS"_q, { "", ' ', ';', false, true } },
		{ u"TRY"_q, { "", '.', ',', false, true } },
		{ u"TTD"_q, {} },
		{ u"TWD"_q, { "NT$" } },
		{ u"TZS"_q, {} },
		{ u"UAH"_q, { "", ' ', ',', false } },
		{ u"UGX"_q, { "", ',', '.', true, false, 0 } },
		{ u"USD"_q, { "$" } },
		{ u"UYU"_q, { "", '.', ',', true, true } },
		{ u"UZS"_q, { "", ' ', ',', false, true } },
		{ u"VEF"_q, { "", '.', ',', true, true } },
		{ u"VND"_q, { "\xE2\x82\xAB", '.', ',', false, true, 0 } },
		{ u"YER"_q, { "", ',', '.', true, true } },
		{ u"ZAR"_q, { "", ',', '.', true, true } },

		//{ u"VUV"_q, { "", ',', '.', false, false, 0 } },
		//{ u"WST"_q, {} },
		//{ u"XAF"_q, { "FCFA", ',', '.', false, false, 0 } },
		//{ u"XCD"_q, {} },
		//{ u"XOF"_q, { "CFA", ' ', ',', false, false, 0 } },
		//{ u"XPF"_q, { "", ',', '.', false, false, 0 } },
		//{ u"ZMW"_q, {} },
		//{ u"ANG"_q, {} },
		//{ u"RWF"_q, { "", ' ', ',', true, true, 0 } },
		//{ u"PGK"_q, {} },
		//{ u"TOP"_q, {} },
		//{ u"SBD"_q, {} },
		//{ u"SCR"_q, {} },
		//{ u"SHP"_q, {} },
		//{ u"SLL"_q, {} },
		//{ u"SOS"_q, {} },
		//{ u"SRD"_q, {} },
		//{ u"STD"_q, {} },
		//{ u"SVC"_q, {} },
		//{ u"SZL"_q, {} },
		//{ u"AOA"_q, {} },
		//{ u"AWG"_q, {} },
		//{ u"BBD"_q, {} },
		//{ u"BIF"_q, { "", ',', '.', false, false, 0 } },
		//{ u"BMD"_q, {} },
		//{ u"BSD"_q, {} },
		//{ u"BWP"_q, {} },
		//{ u"BZD"_q, {} },
		//{ u"CDF"_q, { "", ',', '.', false } },
		//{ u"CVE"_q, { "", ',', '.', true, false, 0 } },
		//{ u"DJF"_q, { "", ',', '.', false, false, 0 } },
		//{ u"FJD"_q, {} },
		//{ u"FKP"_q, {} },
		//{ u"GIP"_q, {} },
		//{ u"GMD"_q, { "", ',', '.', false } },
		//{ u"GNF"_q, { "", ',', '.', false, false, 0 } },
		//{ u"GYD"_q, {} },
		//{ u"HTG"_q, {} },
		//{ u"KHR"_q, { "", ',', '.', false } },
		//{ u"KMF"_q, { "", ',', '.', false, false, 0 } },
		//{ u"KYD"_q, {} },
		//{ u"LAK"_q, { "", ',', '.', false } },
		//{ u"LRD"_q, {} },
		//{ u"LSL"_q, { "", ',', '.', false } },
		//{ u"MGA"_q, { "", ',', '.', true, false, 0 } },
		//{ u"MKD"_q, { "", '.', ',', false, true } },
		//{ u"MWK"_q, {} },
		//{ u"NAD"_q, {} },
		//{ u"CLF"_q, { "", ',', '.', true, false, 4 } },
		//{ u"KWD"_q, { "", ',', '.', true, false, 3 } },
		//{ u"LYD"_q, { "", ',', '.', true, false, 3 } },
		//{ u"OMR"_q, { "", ',', '.', true, false, 3 } },
		//{ u"TND"_q, { "", ',', '.', true, false, 3 } },
		//{ u"UYI"_q, { "", ',', '.', true, false, 0 } },
		//{ u"MRO"_q, { "", ',', '.', true, false, 1 } },
	};
	static const auto kRulesMap = [] {
		// flat_multi_map_pair_type lacks some required constructors :(
		auto &&list = kRules | ranges::views::transform([](auto &&pair) {
			return base::flat_multi_map_pair_type<QString, CurrencyRule>(
				pair.first,
				pair.second);
		});
		return base::flat_map<QString, CurrencyRule>(begin(list), end(list));
	}();
	return kRulesMap;
}

} // namespace

CurrencyRule LookupCurrencyRule(const QString &currency) {
	const auto &map = CurrencyRulesMap();
	const auto i = map.find(currency);
	return (i != end(map)) ? i->second : CurrencyRule{};
}

bool KnownCurrency(const QString &currency) {
	return CurrencyRulesMap().contains(currency);
}

QString CurrencyName(const QString &currency) {
	const auto rule = LookupCurrencyRule(currency);
	return (*rule.international)
		? QString::fromUtf8(rule.international)
		: currency;
}

QString CurrencyEnglishName(const QString &currency) {
	static const auto kNames = std::vector<std::pair<QString, QString>>{
		{ u"AED"_q, u"United Arab Emirates Dirham"_q },
		{ u"AFN"_q, u"Afghan Afghani"_q },
		{ u"ALL"_q, u"Albanian Lek"_q },
		{ u"AMD"_q, u"Armenian Dram"_q },
		{ u"ARS"_q, u"Argentine Peso"_q },
		{ u"AUD"_q, u"Australian Dollar"_q },
		{ u"AZN"_q, u"Azerbaijani Manat"_q },
		{ u"BAM"_q, u"Bosnia-Herzegovina Convertible Mark"_q },
		{ u"BDT"_q, u"Bangladeshi Taka"_q },
		{ u"BGN"_q, u"Bulgarian Lev"_q },
		{ u"BHD"_q, u"Bahraini Dinar"_q },
		{ u"BND"_q, u"Brunei Dollar"_q },
		{ u"BOB"_q, u"Bolivian Boliviano"_q },
		{ u"BRL"_q, u"Brazilian Real"_q },
		{ u"BYN"_q, u"Belarusian Ruble"_q },
		{ u"CAD"_q, u"Canadian Dollar"_q },
		{ u"CHF"_q, u"Swiss Franc"_q },
		{ u"CLP"_q, u"Chilean Peso"_q },
		{ u"CNY"_q, u"Chinese Yuan"_q },
		{ u"COP"_q, u"Colombian Peso"_q },
		{ u"CRC"_q, u"Costa Rican Colón"_q },
		{ u"CZK"_q, u"Czech Koruna"_q },
		{ u"DKK"_q, u"Danish Krone"_q },
		{ u"DOP"_q, u"Dominican Peso"_q },
		{ u"DZD"_q, u"Algerian Dinar"_q },
		{ u"EGP"_q, u"Egyptian Pound"_q },
		{ u"ETB"_q, u"Ethiopian Birr"_q },
		{ u"EUR"_q, u"Euro"_q },
		{ u"GBP"_q, u"British Pound"_q },
		{ u"GEL"_q, u"Georgian Lari"_q },
		{ u"GHS"_q, u"Ghanaian Cedi"_q },
		{ u"GTQ"_q, u"Guatemalan Quetzal"_q },
		{ u"HKD"_q, u"Hong Kong Dollar"_q },
		{ u"HNL"_q, u"Honduran Lempira"_q },
		{ u"HRK"_q, u"Croatian Kuna"_q },
		{ u"HUF"_q, u"Hungarian Forint"_q },
		{ u"IDR"_q, u"Indonesian Rupiah"_q },
		{ u"ILS"_q, u"Israeli New Shekel"_q },
		{ u"INR"_q, u"Indian Rupee"_q },
		{ u"IQD"_q, u"Iraqi Dinar"_q },
		{ u"IRR"_q, u"Iranian Rial"_q },
		{ u"ISK"_q, u"Icelandic Króna"_q },
		{ u"JMD"_q, u"Jamaican Dollar"_q },
		{ u"JOD"_q, u"Jordanian Dinar"_q },
		{ u"JPY"_q, u"Japanese Yen"_q },
		{ u"KES"_q, u"Kenyan Shilling"_q },
		{ u"KGS"_q, u"Kyrgyz Som"_q },
		{ u"KRW"_q, u"South Korean Won"_q },
		{ u"KZT"_q, u"Kazakhstani Tenge"_q },
		{ u"LBP"_q, u"Lebanese Pound"_q },
		{ u"LKR"_q, u"Sri Lankan Rupee"_q },
		{ u"MAD"_q, u"Moroccan Dirham"_q },
		{ u"MDL"_q, u"Moldovan Leu"_q },
		{ u"MMK"_q, u"Myanmar Kyat"_q },
		{ u"MNT"_q, u"Mongolian Tugrik"_q },
		{ u"MOP"_q, u"Macanese Pataca"_q },
		{ u"MUR"_q, u"Mauritian Rupee"_q },
		{ u"MVR"_q, u"Maldivian Rufiyaa"_q },
		{ u"MXN"_q, u"Mexican Peso"_q },
		{ u"MYR"_q, u"Malaysian Ringgit"_q },
		{ u"MZN"_q, u"Mozambican Metical"_q },
		{ u"NGN"_q, u"Nigerian Naira"_q },
		{ u"NIO"_q, u"Nicaraguan Córdoba"_q },
		{ u"NOK"_q, u"Norwegian Krone"_q },
		{ u"NPR"_q, u"Nepalese Rupee"_q },
		{ u"NZD"_q, u"New Zealand Dollar"_q },
		{ u"PAB"_q, u"Panamanian Balboa"_q },
		{ u"PEN"_q, u"Peruvian Sol"_q },
		{ u"PHP"_q, u"Philippine Peso"_q },
		{ u"PKR"_q, u"Pakistani Rupee"_q },
		{ u"PLN"_q, u"Polish Zloty"_q },
		{ u"PYG"_q, u"Paraguayan Guarani"_q },
		{ u"QAR"_q, u"Qatari Riyal"_q },
		{ u"RON"_q, u"Romanian Leu"_q },
		{ u"RSD"_q, u"Serbian Dinar"_q },
		{ u"RUB"_q, u"Russian Ruble"_q },
		{ u"SAR"_q, u"Saudi Riyal"_q },
		{ u"SEK"_q, u"Swedish Krona"_q },
		{ u"SGD"_q, u"Singapore Dollar"_q },
		{ u"SYP"_q, u"Syrian Pound"_q },
		{ u"THB"_q, u"Thai Baht"_q },
		{ u"TJS"_q, u"Tajikistani Somoni"_q },
		{ u"TRY"_q, u"Turkish Lira"_q },
		{ u"TTD"_q, u"Trinidad & Tobago Dollar"_q },
		{ u"TWD"_q, u"New Taiwan Dollar"_q },
		{ u"TZS"_q, u"Tanzanian Shilling"_q },
		{ u"UAH"_q, u"Ukrainian Hryvnia"_q },
		{ u"UGX"_q, u"Ugandan Shilling"_q },
		{ u"USD"_q, u"US Dollar"_q },
		{ u"UYU"_q, u"Uruguayan Peso"_q },
		{ u"UZS"_q, u"Uzbekistani Som"_q },
		{ u"VEF"_q, u"Venezuelan Bolívar (2008–2018)"_q },
		{ u"VND"_q, u"Vietnamese Dong"_q },
		{ u"YER"_q, u"Yemeni Rial"_q },
		{ u"ZAR"_q, u"South African Rand"_q },
		{ u"VUV"_q, u"Vanuatu Vatu"_q },
		{ u"WST"_q, u"Samoan Tala"_q },
		{ u"XAF"_q, u"Central African CFA Franc"_q },
		{ u"XCD"_q, u"East Caribbean Dollar"_q },
		{ u"XOF"_q, u"West African CFA Franc"_q },
		{ u"XPF"_q, u"CFP Franc"_q },
		{ u"ZMW"_q, u"Zambian Kwacha"_q },
		{ u"ANG"_q, u"Netherlands Antillean Guilder"_q },
		{ u"RWF"_q, u"Rwandan Franc"_q },
		{ u"PGK"_q, u"Papua New Guinean Kina"_q },
		{ u"TOP"_q, u"Tongan Paʻanga"_q },
		{ u"SBD"_q, u"Solomon Islands Dollar"_q },
		{ u"SCR"_q, u"Seychellois Rupee"_q },
		{ u"SHP"_q, u"St. Helena Pound"_q },
		{ u"SLL"_q, u"Sierra Leonean Leone (1964—2022)"_q },
		{ u"SOS"_q, u"Somali Shilling"_q },
		{ u"SRD"_q, u"Surinamese Dollar"_q },
		{ u"STD"_q, u"São Tomé & Príncipe Dobra (1977–2017)"_q },
		{ u"SVC"_q, u"Salvadoran Colón"_q },
		{ u"SZL"_q, u"Swazi Lilangeni"_q },
		{ u"AOA"_q, u"Angolan Kwanza"_q },
		{ u"AWG"_q, u"Aruban Florin"_q },
		{ u"BBD"_q, u"Barbadian Dollar"_q },
		{ u"BIF"_q, u"Burundian Franc"_q },
		{ u"BMD"_q, u"Bermudan Dollar"_q },
		{ u"BSD"_q, u"Bahamian Dollar"_q },
		{ u"BWP"_q, u"Botswanan Pula"_q },
		{ u"BZD"_q, u"Belize Dollar"_q },
		{ u"CDF"_q, u"Congolese Franc"_q },
		{ u"CVE"_q, u"Cape Verdean Escudo"_q },
		{ u"DJF"_q, u"Djiboutian Franc"_q },
		{ u"FJD"_q, u"Fijian Dollar"_q },
		{ u"FKP"_q, u"Falkland Islands Pound"_q },
		{ u"GIP"_q, u"Gibraltar Pound"_q },
		{ u"GMD"_q, u"Gambian Dalasi"_q },
		{ u"GNF"_q, u"Guinean Franc"_q },
		{ u"GYD"_q, u"Guyanese Dollar"_q },
		{ u"HTG"_q, u"Haitian Gourde"_q },
		{ u"KHR"_q, u"Cambodian Riel"_q },
		{ u"KMF"_q, u"Comorian Franc"_q },
		{ u"KYD"_q, u"Cayman Islands Dollar"_q },
		{ u"LAK"_q, u"Laotian Kip"_q },
		{ u"LRD"_q, u"Liberian Dollar"_q },
		{ u"LSL"_q, u"Lesotho Loti"_q },
		{ u"MGA"_q, u"Malagasy Ariary"_q },
		{ u"MKD"_q, u"Macedonian Denar"_q },
		{ u"MWK"_q, u"Malawian Kwacha"_q },
		{ u"NAD"_q, u"Namibian Dollar"_q },
		{ u"CLF"_q, u"Chilean Unit of Account (UF)"_q },
		{ u"KWD"_q, u"Kuwaiti Dinar"_q },
		{ u"LYD"_q, u"Libyan Dinar"_q },
		{ u"OMR"_q, u"Omani Rial"_q },
		{ u"TND"_q, u"Tunisian Dinar"_q },
		{ u"UYI"_q, u"Uruguayan Peso (Indexed Units)"_q },
		{ u"MRO"_q, u"Mauritanian Ouguiya (1973–2017)"_q },
	};
	static const auto kNamesMap = [] {
		auto &&list = kNames | ranges::views::transform([](auto &&pair) {
			return base::flat_multi_map_pair_type<QString, QString>(
				pair.first,
				pair.second);
		});
		return base::flat_map<QString, QString>(begin(list), end(list));
	}();
	const auto i = kNamesMap.find(currency);
	return (i != end(kNamesMap)) ? i->second : QString();
}

[[nodiscard]] QString FormatWithSeparators(
		double amount,
		int precision,
		char decimal,
		char thousands) {
	Expects(decimal != 0);

	// Thanks https://stackoverflow.com/a/5058949
	struct FormattingHelper : std::numpunct<char> {
		FormattingHelper(char decimal, char thousands)
		: decimal(decimal)
		, thousands(thousands) {
		}

		char do_decimal_point() const override { return decimal; }
		char do_thousands_sep() const override { return thousands; }
		std::string do_grouping() const override { return "\3"; }

		char decimal = '.';
		char thousands = ',';
	};

	auto stream = std::ostringstream();
	stream.imbue(std::locale(
		stream.getloc(),
		new FormattingHelper(decimal, thousands ? thousands : '?')));
	stream.precision(precision);
	stream << std::fixed << amount;
	auto result = QString::fromStdString(stream.str());
	if (!thousands) {
		result.replace('?', QString());
	}
	return result;
}

QString FormatImageSizeText(const QSize &size) {
	return QString::number(size.width())
		+ QChar(215)
		+ QString::number(size.height());
}

QString FormatPhone(QString phone) {
	if (phone.isEmpty()) {
		return QString();
	}
	if (phone.at(0) == '0') {
		return phone;
	}
	phone = phone.remove(QChar::Space);
	return Countries::Instance().format({
		.phone = (phone.at(0) == '+') ? phone.mid(1) : phone,
	}).formatted;
}

QString FormatTTL(float64 ttl) {
	if (ttl < 86400) {
		return tr::lng_hours(tr::now, lt_count, int(ttl / 3600));
	} else if (ttl < 86400 * 7) {
		return tr::lng_days(tr::now, lt_count, int(ttl / (86400)));
	} else if (ttl < 86400 * 31) {
		const auto days = int(ttl / 86400);
		if ((int(ttl) % 7) == 0) {
			return tr::lng_weeks(tr::now, lt_count, int(days / 7));
		} else {
			return tr::lng_weeks(tr::now, lt_count, int(days / 7))
				+ ' '
				+ tr::lng_days(tr::now, lt_count, int(days % 7));
		}
	} else if (ttl <= (86400 * 31) * 11) {
		return tr::lng_months(tr::now, lt_count, int(ttl / (86400 * 31)));
	} else {
		return tr::lng_years({}, lt_count, std::round(ttl / (86400 * 365)));
	}
}

QString FormatTTLAfter(float64 ttl) {
	return (ttl <= 3600 * 23)
		? tr::lng_settings_ttl_after_hours(tr::now, lt_count, int(ttl / 3600))
		: (ttl <= (86400) * 6)
		? tr::lng_settings_ttl_after_days(
			tr::now,
			lt_count,
			int(ttl / (86400)))
		: (ttl <= (86400 * 7) * 3)
		? tr::lng_settings_ttl_after_weeks(
			tr::now,
			lt_count,
			int(ttl / (86400 * 7)))
		: (ttl <= (86400 * 31) * 11)
		? tr::lng_settings_ttl_after_months(
			tr::now,
			lt_count,
			int(ttl / (86400 * 31)))
		: tr::lng_settings_ttl_after_years(
			tr::now,
			lt_count,
			std::round(ttl / (86400 * 365)));
}

QString FormatTTLTiny(float64 ttl) {
	return (ttl <= 3600 * 9)
		? tr::lng_hours_tiny(tr::now, lt_count, int(ttl / 3600))
		: (ttl <= (86400) * 6)
		? tr::lng_days_tiny(tr::now, lt_count, int(ttl / (86400)))
		: (ttl <= (86400 * 7) * 3)
		? tr::lng_weeks_tiny(tr::now, lt_count, int(ttl / (86400 * 7)))
		: (ttl <= (86400 * 31) * 11)
		? tr::lng_months_tiny({}, lt_count, int(ttl / (86400 * 31)))
		: (ttl <= 86400 * 366)
		? tr::lng_years_tiny({}, lt_count, std::round(ttl / (86400 * 365)))
		: QString();
}

QString FormatMuteFor(float64 sec) {
	return (sec <= 60)
		? tr::lng_seconds(tr::now, lt_count, sec)
		: (sec <= 60 * 59)
		? tr::lng_minutes(tr::now, lt_count, int(sec / 60))
		: FormatTTL(sec);
}

QString FormatMuteForTiny(float64 sec) {
	return (sec <= 60)
		? QString()
		: (sec <= 60 * 59)
		? tr::lng_minutes_tiny(tr::now, lt_count, std::round(sec / 60))
		: (sec <= 3600 * 23)
		? tr::lng_hours_tiny(tr::now, lt_count, std::round(sec / 3600))
		: (sec <= 86400 * 6)
		? tr::lng_days_tiny(tr::now, lt_count, std::round(sec / 86400))
		: (sec <= (86400 * 7) * 3)
		? tr::lng_weeks_tiny(tr::now, lt_count, std::round(sec / (86400 * 7)))
		: (sec <= (86400 * 31) * 11)
		? tr::lng_months_tiny({}, lt_count, std::round(sec / (86400 * 31)))
		: (sec <= 86400 * 366)
		? tr::lng_years_tiny({}, lt_count, std::round(sec / (86400 * 365)))
		: QString();
}

QString FormatResetCloudPasswordIn(float64 sec) {
	return (sec >= 3600) ? FormatTTL(sec) : FormatDurationText(sec);
}

QString FormatDialogsDate(const QDateTime &lastTime) {
	// Show all dates that are in the last 20 hours in time format.
	constexpr int kRecentlyInSeconds = 20 * 3600;

	const auto now = QDateTime::currentDateTime();
	const auto nowDate = now.date();
	const auto lastDate = lastTime.date();

	if ((lastDate == nowDate)
		|| (std::abs(lastTime.secsTo(now)) < kRecentlyInSeconds)) {
		return QLocale().toString(lastTime.time(), QLocale::ShortFormat);
	} else if (std::abs(lastDate.daysTo(nowDate)) < 7) {
		return langDayOfWeek(lastDate);
	} else {
		return QLocale().toString(lastDate, QLocale::ShortFormat);
	}
}

} // namespace Ui
