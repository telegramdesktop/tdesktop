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
} // namespace Data

namespace Dialogs {

class Row;
class IndexedList;
using RowsByLetter = base::flat_map<QChar, not_null<Row*>>;

enum class SortMode {
	Date = 0x00,
	Name = 0x01,
	Add  = 0x02,
};

enum class Mode {
	All       = 0x00,
	Important = 0x01,
};

enum class EntryType : unsigned {
	None = 0x00,
	OneOnOne = 0x01,
	Bot = 0x02,
	Group = 0x04,
	Channel = 0x08,
	TypeMask = 0x0F,  // only the types, disregards favorite status
	All = 0x0F
};
using EntryTypes = base::flags<EntryType>;
inline constexpr bool is_flag_type(EntryType) { return true; }

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
	Entry(not_null<Data::Session*> owner, const Key &key);
	Entry(const Entry &other) = delete;
	Entry &operator=(const Entry &other) = delete;
	virtual ~Entry() = default;

	Data::Session &owner() const;
	Main::Session &session() const;

	PositionChange adjustByPosInChatList(Mode list);
	bool inChatList(Mode list = Mode::All) const {
		return !chatListLinks(list).empty();
	}
	int posInChatList(Mode list) const;
	not_null<Row*> addToChatList(Mode list);
	void setRowInCurrentTab(Row *row);
	void removeFromChatList(Mode list);
	void removeChatListEntryByLetter(Mode list, QChar letter);
	void addChatListEntryByLetter(
		Mode list,
		QChar letter,
		not_null<Row*> row);
	void updateChatListEntry() const;
	void updateChatListEntry(Row *row) const;
	bool isPinnedDialog() const {
		return _pinnedIndex > 0;
	}
	void cachePinnedIndex(int index);
	bool isProxyPromoted() const {
		return _isProxyPromoted;
	}
	void cacheProxyPromoted(bool promoted);
	uint64 sortKeyInChatList() const {
		return _sortKeyInChatList;
	}
	void updateChatListSortPosition();
	void setChatListTimeId(TimeId date);
	virtual void updateChatListExistence();
	bool needUpdateInChatList() const;
	virtual TimeId adjustedChatListTimeId() const;

	virtual int fixedOnTopIndex() const = 0;
	static constexpr auto kArchiveFixOnTopIndex = 1;
	static constexpr auto kProxyPromotionFixOnTopIndex = 2;

	virtual EntryTypes getEntryType() const { return EntryType::None; }
	virtual bool toImportant() const = 0;
	virtual bool shouldBeInChatList() const = 0;
	virtual int chatListUnreadCount() const = 0;
	virtual bool chatListUnreadMark() const = 0;
	virtual bool chatListMutedBadge() const = 0;
	virtual UnreadState chatListUnreadState() const = 0;
	virtual HistoryItem *chatListMessage() const = 0;
	virtual bool chatListMessageKnown() const = 0;
	virtual void requestChatListMessage() = 0;
	virtual const QString &chatListName() const = 0;
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
		int x,
		int y,
		int size) const = 0;
	void paintUserpicLeft(
			Painter &p,
			int x,
			int y,
			int w,
			int size) const {
		paintUserpic(p, rtl() ? (w - x - size) : x, y, size);
	}

	TimeId chatListTimeId() const {
		return _timeId;
	}

	mutable const HistoryItem *textCachedFor = nullptr; // cache
	mutable Ui::Text::String lastItemTextCache;

protected:
	auto unreadStateChangeNotifier(bool required) {
		const auto notify = required && inChatList();
		const auto wasState = notify ? chatListUnreadState() : UnreadState();
		return gsl::finally([=] {
			if (notify) {
				notifyUnreadStateChange(wasState);
			}
		});
	}

private:
	virtual void changedChatListPinHook();

	void notifyUnreadStateChange(const UnreadState &wasState);

	void setChatListExistence(bool exists);
	RowsByLetter &chatListLinks(Mode list);
	const RowsByLetter &chatListLinks(Mode list) const;
	Row *mainChatListLink(Mode list) const;
	Row *rowInCurrentTab() const;

	not_null<IndexedList*> myChatsList(Mode list) const;

	not_null<Data::Session*> _owner;
	Dialogs::Key _key;
	Row *_rowInCurrentTab = nullptr;
	RowsByLetter _chatListLinks[2];
	uint64 _sortKeyInChatList = 0;
	int _pinnedIndex = 0;
	bool _isProxyPromoted = false;
	TimeId _timeId = 0;

};

} // namespace Dialogs
