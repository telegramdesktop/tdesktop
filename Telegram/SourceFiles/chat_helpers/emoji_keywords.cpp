/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/emoji_keywords.h"

#include "emoji_suggestions_helper.h"
#include "lang/lang_instance.h"
#include "lang/lang_cloud_manager.h"
#include "core/application.h"
#include "base/platform/base_platform_info.h"
#include "ui/emoji_config.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "apiwrap.h"

#include <QtGui/QGuiApplication>

namespace ChatHelpers {
namespace {

constexpr auto kRefreshEach = 60 * 60 * crl::time(1000); // 1 hour.
constexpr auto kKeepNotUsedLangPacksCount = 4;
constexpr auto kKeepNotUsedInputLanguagesCount = 4;

using namespace Ui::Emoji;

using Result = EmojiKeywords::Result;

struct LangPackEmoji {
	EmojiPtr emoji = nullptr;
	QString text;
};

struct LangPackData {
	int version = 0;
	int maxKeyLength = 0;
	std::map<QString, std::vector<LangPackEmoji>> emoji;
};

[[nodiscard]] bool MustAddPostfix(const QString &text) {
	if (text.size() != 1) {
		return false;
	}
	const auto code = text[0].unicode();
	return (code == 0x2122U) || (code == 0xA9U) || (code == 0xAEU);
}

[[nodiscard]] bool SkipExactKeyword(
		const QString &language,
		const QString &word) {
	if ((word.size() == 1) && !word[0].isLetter()) {
		return true;
	} else if (word == qstr("10")) {
		return true;
	} else if (language != qstr("en")) {
		return false;
	} else if ((word.size() == 1)
		&& (word[0] != '$')
		&& (word[0].unicode() != 8364)) { // Euro.
		return true;
	} else if ((word.size() == 2)
		&& (word != qstr("us"))
		&& (word != qstr("uk"))
		&& (word != qstr("hi"))
		&& (word != qstr("ok"))) {
		return true;
	}
	return false;
}

[[nodiscard]] EmojiPtr FindExact(const QString &text) {
	auto length = 0;
	const auto result = Find(text, &length);
	return (length < text.size()) ? nullptr : result;
}

void CreateCacheFilePath() {
	QDir().mkpath(internal::CacheFileFolder() + qstr("/keywords"));
}

[[nodiscard]] QString CacheFilePath(QString id) {
	static const auto BadSymbols = QRegularExpression("[^a-zA-Z0-9_\\.\\-]");
	id.replace(BadSymbols, QString());
	if (id.isEmpty()) {
		return QString();
	}
	return internal::CacheFileFolder() + qstr("/keywords/") + id;
}

[[nodiscard]] LangPackData ReadLocalCache(const QString &id) {
	auto file = QFile(CacheFilePath(id));
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	auto result = LangPackData();
	auto stream = QDataStream(&file);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = qint32();
	auto count = qint32();
	stream
		>> version
		>> count;
	if (version < 0 || count < 0 || stream.status() != QDataStream::Ok) {
		return {};
	}
	for (auto i = 0; i != count; ++i) {
		auto key = QString();
		auto size = qint32();
		stream
			>> key
			>> size;
		if (size < 0 || stream.status() != QDataStream::Ok) {
			return {};
		}
		auto &list = result.emoji[key];
		for (auto j = 0; j != size; ++j) {
			auto text = QString();
			stream >> text;
			if (stream.status() != QDataStream::Ok) {
				return {};
			}
			const auto emoji = MustAddPostfix(text)
				? (text + QChar(Ui::Emoji::kPostfix))
				: text;
			const auto entry = LangPackEmoji{ FindExact(emoji), text };
			if (!entry.emoji) {
				return {};
			}
			list.push_back(entry);
		}
		result.maxKeyLength = std::max(result.maxKeyLength, key.size());
	}
	result.version = version;
	return result;
}

void WriteLocalCache(const QString &id, const LangPackData &data) {
	if (!data.version && data.emoji.empty()) {
		return;
	}
	CreateCacheFilePath();
	auto file = QFile(CacheFilePath(id));
	if (!file.open(QIODevice::WriteOnly)) {
		return;
	}
	auto stream = QDataStream(&file);
	stream.setVersion(QDataStream::Qt_5_1);
	stream
		<< qint32(data.version)
		<< qint32(data.emoji.size());
	for (const auto &[key, list] : data.emoji) {
		stream
			<< key
			<< qint32(list.size());
		for (const auto &emoji : list) {
			stream << emoji.text;
		}
	}
}

[[nodiscard]] QString NormalizeQuery(const QString &query) {
	return query.toLower();
}

[[nodiscard]] QString NormalizeKey(const QString &key) {
	return key.toLower().trimmed();
}

void AppendFoundEmoji(
		std::vector<Result> &result,
		const QString &label,
		const std::vector<LangPackEmoji> &list) {
	// It is important that the 'result' won't relocate while inserting.
	result.reserve(result.size() + list.size());
	const auto alreadyBegin = begin(result);
	const auto alreadyEnd = alreadyBegin + result.size();

	auto &&add = ranges::views::all(
		list
	) | ranges::views::filter([&](const LangPackEmoji &entry) {
		const auto i = ranges::find(
			alreadyBegin,
			alreadyEnd,
			entry.emoji,
			&Result::emoji);
		return (i == alreadyEnd);
	}) | ranges::views::transform([&](const LangPackEmoji &entry) {
		return Result{ entry.emoji, label, entry.text };
	});
	result.insert(end(result), add.begin(), add.end());
}

void AppendLegacySuggestions(
		std::vector<Result> &result,
		const QString &query) {
	const auto badSuggestionChar = [](QChar ch) {
		return (ch < 'a' || ch > 'z')
			&& (ch < 'A' || ch > 'Z')
			&& (ch < '0' || ch > '9')
			&& (ch != '_')
			&& (ch != '-')
			&& (ch != '+');
	};
	if (ranges::any_of(query, badSuggestionChar)) {
		return;
	}

	const auto suggestions = GetSuggestions(QStringToUTF16(query));

	// It is important that the 'result' won't relocate while inserting.
	result.reserve(result.size() + suggestions.size());
	const auto alreadyBegin = begin(result);
	const auto alreadyEnd = alreadyBegin + result.size();

	auto &&add = ranges::views::all(
		suggestions
	) | ranges::views::transform([](const Suggestion &suggestion) {
		return Result{
			Find(QStringFromUTF16(suggestion.emoji())),
			QStringFromUTF16(suggestion.label()),
			QStringFromUTF16(suggestion.replacement())
		};
	}) | ranges::views::filter([&](const Result &entry) {
		const auto i = entry.emoji
			? ranges::find(
				alreadyBegin,
				alreadyEnd,
				entry.emoji,
				&Result::emoji)
			: alreadyEnd;
		return (entry.emoji != nullptr)
			&& (i == alreadyEnd);
	});
	result.insert(end(result), add.begin(), add.end());
}

void ApplyDifference(
		LangPackData &data,
		const QVector<MTPEmojiKeyword> &keywords,
		int version) {
	data.version = version;
	for (const auto &keyword : keywords) {
		keyword.match([&](const MTPDemojiKeyword &keyword) {
			const auto word = NormalizeKey(qs(keyword.vkeyword()));
			if (word.isEmpty()) {
				return;
			}
			auto &list = data.emoji[word];
			auto &&emoji = ranges::views::all(
				keyword.vemoticons().v
			) | ranges::views::transform([](const MTPstring &string) {
				const auto text = qs(string);
				const auto emoji = MustAddPostfix(text)
					? (text + QChar(Ui::Emoji::kPostfix))
					: text;
				return LangPackEmoji{ FindExact(emoji), text };
			}) | ranges::views::filter([&](const LangPackEmoji &entry) {
				if (!entry.emoji) {
					LOG(("API Warning: emoji %1 is not supported, word: %2."
						).arg(
							entry.text,
							word));
				}
				return (entry.emoji != nullptr);
			});
			list.insert(end(list), emoji.begin(), emoji.end());
		}, [&](const MTPDemojiKeywordDeleted &keyword) {
			const auto word = NormalizeKey(qs(keyword.vkeyword()));
			if (word.isEmpty()) {
				return;
			}
			const auto i = data.emoji.find(word);
			if (i == end(data.emoji)) {
				return;
			}
			auto &list = i->second;
			for (const auto &emoji : keyword.vemoticons().v) {
				list.erase(
					ranges::remove(list, qs(emoji), &LangPackEmoji::text),
					end(list));
			}
			if (list.empty()) {
				data.emoji.erase(i);
			}
		});
	}
	if (data.emoji.empty()) {
		data.maxKeyLength = 0;
	} else {
		auto &&lengths = ranges::views::all(
			data.emoji
		) | ranges::views::transform([](auto &&pair) {
			return pair.first.size();
		});
		data.maxKeyLength = *ranges::max_element(lengths);
	}
}

} // namespace

class EmojiKeywords::LangPack final {
public:
	using Delegate = details::EmojiKeywordsLangPackDelegate;

	LangPack(not_null<Delegate*> delegate, const QString &id);
	LangPack(const LangPack &other) = delete;
	LangPack &operator=(const LangPack &other) = delete;
	~LangPack();

	[[nodiscard]] QString id() const;

	void refresh();
	void apiChanged();

	[[nodiscard]] std::vector<Result> query(
		const QString &normalized,
		bool exact) const;
	[[nodiscard]] int maxQueryLength() const;

private:
	enum class State {
		ReadingCache,
		PendingRequest,
		Requested,
		Refreshed,
	};

	void readLocalCache();
	void applyDifference(const MTPEmojiKeywordsDifference &result);
	void applyData(LangPackData &&data);

	not_null<Delegate*> _delegate;
	QString _id;
	State _state = State::ReadingCache;
	LangPackData _data;
	crl::time _lastRefreshTime = 0;
	mtpRequestId _requestId = 0;
	base::binary_guard _guard;

};

EmojiKeywords::LangPack::LangPack(
	not_null<Delegate*> delegate,
	const QString &id)
: _delegate(delegate)
, _id(id) {
	readLocalCache();
}

EmojiKeywords::LangPack::~LangPack() {
	if (_requestId) {
		if (const auto api = _delegate->api()) {
			api->request(_requestId).cancel();
		}
	}
}

void EmojiKeywords::LangPack::readLocalCache() {
	const auto id = _id;
	auto callback = crl::guard(_guard.make_guard(), [=](
			LangPackData &&result) {
		applyData(std::move(result));
		refresh();
	});
	crl::async([id, callback = std::move(callback)]() mutable {
		crl::on_main([
			callback = std::move(callback),
			result = ReadLocalCache(id)
		]() mutable {
			callback(std::move(result));
		});
	});
}

QString EmojiKeywords::LangPack::id() const {
	return _id;
}

void EmojiKeywords::LangPack::refresh() {
	if (_state != State::Refreshed) {
		return;
	} else if (_lastRefreshTime > 0
		&& crl::now() - _lastRefreshTime < kRefreshEach) {
		return;
	}
	const auto api = _delegate->api();
	if (!api) {
		_state = State::PendingRequest;
		return;
	}
	_state = State::Requested;
	const auto send = [&](auto &&request) {
		return api->request(
			std::move(request)
		).done([=](const MTPEmojiKeywordsDifference &result) {
			_requestId = 0;
			_lastRefreshTime = crl::now();
			applyDifference(result);
		}).fail([=](const MTP::Error &error) {
			_requestId = 0;
			_lastRefreshTime = crl::now();
		}).send();
	};
	_requestId = (_data.version > 0)
		? send(MTPmessages_GetEmojiKeywordsDifference(
			MTP_string(_id),
			MTP_int(_data.version)))
		: send(MTPmessages_GetEmojiKeywords(
			MTP_string(_id)));
}

void EmojiKeywords::LangPack::applyDifference(
		const MTPEmojiKeywordsDifference &result) {
	result.match([&](const MTPDemojiKeywordsDifference &data) {
		const auto code = qs(data.vlang_code());
		const auto version = data.vversion().v;
		const auto &keywords = data.vkeywords().v;
		if (code != _id) {
			LOG(("API Error: Bad lang_code for emoji keywords %1 -> %2").arg(
				_id,
				code));
			_data.version = 0;
			_state = State::Refreshed;
			return;
		} else if (keywords.isEmpty() && _data.version >= version) {
			_state = State::Refreshed;
			return;
		}
		const auto id = _id;
		auto copy = _data;
		auto callback = crl::guard(_guard.make_guard(), [=](
				LangPackData &&result) {
			applyData(std::move(result));
		});
		crl::async([=,
			copy = std::move(copy),
			callback = std::move(callback)]() mutable {
			ApplyDifference(copy, keywords, version);
			WriteLocalCache(id, copy);
			crl::on_main([
				result = std::move(copy),
				callback = std::move(callback)
			]() mutable {
				callback(std::move(result));
			});
		});
	});
}

void EmojiKeywords::LangPack::applyData(LangPackData &&data) {
	_data = std::move(data);
	_state = State::Refreshed;
	_delegate->langPackRefreshed();
}

void EmojiKeywords::LangPack::apiChanged() {
	if (_state == State::Requested && !_delegate->api()) {
		_requestId = 0;
	} else if (_state != State::PendingRequest) {
		return;
	}
	_state = State::Refreshed;
	refresh();
}

std::vector<Result> EmojiKeywords::LangPack::query(
		const QString &normalized,
		bool exact) const {
	if (normalized.size() > _data.maxKeyLength
		|| _data.emoji.empty()
		|| (exact && SkipExactKeyword(_id, normalized))) {
		return {};
	}

	const auto from = _data.emoji.lower_bound(normalized);
	auto &&chosen = ranges::make_subrange(
		from,
		end(_data.emoji)
	) | ranges::views::take_while([&](const auto &pair) {
		const auto &key = pair.first;
		return exact ? (key == normalized) : key.startsWith(normalized);
	});

	auto result = std::vector<Result>();
	for (const auto &[key, list] : chosen) {
		AppendFoundEmoji(result, key, list);
	}
	return result;
}

int EmojiKeywords::LangPack::maxQueryLength() const {
	return _data.maxKeyLength;
}

EmojiKeywords::EmojiKeywords() {
	crl::on_main(&_guard, [=] {
		handleSessionChanges();
	});
}

EmojiKeywords::~EmojiKeywords() = default;

not_null<details::EmojiKeywordsLangPackDelegate*> EmojiKeywords::delegate() {
	return static_cast<details::EmojiKeywordsLangPackDelegate*>(this);
}

ApiWrap *EmojiKeywords::api() {
	return _api;
}

void EmojiKeywords::langPackRefreshed() {
	_refreshed.fire({});
}

void EmojiKeywords::handleSessionChanges() {
	Core::App().domain().activeSessionValue( // #TODO multi someSessionValue
	) | rpl::map([](Main::Session *session) {
		return session ? &session->api() : nullptr;
	}) | rpl::start_with_next([=](ApiWrap *api) {
		apiChanged(api);
	}, _lifetime);
}

void EmojiKeywords::apiChanged(ApiWrap *api) {
	_api = api;
	if (_api) {
		crl::on_main(&_api->session(), crl::guard(&_guard, [=] {
			base::ObservableViewer(
				Lang::CurrentCloudManager().firstLanguageSuggestion()
			) | rpl::filter([=] {
				// Refresh with the suggested language if we already were asked.
				return !_data.empty();
			}) | rpl::start_with_next([=] {
				refresh();
			}, _suggestedChangeLifetime);
		}));
	} else {
		_langsRequestId = 0;
		_suggestedChangeLifetime.destroy();
	}
	for (const auto &[language, item] : _data) {
		item->apiChanged();
	}
}

void EmojiKeywords::refresh() {
	auto list = languages();
	if (_localList != list) {
		_localList = std::move(list);
		refreshRemoteList();
	} else {
		refreshFromRemoteList();
	}
}

std::vector<QString> EmojiKeywords::languages() {
	if (!_api) {
		return {};
	}
	refreshInputLanguages();

	auto result = std::vector<QString>();
	const auto yield = [&](const QString &language) {
		result.push_back(language);
	};
	const auto yieldList = [&](const QStringList &list) {
		result.insert(end(result), list.begin(), list.end());
	};
	yield(Lang::Id());
	yield(Lang::DefaultLanguageId());
	yield(Lang::CurrentCloudManager().suggestedLanguage());
	yield(Platform::SystemLanguage());
	yieldList(QLocale::system().uiLanguages());
	for (const auto &list : _inputLanguages) {
		yieldList(list);
	}
	ranges::sort(result);
	return result;
}

void EmojiKeywords::refreshInputLanguages() {
	const auto method = QGuiApplication::inputMethod();
	if (!method) {
		return;
	}
	const auto list = method->locale().uiLanguages();
	const auto i = ranges::find(_inputLanguages, list);
	if (i != end(_inputLanguages)) {
		std::rotate(i, i + 1, end(_inputLanguages));
	} else {
		if (_inputLanguages.size() >= kKeepNotUsedInputLanguagesCount) {
			_inputLanguages.pop_front();
		}
		_inputLanguages.push_back(list);
	}
}

rpl::producer<> EmojiKeywords::refreshed() const {
	return _refreshed.events();
}

std::vector<Result> EmojiKeywords::query(
		const QString &query,
		bool exact) const {
	const auto normalized = NormalizeQuery(query);
	if (normalized.isEmpty()) {
		return {};
	}
	auto result = std::vector<Result>();
	for (const auto &[language, item] : _data) {
		const auto list = item->query(normalized, exact);

		// It is important that the 'result' won't relocate while inserting.
		result.reserve(result.size() + list.size());
		const auto alreadyBegin = begin(result);
		const auto alreadyEnd = alreadyBegin + result.size();

		auto &&add = ranges::views::all(
			list
		) | ranges::views::filter([&](Result entry) {
			// In each item->query() result the list has no duplicates.
			// So we need to check only for duplicates between queries.
			const auto i = ranges::find(
				alreadyBegin,
				alreadyEnd,
				entry.emoji,
				&Result::emoji);
			return (i == alreadyEnd);
		});
		result.insert(end(result), add.begin(), add.end());
	}
	if (!exact) {
		AppendLegacySuggestions(result, query);
	}
	return result;
}

int EmojiKeywords::maxQueryLength() const {
	if (_data.empty()) {
		return 0;
	}
	auto &&lengths = _data | ranges::views::transform([](const auto &pair) {
		return pair.second->maxQueryLength();
	});
	return *ranges::max_element(lengths);
}

void EmojiKeywords::refreshRemoteList() {
	if (!_api) {
		_localList.clear();
		setRemoteList({});
		return;
	}
	_api->request(base::take(_langsRequestId)).cancel();
	auto languages = QVector<MTPstring>();
	for (const auto &id : _localList) {
		languages.push_back(MTP_string(id));
	}
	_langsRequestId = _api->request(MTPmessages_GetEmojiKeywordsLanguages(
		MTP_vector<MTPstring>(languages)
	)).done([=](const MTPVector<MTPEmojiLanguage> &result) {
		setRemoteList(ranges::views::all(
			result.v
		) | ranges::views::transform([](const MTPEmojiLanguage &language) {
			return language.match([&](const MTPDemojiLanguage &language) {
				return qs(language.vlang_code());
			});
		}) | ranges::to_vector);
		_langsRequestId = 0;
	}).fail([=](const MTP::Error &error) {
		_langsRequestId = 0;
	}).send();
}

void EmojiKeywords::setRemoteList(std::vector<QString> &&list) {
	if (_remoteList == list) {
		return;
	}
	_remoteList = std::move(list);
	for (auto i = begin(_data); i != end(_data);) {
		if (ranges::find(_remoteList, i->first) != end(_remoteList)) {
			++i;
		} else {
			if (_notUsedData.size() >= kKeepNotUsedLangPacksCount) {
				_notUsedData.pop_front();
			}
			_notUsedData.push_back(std::move(i->second));
			i = _data.erase(i);
		}
	}
	refreshFromRemoteList();
}

void EmojiKeywords::refreshFromRemoteList() {
	for (const auto &id : _remoteList) {
		if (const auto i = _data.find(id); i != end(_data)) {
			i->second->refresh();
			continue;
		}
		const auto i = ranges::find(
			_notUsedData,
			id,
			[](const std::unique_ptr<LangPack> &p) { return p->id(); });
		if (i != end(_notUsedData)) {
			_data.emplace(id, std::move(*i));
			_notUsedData.erase(i);
		} else {
			_data.emplace(
				id,
				std::make_unique<LangPack>(delegate(), id));
		}
	}
}

} // namespace ChatHelpers
