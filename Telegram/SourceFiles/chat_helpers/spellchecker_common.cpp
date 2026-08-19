/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/spellchecker_common.h"

#ifndef TDESKTOP_DISABLE_SPELLCHECK

#include "base/platform/base_platform_info.h"
#include "base/weak_ptr.h"
#include "base/zlib_help.h"
#include "data/data_session.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "mtproto/dedicated_file_loader.h"
#include "spellcheck/platform/platform_spellcheck.h"
#include "spellcheck/spellcheck_utils.h"
#include "spellcheck/spellcheck_value.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/version.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
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

constexpr auto kLangsForLWC = { QLocale::English, QLocale::Portuguese };
constexpr auto kDefaultCountries = { QLocale::UnitedStates, QLocale::Brazil };

// Language With Country.
inline auto LWC(QLocale::Language language, QLocale::Country country) {
	if (ranges::contains(kDefaultCountries, country)) {
		return int(language);
	}
	return (language * 1000) + country;
}

inline auto LanguageFromLocale(QLocale loc) {
	const auto locLang = loc.language();
	return (ranges::contains(kLangsForLWC, locLang)
		&& (loc.country() != QLocale::AnyCountry))
			? LWC(locLang, loc.country())
			: int(locLang);
}

constexpr auto kDictionariesManifestChannel = "tdhbcfiles"_cs;
constexpr auto kDictionariesManifestPostId = 2949;

// Runtime-loaded dictionaries manifest. Kept in memory only: fetched from
// the pinned JSON post the first time something actually needs it
// (Manage Dictionaries, auto-download). Nothing persists to disk, so an
// install whose enabled dictionaries are all on disk never hits the net.
std::vector<Dict> DictionariesList;
rpl::event_stream<> DictionariesListChanged;

void EnsurePath();

bool ParseLocation(const QString &text, QString &channel, int &postId) {
	const auto sep = text.indexOf('#');
	if (sep <= 0 || sep == text.size() - 1) {
		return false;
	}
	auto ok = false;
	const auto parsed = text.mid(sep + 1).toInt(&ok);
	if (!ok || parsed <= 0) {
		return false;
	}
	channel = text.left(sep);
	postId = parsed;
	return true;
}

std::vector<Dict> ParseManifest(const QByteArray &bytes) {
	auto result = std::vector<Dict>();
	auto err = QJsonParseError();
	const auto doc = QJsonDocument::fromJson(bytes, &err);
	if (err.error != QJsonParseError::NoError || !doc.isObject()) {
		LOG(("Spellcheck Error: manifest JSON parse failed: %1"
			).arg(err.errorString()));
		return result;
	}
	const auto list = doc.object().value(u"dictionaries"_q).toArray();
	result.reserve(list.size());
	for (const auto &v : list) {
		const auto obj = v.toObject();
		auto d = Dict();
		d.id = obj.value(u"id"_q).toInt();
		d.size = int64(obj.value(u"size"_q).toDouble());
		d.name = obj.value(u"name"_q).toString();
		const auto location = obj.value(u"location"_q).toString();
		if (!d.id
			|| d.name.isEmpty()
			|| !ParseLocation(location, d.channel, d.postId)) {
			continue;
		}
		result.push_back(std::move(d));
	}
	return result;
}

class DictManifestLoader final : public base::has_weak_ptr {
public:
	explicit DictManifestLoader(base::weak_ptr<Main::Session> session);

	void start();

private:
	void resolved(const MTPInputChannel &channel);
	void received(const MTPmessages_Messages &result);
	void apply(const QByteArray &bytes);
	void finish();

	MTP::WeakInstance _mtp;

};

std::shared_ptr<DictManifestLoader> ActiveManifestLoader;

DictManifestLoader::DictManifestLoader(base::weak_ptr<Main::Session> session)
: _mtp(session) {
}

void DictManifestLoader::start() {
	if (!_mtp.valid()) {
		finish();
		return;
	}
	const auto weak = base::make_weak(this);
	MTP::ResolveChannel(&_mtp, kDictionariesManifestChannel.utf16(), [=](
			const MTPInputChannel &channel) {
		if (const auto strong = weak.get()) {
			strong->resolved(channel);
		}
	}, [=] {
		if (const auto strong = weak.get()) {
			strong->finish();
		}
	});
}

void DictManifestLoader::resolved(const MTPInputChannel &channel) {
	const auto weak = base::make_weak(this);
	_mtp.send(
		MTPchannels_GetMessages(
			channel,
			MTP_vector<MTPInputMessage>(1,
				MTP_inputMessageID(
					MTP_int(kDictionariesManifestPostId)))),
		[=](const MTPmessages_Messages &result) {
			if (const auto strong = weak.get()) {
				strong->received(result);
			}
		},
		[=](const MTP::Error &) {
			if (const auto strong = weak.get()) {
				strong->finish();
			}
		});
}

void DictManifestLoader::received(const MTPmessages_Messages &result) {
	const auto message = MTP::GetMessagesElement(result);
	if (!message || message->type() != mtpc_message) {
		LOG(("Spellcheck Error: manifest message not found."));
		finish();
		return;
	}
	apply(message->c_message().vmessage().v);
	finish();
}

void DictManifestLoader::apply(const QByteArray &bytes) {
	auto parsed = ParseManifest(bytes);
	if (parsed.empty()) {
		LOG(("Spellcheck Error: manifest empty or unparseable."));
		return;
	}
	DictionariesList = std::move(parsed);
	DictionariesListChanged.fire({});
}

void DictManifestLoader::finish() {
	crl::on_main([] {
		ActiveManifestLoader.reset();
	});
}

void StartManifestRefresh(not_null<Main::Session*> session) {
	if (ActiveManifestLoader) {
		return;
	}
	ActiveManifestLoader = std::make_shared<DictManifestLoader>(
		base::make_weak(session));
	ActiveManifestLoader->start();
}

// Callers waiting for the first manifest arrival in this session.
// We don't cache manifest to disk, so each cold start begins empty;
// queued callbacks fire once when the MTP fetch lands.
std::vector<Fn<void()>> ManifestPending;
rpl::lifetime ManifestPendingSubscription;

void EnsureManifestThen(
		not_null<Main::Session*> session,
		Fn<void()> callback) {
	if (!DictionariesList.empty()) {
		callback();
		return;
	}
	const auto firstCaller = ManifestPending.empty();
	ManifestPending.push_back(std::move(callback));
	if (firstCaller) {
		DictionariesListChanged.events(
		) | rpl::take(1) | rpl::on_next([] {
			auto fns = std::move(ManifestPending);
			ManifestPending.clear();
			for (auto &fn : fns) {
				fn();
			}
		}, ManifestPendingSubscription);
	}
	StartManifestRefresh(session);
}

inline auto IsSupportedLang(int lang) {
	return ranges::contains(DictionariesList, lang, &Dict::id);
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
	if (counter >= langs.size()) {
		return;
	}
	const auto id = langs[counter];
	counter++;
	const auto destroyer = [=] {
		BackgroundLoader = nullptr;
		BackgroundLoaderChanged.fire(0);

		if (DictionaryExists(id)) {
			auto dicts = Core::App().settings().dictionariesEnabled();
			if (ranges::contains(dicts, id)) {
				Platform::Spellchecker::UpdateLanguages(dicts);
			} else {
				dicts.push_back(id);
				Core::App().settings().setDictionariesEnabled(
					std::move(dicts));
				Core::App().saveSettingsDelayed();
			}
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
			|| Spellchecker::IsWordSkippable(word));
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
	int64 size,
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
	return DictionariesList;
}

rpl::producer<> DictionariesChanged() {
	return DictionariesListChanged.events();
}

void RefreshDictionariesManifest(not_null<Main::Session*> session) {
	StartManifestRefresh(session);
}

int64 GetDownloadSize(int id) {
	const auto i = ranges::find(DictionariesList, id, &Dict::id);
	return (i == end(DictionariesList)) ? 0 : i->size;
}

MTP::DedicatedLoader::Location GetDownloadLocation(int id) {
	const auto i = ranges::find(DictionariesList, id, &Dict::id);
	if (i == end(DictionariesList)) {
		return MTP::DedicatedLoader::Location{};
	}
	return MTP::DedicatedLoader::Location{ i->channel, i->postId };
}

QString DictPathByLangId(int langId) {
	EnsurePath();
	return u"%1/%2"_q.arg(
		DictionariesPath(),
		Spellchecker::LocaleFromLangId(langId).name());
}

QString DictionariesPath() {
	return cWorkingDir() + u"tdata/dictionaries"_q;
}

bool UnpackDictionary(const QString &path, int langId) {
	const auto folder = DictPathByLangId(langId);
	if (!UnpackBlob(path, folder, IsGoodPartName)) {
		return false;
	}
	// The Serbian archive ships "sr_Cyrl_RS.{dic,aff}", but
	// QLocale(QLocale::Serbian).name() is "sr_RS" on our Qt, so the
	// Hunspell loader and DictionaryExists miss them. Rename to the
	// expected stem.
	if (langId == int(QLocale::Serbian)) {
		const auto dir = QDir(folder);
		for (const auto &ext : kDictExtensions) {
			const auto from = u"sr_Cyrl_RS.%1"_q.arg(ext);
			const auto to = u"sr_RS.%1"_q.arg(ext);
			if (dir.exists(from) && !dir.exists(to)) {
				QFile::rename(dir.filePath(from), dir.filePath(to));
			}
		}
	}
	return true;
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
	const auto folder = u"%1/%2/"_q.arg(
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
	const auto folder = u"%1/%2/"_q.arg(
		DictionariesPath(),
		fileName);
	QDir(folder).removeRecursively();

	const auto path = folder + fileName;
	QDir().mkpath(folder);
	auto input = QFile(u":/misc/en_US_dictionary"_q);
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
		const auto dicts = Core::App().settings().dictionariesEnabled();
		if (dicts.empty()) {
			return QString();
		}
		const auto filtered = ranges::views::all(
			dicts
		) | ranges::views::filter(
			DictionaryExists
		) | ranges::to_vector;

		return (filtered.size() < dicts.size())
			? tr::lng_contacts_loading(tr::now)
			: QString::number(filtered.size());
	};
	return rpl::single(
		computeString()
	) | rpl::then(
		rpl::merge(
			Spellchecker::SupportedScriptsChanged(),
			Spellchecker::DictionariesChanged(),
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
		| rpl::on_next(AddExceptions, lifetime);

		return;
	}

	Spellchecker::SupportedScriptsChanged(
	) | rpl::on_next(AddExceptions, lifetime);

	Spellchecker::SetWorkingDirPath(DictionariesPath());

	settings->dictionariesEnabledChanges(
	) | rpl::on_next([](auto dictionaries) {
		Platform::Spellchecker::UpdateLanguages(dictionaries);
	}, lifetime);

	settings->spellcheckerEnabledChanges(
	) | rpl::on_next(onEnabled, lifetime);

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
			// Avoid pulling the manifest just because an input-method
			// locale flipped; only fetch if we'd actually need to
			// download something for the new locale.
			if (DictionaryExists(l)) {
				return;
			}
			EnsureManifestThen(session, crl::guard(session, [=] {
				if (!IsSupportedLang(l) || DictionaryExists(l)) {
					return;
				}
				DownloadDictionaryInBackground(session, 0, { l });
			}));
		};
		QObject::connect(
			method,
			&QInputMethod::localeChanged,
			std::move(callback));
	};

	if (settings->autoDownloadDictionaries()) {
		session->data().contactsLoaded().changes(
		) | rpl::on_next([=](bool loaded) {
			if (!loaded) {
				return;
			}
			const auto enabled = settings->dictionariesEnabled();
			if (!enabled.empty()
				&& ranges::all_of(enabled, &DictionaryExists)) {
				// Every previously-enabled dictionary is already on
				// disk; no manifest fetch, no network traffic.
				return;
			}
			EnsureManifestThen(session, crl::guard(session, [=] {
				DownloadDictionaryInBackground(
					session, 0, DefaultLanguages());
			}));
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
	) | rpl::on_next([=](bool spell, bool download) {
		if (spell && download) {
			connectInput();
			return;
		}
		disconnect();
	}, lifetime);

}

} // namespace Spellchecker

#endif // !TDESKTOP_DISABLE_SPELLCHECK
