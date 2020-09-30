/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "lang_auto.h"
#include "lang/lang_hardcoded.h"
#include "lang/lang_text_entity.h"

[[nodiscard]] QString langDayOfMonth(const QDate &date);
[[nodiscard]] QString langDayOfMonthFull(const QDate &date);
[[nodiscard]] QString langMonthOfYear(int month, int year);
[[nodiscard]] QString langMonth(const QDate &date);
[[nodiscard]] QString langMonthOfYearFull(int month, int year);
[[nodiscard]] QString langMonthFull(const QDate &date);
[[nodiscard]] QString langDayOfWeek(int index);

[[nodiscard]] inline QString langDayOfWeek(const QDate &date) {
	return langDayOfWeek(date.dayOfWeek());
}

[[nodiscard]] QString langDateTime(const QDateTime &date);
[[nodiscard]] QString langDateTimeFull(const QDateTime &date);
[[nodiscard]] bool langFirstNameGoesSecond();

namespace Lang {

[[nodiscard]] QString Id();
[[nodiscard]] rpl::producer<> Updated();
[[nodiscard]] QString GetNonDefaultValue(const QByteArray &key);

} // namespace Lang
