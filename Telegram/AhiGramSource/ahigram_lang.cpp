/*
This file is part of AhiGram,
a fork of Telegram Desktop with additional features.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

// This code is licensed under GPLv3. Attribution to original source is appreciated.
// Author: https://github.com/DyingLay

AhiGram: Localization implementation
*/

#include "ahigram_lang.h"

#include "base/basic_types.h"
#include "lang/lang_instance.h"

#include "settings.h"

#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QFileInfo>
#include <unordered_map>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif

namespace AhiGram {
namespace {

std::unordered_map<Language, QMap<QString, QString>> g_translations;
rpl::variable<Language> g_currentLanguage = Language::English;
rpl::lifetime g_languageLifetime;

bool LoadTranslations(Language lang, const QString &path){
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)){
        return false;
    }
    QMap<QString, QString> translations;
    QTextStream stream(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    stream.setEncoding(QStringConverter::Utf8);
#else
    stream.setCodec("UTF-8");
#endif

    while(!stream.atEnd()){
        const auto line = stream.readLine().trimmed();
        if(line.isEmpty() || line.startsWith(u"//"_q)) continue;

        const auto eqPos = line.indexOf(u'=');
        if(eqPos <= 0) continue;

        auto key = line.mid(0, eqPos).trimmed();
        auto value = line.mid(eqPos + 1).trimmed();

        if(key.startsWith(QChar('"')) && key.endsWith(QChar('"'))){
            key = key.mid(1, key.length() - 2);
        }
		if (value.startsWith(QChar('"'))) {
			value = value.mid(1);
		}
		if (value.endsWith(QChar(';'))) {
			value = value.left(value.length() - 1);
		}
		if (value.endsWith(QChar('"'))) {
			value = value.left(value.length() - 1);
		}

        translations[key] = value;
    }

    g_translations[lang] = translations;
    return true;
}

void InitializeLanguage() {
	static bool initialized = false;
	if (!initialized) {
		initialized = true;
		const auto langId = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto currentLangId = baseId.isEmpty() ? langId : baseId;
		const auto langCode = currentLangId.left(2).toLower();
		
		g_currentLanguage = (langCode == u"ru") ? Language::Russian : Language::English;
		
		Lang::GetInstance().idChanges(
		) | rpl::start_with_next([=](const QString &newLangId) {
			const auto newBaseId = Lang::GetInstance().baseId();
			const auto newCurrentLangId = newBaseId.isEmpty() ? newLangId : newBaseId;
			const auto newLangCode = newCurrentLangId.left(2).toLower();
			g_currentLanguage = (newLangCode == u"ru") ? Language::Russian : Language::English;
		}, g_languageLifetime);
	}
}

} // namespace

void InitializeLang() {
	static bool initialized = false;
	if (initialized) return;
	initialized = true;
	
	const auto exeDir = cExeDir();
	QDir dir(exeDir);
	
	auto basePath = dir.absoluteFilePath(u"AhiGramSource/langs/"_q);
	QDir baseDir(basePath);
	
	const auto enPath = baseDir.absoluteFilePath(u"ahigram.en.strings"_q);
	const auto ruPath = baseDir.absoluteFilePath(u"ahigram.ru.strings"_q);
	
	if (!LoadTranslations(Language::English, enPath)) {
		const auto srcFile = QFileInfo(__FILE__);
		const auto srcDir = srcFile.absoluteDir();
		const auto langDir = QDir(srcDir.absoluteFilePath(u"langs"_q));
		const auto langPath = langDir.absoluteFilePath(u"ahigram.en.strings"_q);
		LoadTranslations(Language::English, langPath);
	}
	
	if (!LoadTranslations(Language::Russian, ruPath)) {
		const auto srcFile = QFileInfo(__FILE__);
		const auto srcDir = srcFile.absoluteDir();
		const auto langDir = QDir(srcDir.absoluteFilePath(u"langs"_q));
		const auto langPath = langDir.absoluteFilePath(u"ahigram.ru.strings"_q);
		LoadTranslations(Language::Russian, langPath);
	}
	
	if (g_translations[Language::Russian].empty()) {
		g_translations[Language::Russian] = g_translations[Language::English];
	}
}

Language GetCurrentLanguage() {
    InitializeLanguage();
    return g_currentLanguage.current();
}

QString GetLanguageId() {
	return GetCurrentLanguage() == Language::Russian ? u"ru"_q : u"en"_q;
}

QString tr(const QString &key) {
	InitializeLanguage();
	InitializeLang();
	
	const auto lang = g_currentLanguage.current();
	const auto &translations = g_translations[lang];
	
	const auto it = translations.find(key);
	if (it != translations.end()) {
		return it.value();
	}
	
	const auto &enTranslations = g_translations[Language::English];
	const auto enIt = enTranslations.find(key);
	if (enIt != enTranslations.end()) {
		return enIt.value();
	}
	
	return u"<missing: "_q + key + u">"_q;
}

rpl::producer<QString> trReactive(const QString &key) {
	InitializeLanguage();
	InitializeLang();
	
	return g_currentLanguage.value(
	) | rpl::map([=](Language lang) {
		InitializeLanguage();
		InitializeLang();
		
		const auto &translations = g_translations[lang];
		const auto it = translations.find(key);
		if (it != translations.end()) {
			return it.value();
		}
		
		const auto &enTranslations = g_translations[Language::English];
		const auto enIt = enTranslations.find(key);
		if (enIt != enTranslations.end()) {
			return enIt.value();
		}
		
		return u"<missing: "_q + key + u">"_q;
	}) | rpl::distinct_until_changed();
}

rpl::producer<QString> languageChanges() {
	InitializeLanguage();
	return g_currentLanguage.value(
	) | rpl::map([](Language lang) {
		return (lang == Language::Russian) ? u"ru"_q : u"en"_q;
	}) | rpl::distinct_until_changed();
}

} // namespace AhiGram