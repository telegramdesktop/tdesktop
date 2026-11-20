/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

enum class BotInfoField : uchar {
	Name,
	About,
	Description,
};

inline constexpr auto kBotInfoFields = std::array{
	BotInfoField::Name,
	BotInfoField::About,
	BotInfoField::Description,
};

enum class BotInfoProblem : uchar {
	None,
	TooLong,
	EmptyName,
};

struct BotInfoTexts {
	QString name;
	QString about;
	QString description;

	[[nodiscard]] QString &field(BotInfoField field);
	[[nodiscard]] const QString &field(BotInfoField field) const;
	[[nodiscard]] bool empty() const;

	friend inline bool operator==(
		const BotInfoTexts &,
		const BotInfoTexts &) = default;
};

struct BotInfoChange {
	std::optional<QString> name;
	std::optional<QString> about;
	std::optional<QString> description;

	[[nodiscard]] std::optional<QString> &field(BotInfoField field);
	[[nodiscard]] const std::optional<QString> &field(
		BotInfoField field) const;
	[[nodiscard]] bool empty() const;
	[[nodiscard]] int count() const;

	friend inline bool operator==(
		const BotInfoChange &,
		const BotInfoChange &) = default;
};

struct BotInfoLanguage {
	QString code;
	BotInfoTexts server;
	BotInfoChange staged;
	QString error;
	std::array<bool, kBotInfoFields.size()> translated = {};

	[[nodiscard]] bool fallback() const;
	[[nodiscard]] QString value(BotInfoField field) const;
	[[nodiscard]] BotInfoProblem problem(BotInfoField field) const;
	[[nodiscard]] bool hasProblem() const;
	[[nodiscard]] bool hasTranslated() const;
};

[[nodiscard]] int BotInfoFieldLimit(BotInfoField field);
[[nodiscard]] int BotInfoTextLength(const QString &text);
[[nodiscard]] QString NormalizeBotInfoText(BotInfoField field, QString text);
[[nodiscard]] bool ValidBotInfoLangCode(const QString &code);

class BotInfoDraft final {
public:
	BotInfoDraft();

	[[nodiscard]] const std::vector<BotInfoLanguage> &languages() const;
	[[nodiscard]] const BotInfoLanguage *find(const QString &code) const;
	[[nodiscard]] const BotInfoLanguage &fallback() const;

	void add(const QString &code);
	void setServer(const QString &code, BotInfoTexts texts);
	void stage(
		const QString &code,
		BotInfoField field,
		QString text,
		bool translated = false);
	void setError(const QString &code, const QString &error);
	void applied(const QString &code, const BotInfoChange &change);
	void discard();

	[[nodiscard]] int changesCount() const;

	[[nodiscard]] rpl::producer<> updates() const;

private:
	[[nodiscard]] BotInfoLanguage *lookup(const QString &code);
	[[nodiscard]] BotInfoLanguage &ensure(const QString &code);

	std::vector<BotInfoLanguage> _languages;
	rpl::event_stream<> _updates;

};

} // namespace Data
