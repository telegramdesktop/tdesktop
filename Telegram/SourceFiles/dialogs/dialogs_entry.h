/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"
#include "base/weak_ptr.h"
#include "base/flags.h"
#include "dialogs/dialogs_key.h"
#include "ui/unread_badge.h"

class HistoryItem;
class UserData;

namespace Main {
class Session;
} // namespace Main

namespace Data {
class Session;
class Forum;
class Folder;
class ForumTopic;
class CloudImageView;
} // namespace Data

namespace Ui {
} // namespace Ui

namespace Dialogs::Ui {
using namespace ::Ui;
struct PaintContext;
} // namespace Dialogs::Ui

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
	int height = 0;
};

struct UnreadState {
	int messages = 0;
	int messagesMuted = 0;
	int chats = 0;
	int chatsMuted = 0;
	int marks = 0;
	int marksMuted = 0;
	int reactions = 0;
	int reactionsMuted = 0;
	int mentions = 0;
	bool known = false;

	UnreadState &operator+=(const UnreadState &other) {
		messages += other.messages;
		messagesMuted += other.messagesMuted;
		chats += other.chats;
		chatsMuted += other.chatsMuted;
		marks += other.marks;
		marksMuted += other.marksMuted;
		reactions += other.reactions;
		reactionsMuted += other.reactionsMuted;
		mentions += other.mentions;
		return *this;
	}
	UnreadState &operator-=(const UnreadState &other) {
		messages -= other.messages;
		messagesMuted -= other.messagesMuted;
		chats -= other.chats;
		chatsMuted -= other.chatsMuted;
		marks -= other.marks;
		marksMuted -= other.marksMuted;
		reactions -= other.reactions;
		reactionsMuted -= other.reactionsMuted;
		mentions -= other.mentions;
		return *this;
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

struct BadgesState {
	int unreadCounter = 0;
	bool unread : 1 = false;
	bool unreadMuted : 1 = false;
	bool mention : 1 = false;
	bool mentionMuted : 1 = false;
	bool reaction : 1 = false;
	bool reactionMuted : 1 = false;

	friend inline constexpr auto operator<=>(
		BadgesState,
		BadgesState) = default;

	[[nodiscard]] bool empty() const {
		return !unread && !mention && !reaction;
	}
};

enum class CountInBadge : uchar {
	Default,
	Chats,
	Messages,
};

enum class IncludeInBadge : uchar {
	Default,
	Unmuted,
	All,
	UnmutedOrAll,
};

[[nodiscard]] BadgesState BadgesForUnread(
	const UnreadState &state,
	CountInBadge count = CountInBadge::Default,
	IncludeInBadge include = IncludeInBadge::Default);

class Entry : public base::has_weak_ptr {
public:
	enum class Type : uchar {
		History,
		Folder,
		ForumTopic,
	};
	Entry(not_null<Data::Session*> owner, Type type);
	virtual ~Entry();

	[[nodiscard]] Data::Session &owner() const;
	[[nodiscard]] Main::Session &session() const;

	History *asHistory();
	Data::Forum *asForum();
	Data::Folder *asFolder();
	Data::Thread *asThread();
	Data::ForumTopic *asTopic();

	const History *asHistory() const;
	const Data::Forum *asForum() const;
	const Data::Folder *asFolder() const;
	const Data::Thread *asThread() const;
	const Data::ForumTopic *asTopic() const;

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
	void updateChatListEntryPostponed();
	[[nodiscard]] bool isPinnedDialog(FilterId filterId) const {
		return lookupPinnedIndex(filterId) != 0;
	}
	void cachePinnedIndex(FilterId filterId, int index);
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
	virtual UnreadState chatListUnreadState() const = 0;
	virtual BadgesState chatListBadgesState() const = 0;
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
		const Ui::PaintContext &context) const = 0;

	[[nodiscard]] TimeId chatListTimeId() const {
		return _timeId;
	}

	[[nodiscard]] const Ui::Text::String &chatListNameText() const;
	[[nodiscard]] Ui::PeerBadge &chatListPeerBadge() const {
		return _chatListPeerBadge;
	}

protected:
	void notifyUnreadStateChange(const UnreadState &wasState);
	inline auto unreadStateChangeNotifier(bool required);

	[[nodiscard]] int lookupPinnedIndex(FilterId filterId) const;

private:
	enum class Flag : uchar {
		IsThread = (1 << 0),
		IsHistory = (1 << 1),
		UpdatePostponed = (1 << 2),
		InUnreadChangeBlock = (1 << 3),
	};
	friend inline constexpr bool is_flag_type(Flag) { return true; }
	using Flags = base::flags<Flag>;

	virtual void changedChatListPinHook();
	void pinnedIndexChanged(FilterId filterId, int was, int now);
	[[nodiscard]] uint64 computeSortPosition(FilterId filterId) const;

	[[nodiscard]] virtual int chatListNameVersion() const = 0;

	void setChatListExistence(bool exists);
	not_null<Row*> mainChatListLink(FilterId filterId) const;
	Row *maybeMainChatListLink(FilterId filterId) const;

	const not_null<Data::Session*> _owner;
	base::flat_map<FilterId, RowsByLetter> _chatListLinks;
	uint64 _sortKeyInChatList = 0;
	uint64 _sortKeyByDate = 0;
	base::flat_map<FilterId, int> _pinnedIndex;
	mutable Ui::PeerBadge _chatListPeerBadge;
	mutable Ui::Text::String _chatListNameText;
	mutable int _chatListNameVersion = 0;
	TimeId _timeId = 0;
	Flags _flags;

};

auto Entry::unreadStateChangeNotifier(bool required) {
	Expects(!(_flags & Flag::InUnreadChangeBlock));

	_flags |= Flag::InUnreadChangeBlock;
	const auto notify = required && inChatList();
	const auto wasState = notify ? chatListUnreadState() : UnreadState();
	return gsl::finally([=] {
		_flags &= ~Flag::InUnreadChangeBlock;
		if (notify) {
			Assert(inChatList());
			notifyUnreadStateChange(wasState);
		}
	});
}

} // namespace Dialogs
