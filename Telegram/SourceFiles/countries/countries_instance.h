/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

#pragma once

namespace Countries {

struct Info {
	const char *name = nullptr;
	const char *iso2 = nullptr;
	const char *code = nullptr;
	const char *alternativeName = nullptr;
};

[[nodiscard]] const std::array<Info, 231> &List();

[[nodiscard]] const QHash<QString, const Info *> &InfoByCode();
[[nodiscard]] const QHash<QString, const Info *> &InfoByISO2();

[[nodiscard]] QString ValidPhoneCode(QString fullCode);
[[nodiscard]] QString CountryNameByISO2(const QString &iso);
[[nodiscard]] QString CountryISO2ByPhone(const QString &phone);

} // namespace Countries
