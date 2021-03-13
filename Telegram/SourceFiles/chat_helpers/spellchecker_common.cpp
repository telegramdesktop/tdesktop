/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/spellchecker_common.h"

#ifndef TDESKTOP_DISABLE_SPELLCHECK

#include "base/platform/base_platform_info.h"
#include "base/zlib_help.h"
#include "data/data_session.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "spellcheck/platform/platform_spellcheck.h"
#include "spellcheck/spellcheck_utils.h"
#include "spellcheck/spellcheck_value.h"
#include "core/application.h"
#include "core/core_settings.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QInputMethod>

namespace Spellchecker {

namespace {

using namespace Storage::CloudBlob;

constexpr auto kDictExtensions = { "dic", "aff" };

constexpr auto kExceptions = {
	AppFile,
	"\xd0\xa2\xd0\xb5\xd0\xbb\xd0\xb5\xd0\xb3\xd1\x80\xd0\xb0\xd0\xbc"_cs,
};

// 31 - QLocale::English, 91 - QLocale::Portuguese.
constexpr auto kLangsForLWC = { 31, 91 };
// 225 - QLocale::UnitesStates, 30 - QLocale::Brazil.
constexpr auto kDefaultCountries = { 225, 30 };

// Language With Country.
inline auto LWC(QLocale::Country country) {
	const auto l = QLocale::matchingLocales(
		QLocale::AnyLanguage,
		QLocale::AnyScript,
		country)[0];
	if (ranges::contains(kDefaultCountries, country)) {
		return int(l.language());
	}
	return (l.language() * 1000) + country;
}

inline auto LanguageFromLocale(QLocale loc) {
	const auto locLang = int(loc.language());
	return (ranges::contains(kLangsForLWC, locLang)
		&& (loc.country() != QLocale::AnyCountry))
			? LWC(loc.country())
			: locLang;
}

const auto kDictionaries = {
	Dict{{ QLocale::English,             649,   174'516, "English" }}, // en_US
	Dict{{ QLocale::Bulgarian,           594,   229'658, "\xd0\x91\xd1\x8a\xd0\xbb\xd0\xb3\xd0\xb0\xd1\x80\xd1\x81\xd0\xba\xd0\xb8" }}, // bg_BG
	Dict{{ QLocale::Catalan,             595,   417'611, "\x43\x61\x74\x61\x6c\xc3\xa0" }}, // ca_ES
	Dict{{ QLocale::Czech,               596,   860'286, "\xc4\x8c\x65\xc5\xa1\x74\x69\x6e\x61" }}, // cs_CZ
	Dict{{ QLocale::Welsh,               597,   177'305, "\x43\x79\x6d\x72\x61\x65\x67" }}, // cy_GB
	Dict{{ QLocale::Danish,              598,   345'874, "\x44\x61\x6e\x73\x6b" }}, // da_DK
	Dict{{ QLocale::German,              599, 2'412'780, "\x44\x65\x75\x74\x73\x63\x68" }}, // de_DE
	Dict{{ QLocale::Greek,               600, 1'389'160, "\xce\x95\xce\xbb\xce\xbb\xce\xb7\xce\xbd\xce\xb9\xce\xba\xce\xac" }}, // el_GR
	Dict{{ LWC(QLocale::Australia),      601,   175'266, "English (Australia)" }}, // en_AU
	Dict{{ LWC(QLocale::Canada),         602,   174'295, "English (Canada)" }}, // en_CA
	Dict{{ LWC(QLocale::UnitedKingdom),  603,   174'433, "English (United Kingdom)" }}, // en_GB
	Dict{{ QLocale::Spanish,             604,   264'717, "\x45\x73\x70\x61\xc3\xb1\x6f\x6c" }}, // es_ES
	Dict{{ QLocale::Estonian,            605,   757'394, "\x45\x65\x73\x74\x69" }}, // et_EE
	Dict{{ QLocale::Persian,             606,   333'911, "\xd9\x81\xd8\xa7\xd8\xb1\xd8\xb3\xdb\x8c" }}, // fa_IR
	Dict{{ QLocale::French,              607,   321'391, "\x46\x72\x61\x6e\xc3\xa7\x61\x69\x73" }}, // fr_FR
	Dict{{ QLocale::Hebrew,              608,   622'550, "\xd7\xa2\xd7\x91\xd7\xa8\xd7\x99\xd7\xaa" }}, // he_IL
	Dict{{ QLocale::Hindi,               609,    56'105, "\xe0\xa4\xb9\xe0\xa4\xbf\xe0\xa4\xa8\xe0\xa5\x8d\xe0\xa4\xa6\xe0\xa5\x80" }}, // hi_IN
	Dict{{ QLocale::Croatian,            610,   668'876, "\x48\x72\x76\x61\x74\x73\x6b\x69" }}, // hr_HR
	Dict{{ QLocale::Hungarian,           611,   660'402, "\x4d\x61\x67\x79\x61\x72" }}, // hu_HU
	Dict{{ QLocale::Armenian,            612,   928'746, "\xd5\x80\xd5\xa1\xd5\xb5\xd5\xa5\xd6\x80\xd5\xa5\xd5\xb6" }}, // hy_AM
	Dict{{ QLocale::Indonesian,          613,   100'134, "\x49\x6e\x64\x6f\x6e\x65\x73\x69\x61" }}, // id_ID
	Dict{{ QLocale::Italian,             614,   324'613, "\x49\x74\x61\x6c\x69\x61\x6e\x6f" }}, // it_IT
	Dict{{ QLocale::Korean,              615, 1'256'987, "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4" }}, // ko_KR
	Dict{{ QLocale::Lithuanian,          616,   267'427, "\x4c\x69\x65\x74\x75\x76\x69\xc5\xb3" }}, // lt_LT
	Dict{{ QLocale::Latvian,             617,   641'602, "\x4c\x61\x74\x76\x69\x65\xc5\xa1\x75" }}, // lv_LV
	Dict{{ QLocale::NorwegianBokmal,     618,   588'650, "\x4e\x6f\x72\x73\x6b" }}, // nb_NO
	Dict{{ QLocale::Dutch,               619,   743'406, "\x4e\x65\x64\x65\x72\x6c\x61\x6e\x64\x73" }}, // nl_NL
	Dict{{ QLocale::Polish,              620, 1'015'747, "\x50\x6f\x6c\x73\x6b\x69" }}, // pl_PL
	Dict{{ QLocale::Portuguese,          621, 1'231'999, "\x50\x6f\x72\x74\x75\x67\x75\xc3\xaa\x73 (Brazil)" }}, // pt_BR
	Dict{{ LWC(QLocale::Portugal),       622,   138'571, "\x50\x6f\x72\x74\x75\x67\x75\xc3\xaa\x73" }}, // pt_PT
	Dict{{ QLocale::Romanian,            623,   455'643, "\x52\x6f\x6d\xc3\xa2\x6e\xc4\x83" }}, // ro_RO
	Dict{{ QLocale::Russian,             624,   463'194, "\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9" }}, // ru_RU
	Dict{{ QLocale::Slovak,              625,   525'328, "\x53\x6c\x6f\x76\x65\x6e\xc4\x8d\x69\x6e\x61" }}, // sk_SK
	Dict{{ QLocale::Slovenian,           626, 1'143'710, "\x53\x6c\x6f\x76\x65\x6e\xc5\xa1\xc4\x8d\x69\x6e\x61" }}, // sl_SI
	Dict{{ QLocale::Albanian,            627,   583'412, "\x53\x68\x71\x69\x70" }}, // sq_AL
	Dict{{ QLocale::Swedish,             628,   593'877, "\x53\x76\x65\x6e\x73\x6b\x61" }}, // sv_SE
	Dict{{ QLocale::Tamil,               629,   323'193, "\xe0\xae\xa4\xe0\xae\xae\xe0\xae\xbf\xe0\xae\xb4\xe0\xaf\x8d" }}, // ta_IN
	Dict{{ QLocale::Tajik,               630,   369'931, "\xd0\xa2\xd0\xbe\xd2\xb7\xd0\xb8\xd0\xba\xd3\xa3" }}, // tg_TG
	Dict{{ QLocale::Turkish,             631, 4'301'099, "\x54\xc3\xbc\x72\x6b\xc3\xa7\x65" }}, // tr_TR
	Dict{{ QLocale::Ukrainian,           632,   445'711, "\xd0\xa3\xd0\xba\xd1\x80\xd0\xb0\xd1\x97\xd0\xbd\xd1\x81\xd1\x8c\xd0\xba\xd0\xb0" }}, // uk_UA
	Dict{{ QLocale::Vietnamese,          633,    12'949, "\x54\x69\xe1\xba\xbf\x6e\x67\x20\x56\x69\xe1\xbb\x87\x74" }}, // vi_VN
	// The Tajik code is 'tg_TG' in Chromium, but QT has only 'tg_TJ'.
};

inline auto IsSupportedLang(int lang) {
	return ranges::contains(kDictionaries, lang, &Dict::id);
}

void EnsurePath() {
	if (!QDir::current().mkpath(Spellchecker::DictionariesPath())) {
		LOG(("App Error: Could not create dictionaries path."));
	}
}

bool IsGoodPartName(const QString &name) {
	return ranges::any_of(kDictExtensions, [&](const auto &ext) {
		return name.endsWith(ext);
	});
}

using DictLoaderPtr = std::shared_ptr<base::unique_qptr<DictLoader>>;

DictLoaderPtr BackgroundLoader;
rpl::event_stream<int> BackgroundLoaderChanged;

void SetBackgroundLoader(DictLoaderPtr loader) {
	BackgroundLoader = std::move(loader);
}

void DownloadDictionaryInBackground(
		not_null<Main::Session*> session,
		int counter,
		std::vector<int> langs) {
	const auto id = langs[counter];
	counter++;
	const auto destroyer = [=] {
		BackgroundLoader = nullptr;
		BackgroundLoaderChanged.fire(0);

		if (DictionaryExists(id)) {
			auto dicts = Core::App().settings().dictionariesEnabled();
			if (!ranges::contains(dicts, id)) {
				dicts.push_back(id);
				Core::App().settings().setDictionariesEnabled(std::move(dicts));
				Core::App().saveSettingsDelayed();
			}
		}

		if (counter >= langs.size()) {
			return;
		}
		DownloadDictionaryInBackground(session, counter, langs);
	};
	if (DictionaryExists(id)) {
		destroyer();
		return;
	}

	auto sharedLoader = std::make_shared<base::unique_qptr<DictLoader>>();
	*sharedLoader = base::make_unique_q<DictLoader>(
		QCoreApplication::instance(),
		session,
		id,
		GetDownloadLocation(id),
		DictPathByLangId(id),
		GetDownloadSize(id),
		crl::guard(session, destroyer));
	SetBackgroundLoader(std::move(sharedLoader));
	BackgroundLoaderChanged.fire_copy(id);
}

void AddExceptions() {
	const auto exceptions = ranges::views::all(
		kExceptions
	) | ranges::views::transform([](const auto &word) {
		return word.utf16();
	}) | ranges::views::filter([](const auto &word) {
		return !(Platform::Spellchecker::IsWordInDictionary(word)
			|| Spellchecker::IsWordSkippable(&word));
	}) | ranges::to_vector;

	ranges::for_each(exceptions, Platform::Spellchecker::AddWord);
}

} // namespace

DictLoaderPtr GlobalLoader() {
	return BackgroundLoader;
}

rpl::producer<int> GlobalLoaderChanged() {
	return BackgroundLoaderChanged.events();
}

DictLoader::DictLoader(
	QObject *parent,
	not_null<Main::Session*> session,
	int id,
	MTP::DedicatedLoader::Location location,
	const QString &folder,
	int size,
	Fn<void()> destroyCallback)
: BlobLoader(parent, session, id, location, folder, size)
, _destroyCallback(std::move(destroyCallback)) {
}

void DictLoader::unpack(const QString &path) {
	crl::async([=] {
		const auto success = Spellchecker::UnpackDictionary(path, id());
		if (success) {
			QFile(path).remove();
			destroy();
			return;
		}
		crl::on_main([=] { fail(); });
	});
}

void DictLoader::destroy() {
	Expects(_destroyCallback);

	crl::on_main(_destroyCallback);
}

void DictLoader::fail() {
	BlobLoader::fail();
	destroy();
}

std::vector<Dict> Dictionaries() {
	return kDictionaries | ranges::to_vector;
}

int GetDownloadSize(int id) {
	return ranges::find(kDictionaries, id, &Spellchecker::Dict::id)->size;
}

MTP::DedicatedLoader::Location GetDownloadLocation(int id) {
	const auto username = kCloudLocationUsername.utf16();
	const auto i = ranges::find(kDictionaries, id, &Spellchecker::Dict::id);
	return MTP::DedicatedLoader::Location{ username, i->postId };
}

QString DictPathByLangId(int langId) {
	EnsurePath();
	return qsl("%1/%2").arg(
		DictionariesPath(),
		Spellchecker::LocaleFromLangId(langId).name());
}

QString DictionariesPath() {
	return cWorkingDir() + qsl("tdata/dictionaries");
}

bool UnpackDictionary(const QString &path, int langId) {
	const auto folder = DictPathByLangId(langId);
	return UnpackBlob(path, folder, IsGoodPartName);
}

bool DictionaryExists(int langId) {
	if (!langId) {
		return true;
	}
	const auto folder = DictPathByLangId(langId) + '/';
	return ranges::none_of(kDictExtensions, [&](const auto &ext) {
		const auto name = Spellchecker::LocaleFromLangId(langId).name();
		return !QFile(folder + name + '.' + ext).exists();
	});
}

bool RemoveDictionary(int langId) {
	if (!langId) {
		return true;
	}
	const auto fileName = Spellchecker::LocaleFromLangId(langId).name();
	const auto folder = qsl("%1/%2/").arg(
		DictionariesPath(),
		fileName);
	return QDir(folder).removeRecursively();
}

bool WriteDefaultDictionary() {
	// This is an unused function.
	const auto en = QLocale::English;
	if (DictionaryExists(en)) {
		return false;
	}
	const auto fileName = QLocale(en).name();
	const auto folder = qsl("%1/%2/").arg(
		DictionariesPath(),
		fileName);
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

rpl::producer<QString> ButtonManageDictsState(
		not_null<Main::Session*> session) {
	if (Platform::Spellchecker::IsSystemSpellchecker()) {
		return rpl::single(QString());
	}
	const auto computeString = [=] {
		if (!Core::App().settings().spellcheckerEnabled()) {
			return QString();
		}
		if (!Core::App().settings().dictionariesEnabled().size()) {
			return QString();
		}
		const auto dicts = Core::App().settings().dictionariesEnabled();
		const auto filtered = ranges::views::all(
			dicts
		) | ranges::views::filter(
			DictionaryExists
		) | ranges::to_vector;
		const auto active = Platform::Spellchecker::ActiveLanguages();

		return (active.size() == filtered.size())
			? QString::number(filtered.size())
			: tr::lng_contacts_loading(tr::now);
	};
	return rpl::single(
		computeString()
	) | rpl::then(
		rpl::merge(
			Spellchecker::SupportedScriptsChanged(),
			Core::App().settings().dictionariesEnabledChanges(
			) | rpl::to_empty,
			Core::App().settings().spellcheckerEnabledChanges(
			) | rpl::to_empty
		) | rpl::map(computeString)
	);
}

std::vector<int> DefaultLanguages() {
	std::vector<int> langs;

	const auto append = [&](const auto loc) {
		const auto l = LanguageFromLocale(loc);
		if (!ranges::contains(langs, l) && IsSupportedLang(l)) {
			langs.push_back(l);
		}
	};

	const auto method = QGuiApplication::inputMethod();
	langs.reserve(method ? 3 : 2);
	if (method) {
		append(method->locale());
	}
	append(QLocale(Platform::SystemLanguage()));
	append(QLocale(Lang::LanguageIdOrDefault(Lang::Id())));

	return langs;
}

void Start(not_null<Main::Session*> session) {
	Spellchecker::SetPhrases({ {
		{ &ph::lng_spellchecker_submenu, tr::lng_spellchecker_submenu() },
		{ &ph::lng_spellchecker_add, tr::lng_spellchecker_add() },
		{ &ph::lng_spellchecker_remove, tr::lng_spellchecker_remove() },
		{ &ph::lng_spellchecker_ignore, tr::lng_spellchecker_ignore() },
	} });
	const auto settings = &Core::App().settings();
	auto &lifetime = session->lifetime();

	const auto onEnabled = [=](auto enabled) {
		Platform::Spellchecker::UpdateLanguages(
			enabled
				? settings->dictionariesEnabled()
				: std::vector<int>());
	};

	const auto guard = gsl::finally([=] {
		onEnabled(settings->spellcheckerEnabled());
	});

	if (Platform::Spellchecker::IsSystemSpellchecker()) {
		Spellchecker::SupportedScriptsChanged()
		| rpl::take(1)
		| rpl::start_with_next(AddExceptions, lifetime);

		return;
	}

	Spellchecker::SupportedScriptsChanged(
	) | rpl::start_with_next(AddExceptions, lifetime);

	Spellchecker::SetWorkingDirPath(DictionariesPath());

	settings->dictionariesEnabledChanges(
	) | rpl::start_with_next([](auto dictionaries) {
		Platform::Spellchecker::UpdateLanguages(dictionaries);
	}, lifetime);

	settings->spellcheckerEnabledChanges(
	) | rpl::start_with_next(onEnabled, lifetime);

	const auto method = QGuiApplication::inputMethod();

	const auto connectInput = [=] {
		if (!method || !settings->spellcheckerEnabled()) {
			return;
		}
		auto callback = [=] {
			if (BackgroundLoader) {
				return;
			}
			const auto l = LanguageFromLocale(method->locale());
			if (!IsSupportedLang(l) || DictionaryExists(l)) {
				return;
			}
			crl::on_main(session, [=] {
				DownloadDictionaryInBackground(session, 0, { l });
			});
		};
		QObject::connect(
			method,
			&QInputMethod::localeChanged,
			std::move(callback));
	};

	if (settings->autoDownloadDictionaries()) {
		session->data().contactsLoaded().changes(
		) | rpl::start_with_next([=](bool loaded) {
			if (!loaded) {
				return;
			}

			DownloadDictionaryInBackground(session, 0, DefaultLanguages());
		}, lifetime);

		connectInput();
	}

	const auto disconnect = [=] {
		QObject::disconnect(
			method,
			&QInputMethod::localeChanged,
			nullptr,
			nullptr);
	};
	lifetime.add([=] {
		disconnect();
		for (auto &[index, account] : session->domain().accounts()) {
			if (const auto anotherSession = account->maybeSession()) {
				if (anotherSession->uniqueId() != session->uniqueId()) {
					Spellchecker::Start(anotherSession);
					return;
				}
			}
		}
	});

	rpl::combine(
		settings->spellcheckerEnabledValue(),
		settings->autoDownloadDictionariesValue()
	) | rpl::start_with_next([=](bool spell, bool download) {
		if (spell && download) {
			connectInput();
			return;
		}
		disconnect();
	}, lifetime);

}

} // namespace Spellchecker

#endif // !TDESKTOP_DISABLE_SPELLCHECK
