/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/text/format_values.h"

#include "lang/lang_keys.h"

#include <QtCore/QLocale>
#include <locale>
#include <sstream>
#include <iostream>

namespace Ui {
namespace {

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

QString FormatDateTime(QDateTime date, QString format) {
	const auto now = QDateTime::currentDateTime();
	if (date.date() == now.date()) {
		return tr::lng_mediaview_today(
			tr::now,
			lt_time,
			date.time().toString(format));
	} else if (date.date().addDays(1) == now.date()) {
		return tr::lng_mediaview_yesterday(
			tr::now,
			lt_time,
			date.time().toString(format));
	} else {
		return tr::lng_mediaview_date_time(
			tr::now,
			lt_date,
			date.date().toString(u"dd.MM.yy"_q),
			lt_time,
			date.time().toString(format));
	}
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
	return tr::lng_duration_seconds(tr::now, lt_count, duration);
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
	const auto rule = LookupCurrencyRule(currency);

	const auto prefix = (amount < 0)
		? QString::fromUtf8("\xe2\x88\x92")
		: QString();
	const auto value = std::abs(amount) / std::pow(10., rule.exponent);
	const auto name = (*rule.international)
		? QString::fromUtf8(rule.international)
		: currency;
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

CurrencyRule LookupCurrencyRule(const QString &currency) {
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
		{ u"BND"_q, { "", '.', ',', } },
		{ u"BOB"_q, { "", '.', ',', true, true } },
		{ u"BRL"_q, { "R$", '.', ',', true, true } },
		{ u"BHD"_q, { "", ',', '.', true, true, 3 } },
		{ u"BYR"_q, { "", ' ', ',', false, true, 0 } },
		{ u"CAD"_q, { "CA$" } },
		{ u"CHF"_q, { "", '\'', '.', false, true } },
		{ u"CLP"_q, { "", '.', ',', true, true, 0 } },
		{ u"CNY"_q, { "\x43\x4E\xC2\xA5" } },
		{ u"COP"_q, { "", '.', ',', true, true } },
		{ u"CRC"_q, { "", '.', ',', } },
		{ u"CZK"_q, { "", ' ', ',', false, true } },
		{ u"DKK"_q, { "", '\0', ',', false, true } },
		{ u"DOP"_q, {} },
		{ u"DZD"_q, { "", ',', '.', true, true } },
		{ u"EGP"_q, { "", ',', '.', true, true } },
		{ u"EUR"_q, { "\xE2\x82\xAC", ' ', ',', false, true } },
		{ u"GBP"_q, { "\xC2\xA3" } },
		{ u"GEL"_q, { "", ' ', ',', false, true } },
		{ u"GTQ"_q, {} },
		{ u"HKD"_q, { "HK$" } },
		{ u"HNL"_q, { "", ',', '.', true, true } },
		{ u"HRK"_q, { "", '.', ',', false, true } },
		{ u"HUF"_q, { "", ' ', ',', false, true } },
		{ u"IDR"_q, { "", '.', ',', } },
		{ u"ILS"_q, { "\xE2\x82\xAA", ',', '.', true, true } },
		{ u"INR"_q, { "\xE2\x82\xB9" } },
		{ u"ISK"_q, { "", '.', ',', false, true, 0 } },
		{ u"JMD"_q, {} },
		{ u"JPY"_q, { "\xC2\xA5", ',', '.', true, false, 0 } },
		{ u"KES"_q, {} },
		{ u"KGS"_q, { "", ' ', '-', false, true } },
		{ u"KRW"_q, { "\xE2\x82\xA9", ',', '.', true, false, 0 } },
		{ u"KZT"_q, { "", ' ', '-', } },
		{ u"LBP"_q, { "", ',', '.', true, true } },
		{ u"LKR"_q, { "", ',', '.', true, true } },
		{ u"MAD"_q, { "", ',', '.', true, true } },
		{ u"MDL"_q, { "", ',', '.', false, true } },
		{ u"MNT"_q, { "", ' ', ',', } },
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
		{ u"VND"_q, { "\xE2\x82\xAB", '.', ',', false, true, 0 } },
		{ u"YER"_q, { "", ',', '.', true, true } },
		{ u"ZAR"_q, { "", ',', '.', true, true } },
		{ u"IRR"_q, { "", ',', '/', false, true, 2, true } },
		{ u"IQD"_q, { "", ',', '.', true, true, 3 } },
		{ u"VEF"_q, { "", '.', ',', true, true } },
		{ u"SYP"_q, { "", ',', '.', true, true } },

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
		//{ u"ETB"_q, {} },
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
		//{ u"MOP"_q, {} },
		//{ u"MWK"_q, {} },
		//{ u"NAD"_q, {} },
		//{ u"CLF"_q, { "", ',', '.', true, false, 4 } },
		//{ u"JOD"_q, { "", ',', '.', true, false, 3 } },
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
	const auto i = kRulesMap.find(currency);
	return (i != end(kRulesMap)) ? i->second : CurrencyRule{};
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

QString ComposeNameString(
		const QString &filename,
		const QString &songTitle,
		const QString &songPerformer) {
	if (songTitle.isEmpty() && songPerformer.isEmpty()) {
		return filename.isEmpty() ? u"Unknown File"_q : filename;
	}

	if (songPerformer.isEmpty()) {
		return songTitle;
	}

	auto trackTitle = (songTitle.isEmpty() ? u"Unknown Track"_q : songTitle);
	return songPerformer + QString::fromUtf8(" \xe2\x80\x93 ") + trackTitle;
}

} // namespace Ui
