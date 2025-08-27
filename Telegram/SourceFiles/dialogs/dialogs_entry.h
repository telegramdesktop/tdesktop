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
#include "dialogs/dialogs_common.h"
#include "ui/unread_badge.h"

class HistoryItem;
class History;
class UserData;

namespace Main {
class Session;
} // namespace Main

namespace Data {
class Session;
class Forum;
class Folder;
class ForumTopic;
class SavedSublist;
class SavedMessages;
class Thread;
} // namespace Data

namespace Ui {
struct PeerUserpicView;
} // namespace Ui

namespace Dialogs::Ui {
using namespace ::Ui;
struct PaintContext;
} // namespace Dialogs::Ui

namespace Dialogs {

struct UnreadState;
class Row;
class IndexedList;
class MainList;

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
		SavedSublist,
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
	Data::SavedSublist *asSublist();

	const History *asHistory() const;
	const Data::Forum *asForum() const;
	const Data::Folder *asFolder() const;
	const Data::Thread *asThread() const;
	const Data::ForumTopic *asTopic() const;
	const Data::SavedSublist *asSublist() const;

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
	void setColorIndexForFilterId(FilterId, std::optional<uint8>);
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
	void updateChatListEntryHeight();
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
	[[nodiscard]] virtual TimeId adjustedChatListTimeId() const;

	[[nodiscard]] virtual int fixedOnTopIndex() const = 0;
	static constexpr auto kArchiveFixOnTopIndex = 1;
	static constexpr auto kTopPromotionFixOnTopIndex = 2;

	[[nodiscard]] virtual bool shouldBeInChatList() const = 0;
	[[nodiscard]] virtual UnreadState chatListUnreadState() const = 0;
	[[nodiscard]] virtual BadgesState chatListBadgesState() const = 0;
	[[nodiscard]] virtual HistoryItem *chatListMessage() const = 0;
	[[nodiscard]] virtual bool chatListMessageKnown() const = 0;
	[[nodiscard]] virtual const QString &chatListName() const = 0;
	[[nodiscard]] virtual const QString &chatListNameSortKey() const = 0;
	[[nodiscard]] virtual int chatListNameVersion() const = 0;
	[[nodiscard]] virtual auto chatListNameWords() const
		-> const base::flat_set<QString> & = 0;
	[[nodiscard]] virtual auto chatListFirstLetters() const
		-> const base::flat_set<QChar> & = 0;

	[[nodiscard]] virtual bool folderKnown() const {
		return true;
	}
	[[nodiscard]] virtual Data::Folder *folder() const {
		return nullptr;
	}

	virtual void chatListPreloadData() = 0;
	virtual void paintUserpic(
		Painter &p,
		Ui::PeerUserpicView &view,
		const Ui::PaintContext &context) const = 0;

	[[nodiscard]] TimeId chatListTimeId() const {
		return _timeId;
	}

	[[nodiscard]] const Ui::Text::String &chatListNameText() const;
	[[nodiscard]] Ui::PeerBadge &chatListPeerBadge() const {
		return _chatListPeerBadge;
	}

	[[nodiscard]] bool hasChatsFilterTags(FilterId exclude) const;
protected:
	void notifyUnreadStateChange(const UnreadState &wasState);
	inline auto unreadStateChangeNotifier(bool required);

	[[nodiscard]] int lookupPinnedIndex(FilterId filterId) const;

private:
	enum class Flag : uchar {
		IsThread = (1 << 0),
		IsHistory = (1 << 1),
		IsForumTopic = (1 << 2),
		IsSavedSublist = (1 << 3),
		UpdatePostponed = (1 << 4),
		InUnreadChangeBlock = (1 << 5),
	};
	friend inline constexpr bool is_flag_type(Flag) { return true; }
	using Flags = base::flags<Flag>;

	virtual void changedChatListPinHook();
	void pinnedIndexChanged(FilterId filterId, int was, int now);
	[[nodiscard]] uint64 computeSortPosition(FilterId filterId) const;

	void setChatListExistence(bool exists);
	not_null<Row*> mainChatListLink(FilterId filterId) const;
	Row *maybeMainChatListLink(FilterId filterId) const;

	const not_null<Data::Session*> _owner;
	base::flat_map<FilterId, RowsByLetter> _chatListLinks;
	uint64 _sortKeyInChatList = 0;
	uint64 _sortKeyByDate = 0;
	base::flat_map<FilterId, int> _pinnedIndex;
	base::flat_map<FilterId, uint8> _tagColors;
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
	return gsl::finally([=, this] {
		_flags &= ~Flag::InUnreadChangeBlock;
		if (notify) {
			Assert(inChatList());
			notifyUnreadStateChange(wasState);
		}
	});
}

} // namespace Dialogs
