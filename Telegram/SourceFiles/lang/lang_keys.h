/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "lang/lang_instance.h"
#include "lang/lang_hardcoded.h"
#include "lang/lang_text_entity.h"

QString langDayOfMonth(const QDate &date);
QString langDayOfMonthFull(const QDate &date);
QString langMonthOfYear(int month, int year);
QString langMonth(const QDate &date);
QString langMonthOfYearFull(int month, int year);
QString langMonthFull(const QDate &date);
QString langDayOfWeek(int index);

inline QString langDayOfWeek(const QDate &date) {
	return langDayOfWeek(date.dayOfWeek());
}

inline QString langDateTime(const QDateTime &date) {
	return tr::lng_mediaview_date_time(
		tr::now,
		lt_date,
		langDayOfMonth(date.date()),
		lt_time,
		date.time().toString(cTimeFormat()));
}

inline QString langDateTimeFull(const QDateTime &date) {
	return tr::lng_mediaview_date_time(
		tr::now,
		lt_date,
		langDayOfMonthFull(date.date()),
		lt_time,
		date.time().toString(cTimeFormat()));
}

bool langFirstNameGoesSecond();
