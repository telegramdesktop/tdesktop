/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/text/format_values.h"

#include "lang/lang_keys.h"

#include <QtCore/QLocale>

namespace Ui {

namespace {

QString FormatTextWithReadyAndTotal(
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

QString FillAmountAndCurrency(uint64 amount, const QString &currency) {
	static const auto ShortCurrencyNames = QMap<QString, QString>{
		{ u"USD"_q, QString::fromUtf8("\x24") },
		{ u"GBP"_q, QString::fromUtf8("\xC2\xA3") },
		{ u"EUR"_q, QString::fromUtf8("\xE2\x82\xAC") },
		{ u"JPY"_q, QString::fromUtf8("\xC2\xA5") },
	};
	static const auto Denominators = QMap<QString, int>{
		{ u"CLF"_q, 10000 },
		{ u"BHD"_q, 1000 },
		{ u"IQD"_q, 1000 },
		{ u"JOD"_q, 1000 },
		{ u"KWD"_q, 1000 },
		{ u"LYD"_q, 1000 },
		{ u"OMR"_q, 1000 },
		{ u"TND"_q, 1000 },
		{ u"BIF"_q, 1 },
		{ u"BYR"_q, 1 },
		{ u"CLP"_q, 1 },
		{ u"CVE"_q, 1 },
		{ u"DJF"_q, 1 },
		{ u"GNF"_q, 1 },
		{ u"ISK"_q, 1 },
		{ u"JPY"_q, 1 },
		{ u"KMF"_q, 1 },
		{ u"KRW"_q, 1 },
		{ u"MGA"_q, 1 },
		{ u"PYG"_q, 1 },
		{ u"RWF"_q, 1 },
		{ u"UGX"_q, 1 },
		{ u"UYI"_q, 1 },
		{ u"VND"_q, 1 },
		{ u"VUV"_q, 1 },
		{ u"XAF"_q, 1 },
		{ u"XOF"_q, 1 },
		{ u"XPF"_q, 1 },
		{ u"MRO"_q, 10 },
	};
	const auto currencyText = ShortCurrencyNames.value(currency, currency);
	const auto denominator = Denominators.value(currency, 100);
	const auto currencyValue = amount / float64(denominator);
	const auto digits = [&] {
		auto result = 0;
		for (auto test = 1; test < denominator; test *= 10) {
			++result;
		}
		return result;
	}();
	return QLocale::system().toCurrencyString(currencyValue, currencyText);
	//auto amountBucks = amount / 100;
	//auto amountCents = amount % 100;
	//auto amountText = u"%1,%2").arg(amountBucks).arg(amountCents, 2, 10, QChar('0'));
	//return currencyText + amountText;
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
