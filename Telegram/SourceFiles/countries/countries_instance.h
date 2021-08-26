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

class CountriesInstance final {
public:
	using Map = QHash<QString, const Info *>;

	CountriesInstance();
	[[nodiscard]] const std::vector<Info> &list();
	void setList(std::vector<Info> &&infos);

	[[nodiscard]] const Map &byCode();
	[[nodiscard]] const Map &byISO2();

	[[nodiscard]] QString validPhoneCode(QString fullCode);
	[[nodiscard]] QString countryNameByISO2(const QString &iso);
	[[nodiscard]] QString countryISO2ByPhone(const QString &phone);

private:
	std::vector<Info> _list;

	Map _byCode;
	Map _byISO2;

};

CountriesInstance &Instance();

} // namespace Countries
