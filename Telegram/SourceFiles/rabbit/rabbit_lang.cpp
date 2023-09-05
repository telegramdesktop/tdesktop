/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#include "rabbit/rabbit_lang.h"

#include "base/parse_helper.h"
#include "lang/lang_tag.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QDir>

namespace RabbitLang {
namespace Lang {
namespace {

const auto kDefaultLanguage = qsl("en");
const std::vector<QString> kPostfixes = {
	"#zero",
	"#one",
	"#two",
	"#few",
	"#many",
	"#other"
};

QString BaseLangCode;
QString LangCode;

QMap<QString, QString> DefaultValues;
QMap<QString, QString> CurrentValues;

rpl::event_stream<> LangChanges;

QString LangDir() {
	return cWorkingDir() + "tdata/rtg_lang/";
}

void ParseLanguageData(
	const QString &langCode,
	bool isDefault) {
	const auto filename = isDefault
		? qsl(":/rtg_lang/%1.json").arg(langCode)
		: LangDir() + (qsl("%1.json").arg(langCode));

	QFile file(filename);
	if (!file.exists()) {
		return;
	}
	if (!file.open(QIODevice::ReadOnly)) {
		LOG(("RabbitLang::Lang Info: file %1 could not be read.").arg(filename));
		return;
	}
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(
		base::parse::stripComments(file.readAll()),
		&error);
	file.close();

	if (error.error != QJsonParseError::NoError) {
		LOG(("RabbitLang::Lang Info: file %1 has failed to parse. Error: %2"
			).arg(filename
			).arg(error.errorString()));
		return;
	} else if (!document.isObject()) {
		LOG(("RabbitLang::Lang Info: file %1 has failed to parse. Error: object expected"
			).arg(filename));
		return;
	}

	const auto applyValue = [&](const QString &name, const QString &translation) {
		if (langCode == kDefaultLanguage) {
			DefaultValues.insert(name, translation);
		} else {
			CurrentValues.insert(name, translation);
		}
	};

	const auto langKeys = document.object();
	const auto keyList = langKeys.keys();

	for (auto i = keyList.constBegin(), e = keyList.constEnd(); i != e; ++i) {
		const auto key = *i;
		if (key.startsWith("dummy_")) {
			continue;
		}

		const auto value = langKeys.constFind(key);

		if ((*value).isString()) {

			applyValue(key, (*value).toString());

		} else if ((*value).isObject()) {

			const auto keyPlurals = (*value).toObject();
			const auto pluralList = keyPlurals.keys();

			for (auto pli = pluralList.constBegin(), ple = pluralList.constEnd(); pli != ple; ++pli) {
				const auto plural = *pli;
				const auto pluralValue = keyPlurals.constFind(plural);

				if (!(*pluralValue).isString()) {
					LOG(("RabbitLang::Lang Info: wrong value for key %1 in %2 in file %3, string expected")
						.arg(plural).arg(key).arg(filename));
					continue;
				}

				const auto name = QString(key + "#" + plural);
				const auto translation = (*pluralValue).toString();

				applyValue(name, translation);
			}
		} else {
			LOG(("RabbitLang::Lang Info: wrong value for key %1 in file %2, string or object expected")
				.arg(key).arg(filename));
		}
	}
}

void UnpackDefault() {
	const auto langDir = LangDir();
	if (!QDir().exists(langDir)) QDir().mkpath(langDir);

	const auto langs = QDir(":/rtg_lang").entryList(QStringList() << "*.json", QDir::Files);
	auto neededLangs = QStringList() << kDefaultLanguage << LangCode << BaseLangCode;
	neededLangs.removeDuplicates();

	for (auto language : langs) {
		language.chop(5);
		if (!neededLangs.contains(language)) {
			continue;
		}

		const auto path = langDir + language + ".default.json";
		auto input = QFile(qsl(":/rtg_lang/%1.json").arg(language));
		auto output = QFile(path);
		if (input.open(QIODevice::ReadOnly)) {
			auto inputData = input.readAll();
			if (output.open(QIODevice::WriteOnly)) {
				output.write(inputData);
				output.close();
			}
			input.close();
		}
	}
}

} // namespace

void Load(const QString &baseLangCode, const QString &langCode) {
	BaseLangCode = baseLangCode;
	if (BaseLangCode.endsWith("-raw")) {
		BaseLangCode.chop(4);
	}

	LangCode = langCode.isEmpty() ? baseLangCode : langCode;
	if (LangCode.endsWith("-raw")) {
		LangCode.chop(4);
	}

	DefaultValues.clear();
	CurrentValues.clear();

	if (BaseLangCode != kDefaultLanguage) {
		ParseLanguageData(kDefaultLanguage, true);
		ParseLanguageData(kDefaultLanguage, false);
	}

	ParseLanguageData(BaseLangCode, true);
	ParseLanguageData(BaseLangCode, false);

	if (LangCode != BaseLangCode) {
		ParseLanguageData(LangCode, true);
		ParseLanguageData(LangCode, false);
	}

	UnpackDefault();
	LangChanges.fire({});
}

QString Translate(const QString &key, Var var1, Var var2, Var var3, Var var4) {
	auto phrase = (CurrentValues.contains(key) && !CurrentValues.value(key).isEmpty())
		? CurrentValues.value(key)
		: DefaultValues.value(key);

	for (const auto &v : { var1, var2, var3, var4 }) {
		if (!v.key.isEmpty()) {
			auto skipNext = false;
			const auto key = qsl("{%1}").arg(v.key);
			const auto neededLength = phrase.length() - key.length();
			for (auto i = 0; i <= neededLength; i++) {
				if (skipNext) {
					skipNext = false;
					continue;
				}
				
				if (phrase.at(i) == QChar('\\')) {
					skipNext = true;
				} else if (phrase.at(i) == QChar('{') && phrase.mid(i, key.length()) == key) {
					phrase.replace(i, key.length(), v.value);
					break;
				}
			}
		}
	}

	return phrase;
}

QString Translate(const QString &key, float64 value, Var var1, Var var2, Var var3, Var var4) {
	const auto shift = ::Lang::PluralShift(value);
	return Translate(key + kPostfixes.at(shift), var1, var2, var3);
}

TextWithEntities TranslateWithEntities(const QString &key, EntVar var1, EntVar var2, EntVar var3, EntVar var4) {
	TextWithEntities phrase = {
		(CurrentValues.contains(key) && !CurrentValues.value(key).isEmpty())
			? CurrentValues.value(key)
			: DefaultValues.value(key)
	};

	for (const auto &v : { var1, var2, var3, var4 }) {
		if (!v.key.isEmpty()) {
			auto skipNext = false;
			const auto key = qsl("{%1}").arg(v.key);
			const auto neededLength = phrase.text.length() - key.length();
			for (auto i = 0; i <= neededLength; i++) {
				if (skipNext) {
					skipNext = false;
					continue;
				}
				
				if (phrase.text.at(i) == QChar('\\')) {
					skipNext = true;
				} else if (phrase.text.at(i) == QChar('{') && phrase.text.mid(i, key.length()) == key) {
					phrase.text.replace(i, key.length(), v.value.text);
					const auto endOld = i + key.length();
					const auto endNew = i + v.value.text.length();

					// Shift existing entities
					if (endNew > endOld) {
						const auto diff = endNew - endOld;
						for (auto &entity : phrase.entities) {
							if (entity.offset() > endOld) {
								entity.shiftRight(diff);
							} else if (entity.offset() <= i && entity.offset() + entity.length() >= endOld) {
								entity.shrinkFromRight(-diff);
							}
						}
					} else if (endNew < endOld) {
						const auto diff = endOld - endNew;
						for (auto &entity : phrase.entities) {
							if (entity.offset() > endNew) {
								entity.shiftLeft(diff);
							} else if (entity.offset() <= i && entity.offset() + entity.length() >= endNew) {
								entity.shrinkFromRight(diff);
							}
						}
					}

					// Add new entities
					for (auto entity : v.value.entities) {
						phrase.entities.append(EntityInText(
							entity.type(),
							entity.offset() + i,
							entity.length(),
							entity.data()));
					}
					break;
				}
			}
		}
	}

	return phrase;
}

TextWithEntities TranslateWithEntities(const QString &key, float64 value, EntVar var1, EntVar var2, EntVar var3, EntVar var4) {
	const auto shift = ::Lang::PluralShift(value);
	return TranslateWithEntities(key + kPostfixes.at(shift), var1, var2, var3, var4);
}

rpl::producer<> Events() {
	return LangChanges.events();
}

} // namespace Lang
} // namespace RabbitLang