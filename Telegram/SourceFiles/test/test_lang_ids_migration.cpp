/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_lang_ids_migration.h"

#include "chat_helpers/spellchecker_locale_ids.h"
#include "core/core_settings.h"

#include <QtCore/QLocale>

namespace Test {
namespace {

using Spellchecker::ConvertLangId;
using Spellchecker::LangIdSchemeForAppVersion;
using Spellchecker::RecoverLangIdFromSerialized;

constexpr auto kFirstQt6Release = 7003002;

void Check(
		LangIdsMigrationReport &report,
		bool ok,
		const QString &what) {
	if (!ok) {
		report.failures.push_back(what);
	}
}

[[nodiscard]] LanguageId Lang(int value) {
	return { QLocale::Language(value) };
}

[[nodiscard]] bool Contains(
		const std::vector<LanguageId> &list,
		LanguageId id) {
	for (const auto &item : list) {
		if (item == id) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] QByteArray OldFormatBlob(const Core::Settings &from) {
	const auto full = from.serialize();
	const auto translateToCode = QLocale::languageToCode(
		QLocale::Language(from.translateTo().value));
	auto tail = sizeof(qint32) // format marker
		+ sizeof(qint32); // skip list count
	for (const auto &id : from.skipTranslationLanguages()) {
		const auto code = QLocale::languageToCode(id.value);
		tail += sizeof(quint32) + code.size() * sizeof(ushort);
	}
	tail += sizeof(quint32) + translateToCode.size() * sizeof(ushort);
	return full.mid(0, full.size() - tail);
}

[[nodiscard]] QByteArray OldFormatBlobUnset(const Core::Settings &from) {
	const auto full = from.serialize();
	return full.mid(0, full.size() - (3 * sizeof(qint32)));
}

void CheckTable(LangIdsMigrationReport &report) {
	const auto pairs = {
		std::pair{ 42, 94 }, // German
		std::pair{ 28, 67 }, // Czech
		std::pair{ 96, 239 }, // Russian
		std::pair{ 31, 75 }, // English
		std::pair{ 25, 58 }, // Chinese
		std::pair{ 59, 120 }, // Japanese
		std::pair{ 142, 42 }, // Bosnian
		std::pair{ 188, 28 }, // Bambara
		std::pair{ 43, 96 }, // Greek
	};
	for (const auto &[v5, v6] : pairs) {
		Check(report, ConvertLangId(v5, 5, 6) == v6, u"anchor 5->6"_q);
		Check(report, ConvertLangId(v6, 6, 5) == v5, u"anchor 6->5"_q);
	}
	Check(report, ConvertLangId(0, 5, 6) == 0, u"zero edge"_q);
	Check(report, ConvertLangId(-5, 5, 6) == 0, u"negative edge"_q);
	Check(
		report,
		ConvertLangId(1 << 20, 5, 6) == (1 << 20),
		u"huge passes through"_q);
	Check(
		report,
		RecoverLangIdFromSerialized(1 << 20, 5) == 0,
		u"huge dropped by recovery"_q);
	Check(report, ConvertLangId(94, 6, 6) == 94, u"same scheme identity"_q);
	Check(
		report,
		ConvertLangId(42 * 1000 + 82, 5, 6) == 94 * 1000 + 91,
		u"compound 5->6"_q);
	Check(
		report,
		ConvertLangId(94 * 1000 + 91, 6, 5) == 42 * 1000 + 82,
		u"compound 6->5"_q);

	auto roundTrips = 0;
	for (auto v = 2; v != 400; ++v) {
		if (v > int(QLocale::LastLanguage)) {
			break;
		}
		const auto converted = ConvertLangId(v, 5, 6);
		if (!converted) {
			++roundTrips;
			continue;
		}
		Check(
			report,
			ConvertLangId(converted, 6, 5) == v,
			u"table roundtrip"_q);
		++roundTrips;
	}
	Check(report, roundTrips > 250, u"table size sane"_q);

	for (auto v = 2; v != int(QLocale::LastLanguage); ++v) {
		const auto language = QLocale::Language(v);
		const auto code = QLocale::languageToCode(language);
		Check(report, !code.isEmpty(), u"empty code"_q);
		Check(
			report,
			QLocale::codeToLanguage(code) == language,
			u"code roundtrip"_q);
	}
}

void CheckSchemeForAppVersion(LangIdsMigrationReport &report) {
	Check(report, LangIdSchemeForAppVersion(0) == 6, u"scheme fresh"_q);
	Check(report, LangIdSchemeForAppVersion(1) == 5, u"scheme ancient"_q);
	Check(report, LangIdSchemeForAppVersion(7002009) == 5, u"scheme 7.2.9"_q);
	Check(report, LangIdSchemeForAppVersion(7003001) == 5, u"scheme 7.3.1"_q);
	Check(report, LangIdSchemeForAppVersion(kFirstQt6Release) == 6,
		u"scheme 7.3.2"_q);
	Check(report, LangIdSchemeForAppVersion(7003003) == 6, u"scheme next"_q);
}

void CheckRecover(LangIdsMigrationReport &report) {
	Check(report, RecoverLangIdFromSerialized(0, 5) == 0, u"recover unset"_q);
	Check(report, RecoverLangIdFromSerialized(1, 5) == 0, u"recover C"_q);
	Check(report, RecoverLangIdFromSerialized(42, 5) == 94,
		u"recover German"_q);
	Check(report, RecoverLangIdFromSerialized(94, 6) == 94,
		u"recover identity"_q);
	Check(report, RecoverLangIdFromSerialized(4294967296ull, 5) == 0,
		u"recover overflow"_q);
	Check(report, RecoverLangIdFromSerialized(0xFFFFFFFFull, 5) == 0,
		u"garbage target dropped"_q);
}

void CheckBlob(LangIdsMigrationReport &report) {
	auto origin = Core::Settings();
	origin.setSkipTranslationLanguages({ Lang(94), Lang(31) });
	origin.setTranslateTo(Lang(84));

	const auto blob = origin.serialize();

	auto reader = Core::Settings();
	reader.addFromSerialized(blob, kFirstQt6Release);
	Check(report, reader.translateTo() == Lang(84), u"tail target kept"_q);
	const auto readerSkip = reader.skipTranslationLanguages();
	Check(
		report,
		readerSkip.size() == 2
			&& Contains(readerSkip, Lang(94))
			&& Contains(readerSkip, Lang(31)),
		u"tail skip kept"_q);
	Check(report, reader.serialize() == blob, u"blob byte-stable"_q);

	auto current = Core::Settings();
	current.addFromSerialized(OldFormatBlob(origin), kFirstQt6Release);
	Check(report, current.translateTo() == Lang(84), u"qt6 file target"_q);

	const auto savedOnce = current.serialize();
	auto currentAgain = Core::Settings();
	currentAgain.addFromSerialized(savedOnce, kFirstQt6Release);
	Check(
		report,
		currentAgain.serialize() == savedOnce,
		u"qt6 file save idempotent"_q);

	auto migrated = Core::Settings();
	migrated.addFromSerialized(OldFormatBlob(origin), kFirstQt6Release - 1);
	Check(
		report,
		migrated.translateTo() == Lang(ConvertLangId(84, 5, 6)),
		u"qt5 file target migrated"_q);
	const auto migratedSkip = migrated.skipTranslationLanguages();
	Check(
		report,
		migratedSkip.size() == 2
			&& Contains(migratedSkip, Lang(ConvertLangId(94, 5, 6)))
			&& Contains(migratedSkip, Lang(ConvertLangId(31, 5, 6))),
		u"qt5 file skip migrated"_q);
}

void CheckUnsetTarget(LangIdsMigrationReport &report) {
	auto origin = Core::Settings();
	origin.setSkipTranslationLanguages({});
	origin.setTranslateTo(LanguageId{ QLocale::AnyLanguage });
	const auto blob = origin.serialize();

	auto reader = Core::Settings();
	reader.addFromSerialized(blob, kFirstQt6Release);
	Check(report, reader.serialize() == blob, u"unset target byte-stable"_q);

	auto migrated = Core::Settings();
	migrated.addFromSerialized(OldFormatBlobUnset(origin), kFirstQt6Release - 1);
	Check(report, migrated.serialize() == blob, u"unset target migrated"_q);
}

} // namespace

LangIdsMigrationReport RunLangIdsMigrationSelfTest() {
	auto report = LangIdsMigrationReport();
	CheckTable(report);
	CheckSchemeForAppVersion(report);
	CheckRecover(report);
	CheckBlob(report);
	CheckUnsetTarget(report);
	report.passed = report.failures.isEmpty();
	return report;
}

} // namespace Test

#endif // _DEBUG
