/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class ApiWrap;

namespace ChatHelpers {
namespace details {

class EmojiKeywordsLangPackDelegate {
public:
	virtual ApiWrap *api() = 0;
	virtual void langPackRefreshed() = 0;

protected:
	~EmojiKeywordsLangPackDelegate() = default;

};

} // namespace details

class EmojiKeywords final : private details::EmojiKeywordsLangPackDelegate {
public:
	EmojiKeywords();
	EmojiKeywords(const EmojiKeywords &other) = delete;
	EmojiKeywords &operator=(const EmojiKeywords &other) = delete;
	~EmojiKeywords();

	void refresh();

	[[nodiscard]] rpl::producer<> refreshed() const;

	struct Result {
		EmojiPtr emoji = nullptr;
		QString label;
		QString replacement;
	};
	[[nodiscard]] std::vector<Result> query(
		const QString &query,
		bool exact = false) const;
	[[nodiscard]] std::vector<Result> queryMine(
		const QString &query,
		bool exact = false) const;
	[[nodiscard]] int maxQueryLength() const;

private:
	class LangPack;

	not_null<details::EmojiKeywordsLangPackDelegate*> delegate();
	ApiWrap *api() override;
	void langPackRefreshed() override;

	[[nodiscard]] static std::vector<Result> PrioritizeRecent(
		std::vector<Result> list);
	[[nodiscard]] static std::vector<Result> ApplyVariants(
		std::vector<Result> list);

	void handleSessionChanges();
	void apiChanged(ApiWrap *api);
	void refreshInputLanguages();
	[[nodiscard]] std::vector<QString> languages();
	void refreshRemoteList();
	void setRemoteList(std::vector<QString> &&list);
	void refreshFromRemoteList();

	ApiWrap *_api = nullptr;
	std::vector<QString> _localList;
	std::vector<QString> _remoteList;
	mtpRequestId _langsRequestId = 0;
	base::flat_map<QString, std::unique_ptr<LangPack>> _data;
	std::deque<std::unique_ptr<LangPack>> _notUsedData;
	std::deque<QStringList> _inputLanguages;
	rpl::event_stream<> _refreshed;

	rpl::lifetime _suggestedChangeLifetime;

	rpl::lifetime _lifetime;
	base::has_weak_ptr _guard;

};

} // namespace ChatHelpers
