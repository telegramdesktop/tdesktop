/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

#pragma once

namespace Countries {

struct CallingCodeInfo {
	QString callingCode;
	std::vector<QString> prefixes;
	std::vector<QString> patterns;
};

struct Info {
	QString name;
	QString iso2;
	QString alternativeName;
	std::vector<CallingCodeInfo> codes;
	bool isHidden = false;
};

struct FormatResult {
	QString formatted;
	QVector<int> groups;
	QString code;
};

struct FormatArgs {
	QString phone;
	bool onlyGroups = false;
	bool skipCode = false;
	bool incomplete = false;
	bool onlyCode = false;
};

class CountriesInstance final {
public:
	using Map = QHash<QString, const Info *>;

	CountriesInstance();
	[[nodiscard]] const std::vector<Info> &list() const;
	void setList(std::vector<Info> &&infos);

	[[nodiscard]] const Map &byCode() const;
	[[nodiscard]] const Map &byISO2() const;

	[[nodiscard]] QString validPhoneCode(QString fullCode) const;
	[[nodiscard]] QString countryNameByISO2(const QString &iso) const;
	[[nodiscard]] QString countryISO2ByPhone(const QString &phone) const;
	[[nodiscard]] QString flagEmojiByISO2(const QString &iso) const;

	[[nodiscard]] FormatResult format(FormatArgs args) const;

	[[nodiscard]] rpl::producer<> updated() const;

private:
	mutable std::vector<Info> _list;

	mutable Map _byCode;
	mutable Map _byISO2;

	rpl::event_stream<> _updated;

};

CountriesInstance &Instance();

[[nodiscard]] QString ExtractPhoneCode(const QString &phone);
[[nodiscard]] QVector<int> Groups(const QString &phone);

} // namespace Countries
