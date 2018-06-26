/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"

#include "dialogs/dialogs_key.h"

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

struct PositionChange {
	int movedFrom;
	int movedTo;
};

class Entry {
public:
	Entry(const Key &key);

	PositionChange adjustByPosInChatList(
		Mode list,
		not_null<IndexedList*> indexed);
	bool inChatList(Mode list) const {
		return !chatListLinks(list).empty();
	}
	int posInChatList(Mode list) const;
	not_null<Row*> addToChatList(Mode list, not_null<IndexedList*> indexed);
	void removeFromChatList(Mode list, not_null<IndexedList*> indexed);
	void removeChatListEntryByLetter(Mode list, QChar letter);
	void addChatListEntryByLetter(
		Mode list,
		QChar letter,
		not_null<Row*> row);
	void updateChatListEntry() const;
	bool isPinnedDialog() const {
		return _pinnedIndex > 0;
	}
	void cachePinnedIndex(int index);
	bool isProxyPromoted() const {
		return _isProxyPromoted;
	}
	virtual bool useProxyPromotion() const = 0;
	void cacheProxyPromoted(bool promoted);
	uint64 sortKeyInChatList() const {
		return _sortKeyInChatList;
	}
	void updateChatListSortPosition();
	void setChatsListDate(QDateTime date);
	virtual void updateChatListExistence();
	bool needUpdateInChatList() const;

	virtual bool toImportant() const = 0;
	virtual bool shouldBeInChatList() const = 0;
	virtual int chatListUnreadCount() const = 0;
	virtual bool chatListUnreadMark() const = 0;
	virtual bool chatListMutedBadge() const = 0;
	virtual HistoryItem *chatsListItem() const = 0;
	virtual const QString &chatsListName() const = 0;
	virtual const base::flat_set<QString> &chatsListNameWords() const = 0;
	virtual const base::flat_set<QChar> &chatsListFirstLetters() const = 0;

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

	QDateTime chatsListDate() const {
		return _lastMessageDate;
	}

	virtual ~Entry() = default;

	mutable const HistoryItem *textCachedFor = nullptr; // cache
	mutable Text lastItemTextCache;

private:
	virtual QDateTime adjustChatListDate() const;
	virtual void changedInChatListHook(Dialogs::Mode list, bool added);
	virtual void changedChatListPinHook();

	void setChatListExistence(bool exists);
	RowsByLetter &chatListLinks(Mode list);
	const RowsByLetter &chatListLinks(Mode list) const;
	Row *mainChatListLink(Mode list) const;

	Dialogs::Key _key;
	RowsByLetter _chatListLinks[2];
	uint64 _sortKeyInChatList = 0;
	int _pinnedIndex = 0;
	bool _isProxyPromoted = false;
	QDateTime _lastMessageDate;

};

} // namespace Dialogs
