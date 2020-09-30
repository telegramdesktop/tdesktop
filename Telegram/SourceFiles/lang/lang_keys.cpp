/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lang/lang_keys.h"

#include "lang/lang_file_parser.h"
#include "ui/integration.h"

namespace {

template <typename WithYear, typename WithoutYear>
inline QString langDateMaybeWithYear(
		QDate date,
		WithYear withYear,
		WithoutYear withoutYear) {
	const auto month = date.month();
	if (month <= 0 || month > 12) {
		return u"MONTH_ERR"_q;
	};
	const auto year = date.year();
	const auto current = QDate::currentDate();
	const auto currentYear = current.year();
	const auto currentMonth = current.month();
	if (year != currentYear) {
		const auto yearIsMuchGreater = [](int year, int otherYear) {
			return (year > otherYear + 1);
		};
		const auto monthIsMuchGreater = [](
				int year,
				int month,
				int otherYear,
				int otherMonth) {
			return (year == otherYear + 1) && (month + 12 > otherMonth + 3);
		};
		if (false
			|| yearIsMuchGreater(year, currentYear)
			|| yearIsMuchGreater(currentYear, year)
			|| monthIsMuchGreater(year, month, currentYear, currentMonth)
			|| monthIsMuchGreater(currentYear, currentMonth, year, month)) {
			return withYear(month, year);
		}
	}
	return withoutYear(month, year);
}

tr::phrase<> Month(int index) {
	switch (index) {
	case 1: return tr::lng_month1;
	case 2: return tr::lng_month2;
	case 3: return tr::lng_month3;
	case 4: return tr::lng_month4;
	case 5: return tr::lng_month5;
	case 6: return tr::lng_month6;
	case 7: return tr::lng_month7;
	case 8: return tr::lng_month8;
	case 9: return tr::lng_month9;
	case 10: return tr::lng_month10;
	case 11: return tr::lng_month11;
	case 12: return tr::lng_month12;
	}
	Unexpected("Index in MonthSmall.");
}

tr::phrase<> MonthSmall(int index) {
	switch (index) {
	case 1: return tr::lng_month1_small;
	case 2: return tr::lng_month2_small;
	case 3: return tr::lng_month3_small;
	case 4: return tr::lng_month4_small;
	case 5: return tr::lng_month5_small;
	case 6: return tr::lng_month6_small;
	case 7: return tr::lng_month7_small;
	case 8: return tr::lng_month8_small;
	case 9: return tr::lng_month9_small;
	case 10: return tr::lng_month10_small;
	case 11: return tr::lng_month11_small;
	case 12: return tr::lng_month12_small;
	}
	Unexpected("Index in MonthSmall.");
}

tr::phrase<> MonthDay(int index) {
	switch (index) {
	case 1: return tr::lng_month_day1;
	case 2: return tr::lng_month_day2;
	case 3: return tr::lng_month_day3;
	case 4: return tr::lng_month_day4;
	case 5: return tr::lng_month_day5;
	case 6: return tr::lng_month_day6;
	case 7: return tr::lng_month_day7;
	case 8: return tr::lng_month_day8;
	case 9: return tr::lng_month_day9;
	case 10: return tr::lng_month_day10;
	case 11: return tr::lng_month_day11;
	case 12: return tr::lng_month_day12;
	}
	Unexpected("Index in MonthDay.");
}

tr::phrase<> Weekday(int index) {
	switch (index) {
	case 1: return tr::lng_weekday1;
	case 2: return tr::lng_weekday2;
	case 3: return tr::lng_weekday3;
	case 4: return tr::lng_weekday4;
	case 5: return tr::lng_weekday5;
	case 6: return tr::lng_weekday6;
	case 7: return tr::lng_weekday7;
	}
	Unexpected("Index in Weekday.");
}

} // namespace

bool langFirstNameGoesSecond() {
	const auto kFirstName = QChar(0x0001);
	const auto kLastName = QChar(0x0002);
	const auto fullname = tr::lng_full_name(
		tr::now,
		lt_first_name,
		QString(1, kFirstName),
		lt_last_name,
		QString(1, kLastName));
	return fullname.indexOf(kLastName) < fullname.indexOf(kFirstName);
}

QString langDayOfMonth(const QDate &date) {
	auto day = date.day();
	return langDateMaybeWithYear(date, [&](int month, int year) {
		return tr::lng_month_day_year(
			tr::now,
			lt_month,
			MonthSmall(month)(tr::now),
			lt_day,
			QString::number(day),
			lt_year,
			QString::number(year));
	}, [day](int month, int year) {
		return tr::lng_month_day(
			tr::now,
			lt_month,
			MonthSmall(month)(tr::now),
			lt_day,
			QString::number(day));
	});
}

QString langDayOfMonthFull(const QDate &date) {
	auto day = date.day();
	return langDateMaybeWithYear(date, [day](int month, int year) {
		return tr::lng_month_day_year(
			tr::now,
			lt_month,
			MonthDay(month)(tr::now),
			lt_day,
			QString::number(day),
			lt_year,
			QString::number(year));
	}, [day](int month, int year) {
		return tr::lng_month_day(
			tr::now,
			lt_month,
			MonthDay(month)(tr::now),
			lt_day,
			QString::number(day));
	});
}

QString langMonthOfYear(int month, int year) {
	return (month > 0 && month <= 12)
		? tr::lng_month_year(
			tr::now,
			lt_month,
			MonthSmall(month)(tr::now),
			lt_year,
			QString::number(year))
		: u"MONTH_ERR"_q;
}

QString langMonth(const QDate &date) {
	return langDateMaybeWithYear(date, [](int month, int year) {
		return langMonthOfYear(month, year);
	}, [](int month, int year) {
		return MonthSmall(month)(tr::now);
	});
}

QString langMonthOfYearFull(int month, int year) {
	return (month > 0 && month <= 12)
		? tr::lng_month_year(
			tr::now,
			lt_month,
			Month(month)(tr::now),
			lt_year,
			QString::number(year))
		: u"MONTH_ERR"_q;
}

QString langMonthFull(const QDate &date) {
	return langDateMaybeWithYear(date, [](int month, int year) {
		return langMonthOfYearFull(month, year);
	}, [](int month, int year) {
		return Month(month)(tr::now);
	});
}

QString langDayOfWeek(int index) {
	return (index > 0 && index <= 7) ? Weekday(index)(tr::now) : u"DAY_ERR"_q;
}

QString langDateTime(const QDateTime &date) {
	return tr::lng_mediaview_date_time(
		tr::now,
		lt_date,
		langDayOfMonth(date.date()),
		lt_time,
		date.time().toString(Ui::Integration::Instance().timeFormat()));
}

QString langDateTimeFull(const QDateTime &date) {
	return tr::lng_mediaview_date_time(
		tr::now,
		lt_date,
		langDayOfMonthFull(date.date()),
		lt_time,
		date.time().toString(Ui::Integration::Instance().timeFormat()));
}
