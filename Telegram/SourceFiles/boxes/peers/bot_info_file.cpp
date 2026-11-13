/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/bot_info_file.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace {

using Field = Data::BotInfoField;

constexpr auto kMaxFileSize = 1024 * 1024;

const auto kBotKey = u"bot"_q;
const auto kLanguagesKey = u"languages"_q;
const auto kDefaultKey = u"default"_q;
const auto kShortDescriptionKey = u"short_description"_q;

[[nodiscard]] QString FieldKey(Field field) {
	switch (field) {
	case Field::Name: return u"name"_q;
	case Field::About: return u"about"_q;
	case Field::Description: return u"description"_q;
	}
	Unexpected("Field in FieldKey.");
}

[[nodiscard]] std::optional<Field> FieldByKey(const QString &key) {
	if (key == kShortDescriptionKey) {
		return Field::About;
	}
	for (const auto field : Data::kBotInfoFields) {
		if (key == FieldKey(field)) {
			return field;
		}
	}
	return std::nullopt;
}

[[nodiscard]] BotInfoFile Failed(
		BotInfoFileError error,
		const QString &where = QString()) {
	return { .error = error, .where = where };
}

[[nodiscard]] BotInfoFile SyntaxFailed(
		const QByteArray &content,
		const QJsonParseError &error) {
	const auto before = content.left(error.offset);
	auto result = Failed(BotInfoFileError::Syntax);
	result.details = error.errorString();
	result.line = int(before.count('\n')) + 1;
	result.column = int(before.size() - before.lastIndexOf('\n'));
	return result;
}

[[nodiscard]] BotInfoFileError ParseLanguage(
		const QJsonObject &object,
		const QString &path,
		BotInfoFileLanguage &language,
		QString &where) {
	for (auto i = object.begin(); i != object.end(); ++i) {
		where = path + u'.' + i.key();
		const auto field = FieldByKey(i.key());
		if (!field) {
			return BotInfoFileError::UnknownKey;
		} else if (!i.value().isString()) {
			return BotInfoFileError::NotString;
		}
		auto &text = language.texts.field(*field);
		if (text) {
			return BotInfoFileError::Duplicate;
		}
		text = i.value().toString();
	}
	return BotInfoFileError::None;
}

} // namespace

BotInfoFile ParseBotInfoFile(const QByteArray &content) {
	if (content.size() > kMaxFileSize) {
		return Failed(BotInfoFileError::TooLarge);
	}
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(content, &error);
	if (error.error != QJsonParseError::NoError) {
		return SyntaxFailed(content, error);
	} else if (!document.isObject()) {
		return Failed(BotInfoFileError::Shape);
	}
	const auto root = document.object();
	for (auto i = root.begin(); i != root.end(); ++i) {
		if ((i.key() != kBotKey) && (i.key() != kLanguagesKey)) {
			return Failed(BotInfoFileError::UnknownKey, i.key());
		}
	}
	const auto bot = root.value(kBotKey);
	const auto languages = root.value(kLanguagesKey);
	if (!bot.isUndefined() && !bot.isString()) {
		return Failed(BotInfoFileError::NotString, kBotKey);
	} else if (!languages.isObject()) {
		return Failed(BotInfoFileError::Shape);
	}
	auto result = BotInfoFile{ .bot = bot.toString() };
	const auto list = languages.toObject();
	for (auto i = list.begin(); i != list.end(); ++i) {
		const auto path = kLanguagesKey + u'.' + i.key();
		const auto lower = i.key().trimmed().toLower();
		const auto code = (lower == kDefaultKey) ? QString() : lower;
		if (!code.isEmpty() && !Data::ValidBotInfoLangCode(code)) {
			return Failed(BotInfoFileError::BadCode, i.key());
		} else if (!i.value().isObject()) {
			return Failed(BotInfoFileError::Shape, path);
		} else if (ranges::contains(
				result.languages,
				code,
				&BotInfoFileLanguage::code)) {
			return Failed(BotInfoFileError::Duplicate, path);
		}
		auto language = BotInfoFileLanguage{ .code = code };
		auto where = QString();
		const auto failed = ParseLanguage(
			i.value().toObject(),
			path,
			language,
			where);
		if (failed != BotInfoFileError::None) {
			return Failed(failed, where);
		}
		result.languages.push_back(std::move(language));
	}
	return result;
}

QByteArray SerializeBotInfoFile(
		const QString &bot,
		const Data::BotInfoDraft &draft) {
	auto languages = QJsonObject();
	for (const auto &language : draft.languages()) {
		auto texts = QJsonObject();
		for (const auto field : Data::kBotInfoFields) {
			const auto text = language.value(field);
			if (!text.isEmpty()) {
				texts.insert(FieldKey(field), text);
			}
		}
		if (language.fallback() || !texts.isEmpty()) {
			languages.insert(
				language.fallback() ? kDefaultKey : language.code,
				texts);
		}
	}
	auto root = QJsonObject();
	if (!bot.isEmpty()) {
		root.insert(kBotKey, bot);
	}
	root.insert(kLanguagesKey, languages);
	return QJsonDocument(root).toJson(QJsonDocument::Indented);
}
