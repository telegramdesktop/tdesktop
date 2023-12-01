/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/statistics_format_values.h"

#include "base/unixtime.h"
#include "lang/lang_keys.h"

#include <QtCore/QLocale>

namespace Statistic {

QString LangDayMonthYear(crl::time seconds) {
	const auto date = base::unixtime::parse(seconds).date();
	return tr::lng_stats_day_month_year(
		tr::now,
		lt_days_count,
		QString::number(date.day()),
		lt_month,
		Lang::MonthSmall(date.month())(tr::now),
		lt_year,
		QString::number(date.year()));
}

QString LangDayMonth(crl::time seconds) {
	const auto date = base::unixtime::parse(seconds).date();
	return tr::lng_stats_day_month(
		tr::now,
		lt_days_count,
		QString::number(date.day()),
		lt_month,
		Lang::MonthSmall(date.month())(tr::now));
}

QString LangDetailedDayMonth(crl::time seconds) {
	const auto dateTime = base::unixtime::parse(seconds);
	if (dateTime.toUTC().time().hour() || dateTime.toUTC().time().minute()) {
		constexpr auto kOneDay = 3600 * 24;
		if (seconds < kOneDay) {
			return QLocale().toString(dateTime, QLocale::ShortFormat);
		}
		return tr::lng_stats_weekday_day_month_time(
			tr::now,
			lt_day,
			Lang::Weekday(dateTime.date().dayOfWeek())(tr::now),
			lt_days_count,
			QString::number(dateTime.date().day()),
			lt_month,
			Lang::MonthSmall(dateTime.date().month())(tr::now),
			lt_time,
			QLocale().toString(dateTime.time(), QLocale::ShortFormat));
	} else {
		return tr::lng_stats_weekday_day_month_year(
			tr::now,
			lt_day,
			Lang::Weekday(dateTime.date().dayOfWeek())(tr::now),
			lt_days_count,
			QString::number(dateTime.date().day()),
			lt_month,
			Lang::MonthSmall(dateTime.date().month())(tr::now),
			lt_year,
			QString::number(dateTime.date().year()));
	}
}

} // namespace Statistic
