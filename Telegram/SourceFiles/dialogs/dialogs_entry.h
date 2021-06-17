/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"

#include "dialogs/dialogs_key.h"

namespace Main {
class Session;
} // namespace Main

namespace Data {
class Session;
class Folder;
class CloudImageView;
} // namespace Data

namespace Dialogs {

class Row;
class IndexedList;
class MainList;

struct RowsByLetter {
	not_null<Row*> main;
	base::flat_map<QChar, not_null<Row*>> letters;
};

enum class SortMode {
	Date    = 0x00,
	Name    = 0x01,
	Add     = 0x02,
};

struct PositionChange {
	int from = -1;
	int to = -1;
};

struct UnreadState {
	int messages = 0;
	int messagesMuted = 0;
	int chats = 0;
	int chatsMuted = 0;
	int marks = 0;
	int marksMuted = 0;
	bool known = false;

	UnreadState &operator+=(const UnreadState &other) {
		messages += other.messages;
		messagesMuted += other.messagesMuted;
		chats += other.chats;
		chatsMuted += other.chatsMuted;
		marks += other.marks;
		marksMuted += other.marksMuted;
		return *this;
	}
	UnreadState &operator-=(const UnreadState &other) {
		messages -= other.messages;
		messagesMuted -= other.messagesMuted;
		chats -= other.chats;
		chatsMuted -= other.chatsMuted;
		marks -= other.marks;
		marksMuted -= other.marksMuted;
		return *this;
	}

	bool empty() const {
		return !messages && !chats && !marks;
	}
};

inline UnreadState operator+(const UnreadState &a, const UnreadState &b) {
	auto result = a;
	result += b;
	return result;
}

inline UnreadState operator-(const UnreadState &a, const UnreadState &b) {
	auto result = a;
	result -= b;
	return result;
}

class Entry {
public:
	enum class Type {
		History,
		Folder,
	};
	Entry(not_null<Data::Session*> owner, Type type);
	Entry(const Entry &other) = delete;
	Entry &operator=(const Entry &other) = delete;
	virtual ~Entry() = default;

	[[nodiscard]] Data::Session &owner() const;
	[[nodiscard]] Main::Session &session() const;

	History *asHistory();
	Data::Folder *asFolder();

	PositionChange adjustByPosInChatList(
		FilterId filterId,
		not_null<MainList*> list);
	[[nodiscard]] bool inChatList(FilterId filterId = 0) const {
		return _chatListLinks.contains(filterId);
	}
	RowsByLetter *chatListLinks(FilterId filterId);
	const RowsByLetter *chatListLinks(FilterId filterId) const;
	[[nodiscard]] int posInChatList(FilterId filterId) const;
	not_null<Row*> addToChatList(
		FilterId filterId,
		not_null<MainList*> list);
	void removeFromChatList(
		FilterId filterId,
		not_null<MainList*> list);
	void removeChatListEntryByLetter(FilterId filterId, QChar letter);
	void addChatListEntryByLetter(
		FilterId filterId,
		QChar letter,
		not_null<Row*> row);
	void updateChatListEntry();
	[[nodiscard]] bool isPinnedDialog(FilterId filterId) const {
		return lookupPinnedIndex(filterId) != 0;
	}
	void cachePinnedIndex(FilterId filterId, int index);
	[[nodiscard]] bool isTopPromoted() const;
	[[nodiscard]] uint64 sortKeyInChatList(FilterId filterId) const {
		return filterId
			? computeSortPosition(filterId)
			: _sortKeyInChatList;
	}
	void updateChatListSortPosition();
	void setChatListTimeId(TimeId date);
	virtual void updateChatListExistence();
	bool needUpdateInChatList() const;
	virtual TimeId adjustedChatListTimeId() const;

	virtual int fixedOnTopIndex() const = 0;
	static constexpr auto kArchiveFixOnTopIndex = 1;
	static constexpr auto kTopPromotionFixOnTopIndex = 2;

	virtual bool shouldBeInChatList() const = 0;
	virtual int chatListUnreadCount() const = 0;
	virtual bool chatListUnreadMark() const = 0;
	virtual bool chatListMutedBadge() const = 0;
	virtual UnreadState chatListUnreadState() const = 0;
	virtual HistoryItem *chatListMessage() const = 0;
	virtual bool chatListMessageKnown() const = 0;
	virtual void requestChatListMessage() = 0;
	virtual const QString &chatListName() const = 0;
	virtual const QString &chatListNameSortKey() const = 0;
	virtual const base::flat_set<QString> &chatListNameWords() const = 0;
	virtual const base::flat_set<QChar> &chatListFirstLetters() const = 0;

	virtual bool folderKnown() const {
		return true;
	}
	virtual Data::Folder *folder() const {
		return nullptr;
	}

	virtual void loadUserpic() = 0;
	virtual void paintUserpic(
		Painter &p,
		std::shared_ptr<Data::CloudImageView> &view,
		int x,
		int y,
		int size) const = 0;
	void paintUserpicLeft(
			Painter &p,
			std::shared_ptr<Data::CloudImageView> &view,
			int x,
			int y,
			int w,
			int size) const {
		paintUserpic(p, view, rtl() ? (w - x - size) : x, y, size);
	}

	TimeId chatListTimeId() const {
		return _timeId;
	}

	mutable const HistoryItem *textCachedFor = nullptr; // cache
	mutable Ui::Text::String lastItemTextCache;

protected:
	void notifyUnreadStateChange(const UnreadState &wasState);
	auto unreadStateChangeNotifier(bool required) {
		const auto notify = required && inChatList();
		const auto wasState = notify ? chatListUnreadState() : UnreadState();
		return gsl::finally([=] {
			if (notify) {
				notifyUnreadStateChange(wasState);
			}
		});
	}

	[[nodiscard]] int lookupPinnedIndex(FilterId filterId) const;

	void cacheTopPromoted(bool promoted);

private:
	virtual void changedChatListPinHook();
	void pinnedIndexChanged(int was, int now);
	[[nodiscard]] uint64 computeSortPosition(FilterId filterId) const;

	void setChatListExistence(bool exists);
	not_null<Row*> mainChatListLink(FilterId filterId) const;
	Row *maybeMainChatListLink(FilterId filterId) const;

	const not_null<Data::Session*> _owner;
	base::flat_map<FilterId, RowsByLetter> _chatListLinks;
	uint64 _sortKeyInChatList = 0;
	uint64 _sortKeyByDate = 0;
	base::flat_map<FilterId, int> _pinnedIndex;
	TimeId _timeId = 0;
	bool _isTopPromoted = false;
	const bool _isFolder = false;

};

} // namespace Dialogs
