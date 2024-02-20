/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "base/timer.h"

class History;

namespace Dialogs {
class MainList;
class Key;
} // namespace Dialogs

namespace Ui {
struct MoreChatsBarContent;
} // namespace Ui

namespace Data {

class Session;

class ChatFilter final {
public:
	enum class Flag : ushort {
		Contacts    = (1 << 0),
		NonContacts = (1 << 1),
		Groups      = (1 << 2),
		Channels    = (1 << 3),
		Bots        = (1 << 4),
		NoMuted     = (1 << 5),
		NoRead      = (1 << 6),
		NoArchived  = (1 << 7),
		RulesMask   = ((1 << 8) - 1),

		Chatlist    = (1 << 8),
		HasMyLinks  = (1 << 9),

		NewChats      = (1 << 10), // Telegram Business exceptions.
		ExistingChats = (1 << 11),
	};
	friend constexpr inline bool is_flag_type(Flag) { return true; };
	using Flags = base::flags<Flag>;

	ChatFilter() = default;
	ChatFilter(
		FilterId id,
		const QString &title,
		const QString &iconEmoji,
		Flags flags,
		base::flat_set<not_null<History*>> always,
		std::vector<not_null<History*>> pinned,
		base::flat_set<not_null<History*>> never);

	[[nodiscard]] ChatFilter withId(FilterId id) const;
	[[nodiscard]] ChatFilter withTitle(const QString &title) const;
	[[nodiscard]] ChatFilter withChatlist(
		bool chatlist,
		bool hasMyLinks) const;

	[[nodiscard]] static ChatFilter FromTL(
		const MTPDialogFilter &data,
		not_null<Session*> owner);
	[[nodiscard]] MTPDialogFilter tl(FilterId replaceId = 0) const;

	[[nodiscard]] FilterId id() const;
	[[nodiscard]] QString title() const;
	[[nodiscard]] QString iconEmoji() const;
	[[nodiscard]] Flags flags() const;
	[[nodiscard]] bool chatlist() const;
	[[nodiscard]] bool hasMyLinks() const;
	[[nodiscard]] const base::flat_set<not_null<History*>> &always() const;
	[[nodiscard]] const std::vector<not_null<History*>> &pinned() const;
	[[nodiscard]] const base::flat_set<not_null<History*>> &never() const;

	[[nodiscard]] bool contains(not_null<History*> history) const;

private:
	FilterId _id = 0;
	QString _title;
	QString _iconEmoji;
	base::flat_set<not_null<History*>> _always;
	std::vector<not_null<History*>> _pinned;
	base::flat_set<not_null<History*>> _never;
	Flags _flags;

};

inline bool operator==(const ChatFilter &a, const ChatFilter &b) {
	return (a.title() == b.title())
		&& (a.iconEmoji() == b.iconEmoji())
		&& (a.flags() == b.flags())
		&& (a.always() == b.always())
		&& (a.never() == b.never());
}

inline bool operator!=(const ChatFilter &a, const ChatFilter &b) {
	return !(a == b);
}

struct ChatFilterLink {
	FilterId id = 0;
	QString url;
	QString title;
	std::vector<not_null<History*>> chats;

	friend inline bool operator==(
		const ChatFilterLink &a,
		const ChatFilterLink &b) = default;
};

struct SuggestedFilter {
	ChatFilter filter;
	QString description;
};

class ChatFilters final {
public:
	explicit ChatFilters(not_null<Session*> owner);
	~ChatFilters();

	void setPreloaded(const QVector<MTPDialogFilter> &result);

	void load();
	void reload();
	void apply(const MTPUpdate &update);
	void set(ChatFilter filter);
	void remove(FilterId id);
	void moveAllToFront();
	[[nodiscard]] const std::vector<ChatFilter> &list() const;
	[[nodiscard]] rpl::producer<> changed() const;
	[[nodiscard]] rpl::producer<FilterId> isChatlistChanged() const;
	[[nodiscard]] bool loaded() const;
	[[nodiscard]] bool has() const;

	[[nodiscard]] FilterId defaultId() const;
	[[nodiscard]] FilterId lookupId(int index) const;

	bool loadNextExceptions(bool chatsListLoaded);

	void refreshHistory(not_null<History*> history);

	[[nodiscard]] not_null<Dialogs::MainList*> chatsList(FilterId filterId);
	void clear();

	const ChatFilter &applyUpdatedPinned(
		FilterId id,
		const std::vector<Dialogs::Key> &dialogs);
	void saveOrder(
		const std::vector<FilterId> &order,
		mtpRequestId after = 0);

	[[nodiscard]] bool archiveNeeded() const;

	void requestSuggested();
	[[nodiscard]] bool suggestedLoaded() const;
	[[nodiscard]] auto suggestedFilters() const
		-> const std::vector<SuggestedFilter> &;
	[[nodiscard]] rpl::producer<> suggestedUpdated() const;

	ChatFilterLink add(
		FilterId id,
		const MTPExportedChatlistInvite &update);
	void edit(
		FilterId id,
		const QString &url,
		const QString &title);
	void destroy(FilterId id, const QString &url);
	rpl::producer<std::vector<ChatFilterLink>> chatlistLinks(
		FilterId id) const;
	void reloadChatlistLinks(FilterId id);

	[[nodiscard]] rpl::producer<Ui::MoreChatsBarContent> moreChatsContent(
		FilterId id);
	[[nodiscard]] const std::vector<not_null<PeerData*>> &moreChats(
		FilterId id) const;
	void moreChatsHide(FilterId id, bool localOnly = false);

private:
	struct MoreChatsData {
		std::vector<not_null<PeerData*>> missing;
		crl::time lastUpdate = 0;
		mtpRequestId requestId = 0;
		std::weak_ptr<bool> watching;
	};

	void load(bool force);
	void received(const QVector<MTPDialogFilter> &list);
	bool applyOrder(const QVector<MTPint> &order);
	bool applyChange(ChatFilter &filter, ChatFilter &&updated);
	void applyInsert(ChatFilter filter, int position);
	void applyRemove(int position);

	void checkLoadMoreChatsLists();
	void loadMoreChatsList(FilterId id);

	const not_null<Session*> _owner;

	std::vector<ChatFilter> _list;
	base::flat_map<FilterId, std::unique_ptr<Dialogs::MainList>> _chatsLists;
	rpl::event_stream<> _listChanged;
	rpl::event_stream<FilterId> _isChatlistChanged;
	mtpRequestId _loadRequestId = 0;
	mtpRequestId _saveOrderRequestId = 0;
	mtpRequestId _saveOrderAfterId = 0;
	bool _loaded = false;
	bool _reloading = false;

	mtpRequestId _suggestedRequestId = 0;
	std::vector<SuggestedFilter> _suggested;
	rpl::event_stream<> _suggestedUpdated;
	crl::time _suggestedLastReceived = 0;

	std::deque<FilterId> _exceptionsToLoad;
	mtpRequestId _exceptionsLoadRequestId = 0;

	base::flat_map<FilterId, std::vector<ChatFilterLink>> _chatlistLinks;
	rpl::event_stream<FilterId> _chatlistLinksUpdated;
	mtpRequestId _linksRequestId = 0;

	base::flat_map<FilterId, MoreChatsData> _moreChatsData;
	rpl::event_stream<FilterId> _moreChatsUpdated;
	base::Timer _moreChatsTimer;

};

} // namespace Data
