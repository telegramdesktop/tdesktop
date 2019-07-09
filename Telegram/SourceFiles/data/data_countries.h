/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

#pragma once

namespace Data {

struct CountryInfo {
	const char *name = nullptr;
	const char *iso2 = nullptr;
	const char *code = nullptr;
	const char *alternativeName = nullptr;
};

[[nodiscard]] const std::array<CountryInfo, 231> &Countries();

[[nodiscard]] const QHash<QString, const CountryInfo *> &CountriesByCode();
[[nodiscard]] const QHash<QString, const CountryInfo *> &CountriesByISO2();

[[nodiscard]] QString ValidPhoneCode(QString fullCode);
[[nodiscard]] QString CountryNameByISO2(const QString &iso);
[[nodiscard]] QString CountryISO2ByPhone(const QString &phone);

} // namespace Data
