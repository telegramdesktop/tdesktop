/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_birthday.h"

#include "base/timer_rpl.h"
#include "lang/lang_keys.h"

#include <QtCore/QDate>

namespace Data {
namespace {

[[nodiscard]] bool Validate(int day, int month, int year) {
	if (year != 0
		&& (year < Birthday::kYearMin || year > Birthday::kYearMax)) {
		return false;
	} else if (day < 1) {
		return false;
	} else if (month == 2) {
		if (day == 29) {
			return !year
				|| (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
		}
		return day <= 28;
	} else if (month == 4 || month == 6 || month == 9 || month == 11) {
		return day <= 30;
	} else if (month > 0 && month <= 12) {
		return day <= 31;
	}
	return false;
}

[[nodiscard]] int Serialize(int day, int month, int year) {
	return day + month * 100 + year * 10000;
}

} // namespace

Birthday::Birthday(int day, int month, int year)
: _value(Validate(day, month, year) ? Serialize(day, month, year) : 0) {
}

Birthday Birthday::FromSerialized(int value) {
	return Birthday(value % 100, (value / 100) % 100, value / 10000);
}

int Birthday::serialize() const {
	return _value;
}

bool Birthday::valid() const {
	return _value != 0;
}

int Birthday::day() const {
	return _value % 100;
}

int Birthday::month() const {
	return (_value / 100) % 100;
}

int Birthday::year() const {
	return _value / 10000;
}

QString BirthdayText(Birthday date) {
	if (const auto year = date.year()) {
		return tr::lng_month_day_year(
			tr::now,
			lt_month,
			Lang::MonthSmall(date.month())(tr::now),
			lt_day,
			QString::number(date.day()),
			lt_year,
			QString::number(year));
	} else if (date) {
		return tr::lng_month_day(
			tr::now,
			lt_month,
			Lang::MonthSmall(date.month())(tr::now),
			lt_day,
			QString::number(date.day()));
	}
	return QString();
}

QString BirthdayCake() {
	return QString::fromUtf8("\xf0\x9f\x8e\x82");
}

int BirthdayAge(Birthday date) {
	if (!date.year()) {
		return 0;
	}
	const auto now = QDate::currentDate();
	const auto day = QDate(date.year(), date.month(), date.day());
	if (!day.isValid() || day >= now) {
		return 0;
	}
	auto age = now.year() - date.year();
	if (now < QDate(date.year() + age, date.month(), date.day())) {
		--age;
	}
	return age;
}

bool IsBirthdayToday(Birthday date) {
	if (!date) {
		return false;
	}
	const auto now = QDate::currentDate();
	return date.day() == now.day() && date.month() == now.month();
}

rpl::producer<bool> IsBirthdayTodayValue(Birthday date) {
	return rpl::single() | rpl::then(base::timer_each(
		60 * crl::time(1000)
	)) | rpl::map([=] {
		return IsBirthdayToday(date);
	}) | rpl::distinct_until_changed();
}

} // namespace Data
