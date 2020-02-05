/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/spellchecker_common.h"

#ifndef TDESKTOP_DISABLE_SPELLCHECK

#include "base/zlib_help.h"

namespace Spellchecker {

namespace {

// Language With Country.
inline auto LWC(QLocale::Country country) {
	const auto l = QLocale::matchingLocales(
		QLocale::AnyLanguage,
		QLocale::AnyScript,
		country)[0];
	return (l.language() * 1000) + country;
}

const auto kDictionaries = {
	Dict{ QLocale::Bulgarian,            12,   229'658, "\xd0\x91\xd1\x8a\xd0\xbb\xd0\xb3\xd0\xb0\xd1\x80\xd1\x81\xd0\xba\xd0\xb8" },
	Dict{ QLocale::Catalan,              13,   417'611, "\x43\x61\x74\x61\x6c\xc3\xa0" },
	Dict{ QLocale::Czech,                14,   860'286, "\xc4\x8c\x65\xc5\xa1\x74\x69\x6e\x61" },
	Dict{ QLocale::Welsh,                15,   177'305, "\x43\x79\x6d\x72\x61\x65\x67" },
	Dict{ QLocale::Danish,               16,   345'874, "\x44\x61\x6e\x73\x6b" },
	Dict{ QLocale::German,               17, 2'412'780, "\x44\x65\x75\x74\x73\x63\x68" },
	Dict{ QLocale::Greek,                18, 1'389'160, "\xce\x95\xce\xbb\xce\xbb\xce\xb7\xce\xbd\xce\xb9\xce\xba\xce\xac" },
	Dict{ LWC(QLocale::Australia),       19,   175'266, "English (Australia)" },
	Dict{ LWC(QLocale::Canada),          20,   174'295, "English (Canada)" },
	Dict{ LWC(QLocale::UnitedKingdom),   21,   174'433, "English (United Kingdom)" },
	Dict{ QLocale::English,              22,   174'516, "English" },
	Dict{ QLocale::Spanish,              23,   264'717, "\x45\x73\x70\x61\xc3\xb1\x6f\x6c" },
	Dict{ QLocale::Estonian,             24,   757'394, "\x45\x65\x73\x74\x69" },
	Dict{ QLocale::Persian,              25,   333'911, "\xd9\x81\xd8\xa7\xd8\xb1\xd8\xb3\xdb\x8c" },
	Dict{ QLocale::French,               26,   321'391, "\x46\x72\x61\x6e\xc3\xa7\x61\x69\x73" },
	Dict{ QLocale::Hebrew,               27,   622'550, "\xd7\xa2\xd7\x91\xd7\xa8\xd7\x99\xd7\xaa" },
	Dict{ QLocale::Hindi,                28,    56'105, "\xe0\xa4\xb9\xe0\xa4\xbf\xe0\xa4\xa8\xe0\xa5\x8d\xe0\xa4\xa6\xe0\xa5\x80" },
	Dict{ QLocale::Croatian,             29,   668'876, "\x48\x72\x76\x61\x74\x73\x6b\x69" },
	Dict{ QLocale::Hungarian,            30,   660'402, "\x4d\x61\x67\x79\x61\x72" },
	Dict{ QLocale::Armenian,             31,   928'746, "\xd5\x80\xd5\xa1\xd5\xb5\xd5\xa5\xd6\x80\xd5\xa5\xd5\xb6" },
	Dict{ QLocale::Indonesian,           32,   100'134, "\x49\x6e\x64\x6f\x6e\x65\x73\x69\x61" },
	Dict{ QLocale::Italian,              33,   324'613, "\x49\x74\x61\x6c\x69\x61\x6e\x6f" },
	Dict{ QLocale::Korean,               34, 1'256'987, "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4" },
	Dict{ QLocale::Lithuanian,           35,   267'427, "\x4c\x69\x65\x74\x75\x76\x69\xc5\xb3" },
	Dict{ QLocale::Latvian,              36,   641'602, "\x4c\x61\x74\x76\x69\x65\xc5\xa1\x75" },
	Dict{ QLocale::Norwegian,            37,   588'650, "\x4e\x6f\x72\x73\x6b" },
	Dict{ QLocale::Dutch,                38,   743'406, "\x4e\x65\x64\x65\x72\x6c\x61\x6e\x64\x73" },
	Dict{ QLocale::Polish,               39, 1'015'747, "\x50\x6f\x6c\x73\x6b\x69" },
	Dict{ LWC(QLocale::Brazil),          40, 1'231'999, "\x50\x6f\x72\x74\x75\x67\x75\xc3\xaa\x73 (Brazil)" },
	Dict{ QLocale::Portugal,             41,   138'571, "\x50\x6f\x72\x74\x75\x67\x75\xc3\xaa\x73" },
	Dict{ QLocale::Romanian,             42,   455'643, "\x52\x6f\x6d\xc3\xa2\x6e\xc4\x83" },
	Dict{ QLocale::Russian,              43,   463'194, "\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9" },
	Dict{ QLocale::Slovak,               44,   525'328, "\x53\x6c\x6f\x76\x65\x6e\xc4\x8d\x69\x6e\x61" },
	Dict{ QLocale::Slovenian,            45, 1'143'710, "\x53\x6c\x6f\x76\x65\x6e\xc5\xa1\xc4\x8d\x69\x6e\x61" },
	Dict{ QLocale::Albanian,             46,   583'412, "\x53\x68\x71\x69\x70" },
	Dict{ QLocale::Swedish,              47,   593'877, "\x53\x76\x65\x6e\x73\x6b\x61" },
	Dict{ QLocale::Tamil,                48,   323'193, "\xe0\xae\xa4\xe0\xae\xae\xe0\xae\xbf\xe0\xae\xb4\xe0\xaf\x8d" },
	Dict{ QLocale::Tajik,                49,   369'931, "\xd0\xa2\xd0\xbe\xd2\xb7\xd0\xb8\xd0\xba\xd3\xa3" },
	Dict{ QLocale::Turkish,              50, 4'301'099, "\x54\xc3\xbc\x72\x6b\xc3\xa7\x65" },
	Dict{ QLocale::Ukrainian,            51,   445'711, "\xd0\xa3\xd0\xba\xd1\x80\xd0\xb0\xd1\x97\xd0\xbd\xd1\x81\xd1\x8c\xd0\xba\xd0\xb0" },
	Dict{ QLocale::Vietnamese,           52,    12'949, "\x54\x69\xe1\xba\xbf\x6e\x67\x20\x56\x69\xe1\xbb\x87\x74" },
};

QLocale LocaleFromLangId(int langId) {
	if (langId > 1000) {
		const auto l = langId / 1000;
		const auto lang = static_cast<QLocale::Language>(l);
		const auto country = static_cast<QLocale::Country>(langId - l * 1000);
		return QLocale(lang, country);
	}
	return QLocale(static_cast<QLocale::Language>(langId));
}

void EnsurePath() {
	if (!QDir::current().mkpath(Spellchecker::DictionariesPath())) {
		LOG(("App Error: Could not create dictionaries path."));
	}
}

QByteArray ReadFinalFile(const QString &path) {
	constexpr auto kMaxZipSize = 10 * 1024 * 1024; //12
	auto file = QFile(path);
	if (file.size() > kMaxZipSize || !file.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	return file.readAll();
}

bool ExtractZipFile(zlib::FileToRead &zip, const QString path) {
	constexpr auto kMaxSize = 10 * 1024 * 1024;
	const auto content = zip.readCurrentFileContent(kMaxSize);
	if (content.isEmpty() || zip.error() != UNZ_OK) {
		return false;
	}
	auto file = QFile(path);
	return file.open(QIODevice::WriteOnly)
		&& (file.write(content) == content.size());
}

} // namespace

std::initializer_list<const Dict> Dictionaries() {
	return kDictionaries;
}

bool IsGoodPartName(const QString &name) {
	return name.endsWith(qsl(".dic"))
		|| name.endsWith(qsl(".aff"));
}

QString DictPathByLangId(int langId) {
	EnsurePath();
	return qsl("%1/%2")
		.arg(DictionariesPath())
		.arg(LocaleFromLangId(langId).name());
}

QString DictionariesPath() {
	return cWorkingDir() + qsl("tdata/dictionaries");
}

bool UnpackDictionary(const QString &path, int langId) {
	const auto folder = DictPathByLangId(langId);
	const auto bytes = ReadFinalFile(path);
	if (bytes.isEmpty()) {
		return false;
	}
	auto zip = zlib::FileToRead(bytes);
	if (zip.goToFirstFile() != UNZ_OK) {
		return false;
	}
	do {
		const auto name = zip.getCurrentFileName();
		const auto path = folder + '/' + name;
		if (IsGoodPartName(name) && !ExtractZipFile(zip, path)) {
			return false;
		}

		const auto jump = zip.goToNextFile();
		if (jump == UNZ_END_OF_LIST_OF_FILE) {
			break;
		} else if (jump != UNZ_OK) {
			return false;
		}
	} while (true);
	return true;
}

bool DictionaryExists(int langId) {
	if (!langId) {
		return true;
	}
	const auto folder = DictPathByLangId(langId) + '/';
	const auto exts = { "dic", "aff" };
	const auto bad = ranges::find_if(exts, [&](const QString &ext) {
		const auto name = LocaleFromLangId(langId).name();
		return !QFile(folder + name + '.' + ext).exists();
	});
	return (bad == exts.end());
}

bool WriteDefaultDictionary() {
	// This is an unused function.
	const auto en = QLocale::English;
	if (DictionaryExists(en)) {
		return false;
	}
	const auto fileName = QLocale(en).name();
	const auto folder = qsl("%1/%2/")
		.arg(DictionariesPath())
		.arg(fileName);
	QDir(folder).removeRecursively();

	const auto path = folder + fileName;
	QDir().mkpath(folder);
	auto input = QFile(qsl(":/misc/en_US_dictionary"));
	auto output = QFile(path);
	if (input.open(QIODevice::ReadOnly)
		&& output.open(QIODevice::WriteOnly)) {
		output.write(input.readAll());
		const auto result = Spellchecker::UnpackDictionary(path, en);
		output.remove();
		return result;
	}
	return false;
}

} // namespace Spellchecker

#endif // !TDESKTOP_DISABLE_SPELLCHECK
