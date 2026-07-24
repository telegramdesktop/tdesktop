/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_main_list.h"
#include "ui/text/text.h"

class ChannelData;
class History;
class PeerData;

namespace Data {

struct CommunityLinkedPeer {
	not_null<PeerData*> peer;
	std::optional<bool> visible;
	bool canViewHistory = false;

	friend inline bool operator==(
		const CommunityLinkedPeer &,
		const CommunityLinkedPeer &) = default;
};

[[nodiscard]] bool IsCommunityChatViewable(const CommunityLinkedPeer &linked);
[[nodiscard]] ChannelId PeerLinkedCommunityId(not_null<PeerData*> peer);
[[nodiscard]] bool CommunityChatJoined(not_null<PeerData*> peer);
[[nodiscard]] bool CommunityChatJoined(not_null<const History*> history);

class CommunityInfo final {
public:
	explicit CommunityInfo(not_null<ChannelData*> channel);

	[[nodiscard]] not_null<ChannelData*> channel() const {
		return _channel;
	}

	void applyLinkedPeers(const QVector<MTPCommunityPeer> &list);
	[[nodiscard]] auto linkedPeers() const
	-> const std::vector<CommunityLinkedPeer> & {
		return _linkedPeers;
	}
	[[nodiscard]] rpl::producer<> linkedPeersValue() const;
	[[nodiscard]] rpl::producer<> refreshed() const;
	[[nodiscard]] bool isHidden(not_null<PeerData*> peer) const;

	[[nodiscard]] bool collapsedInDialogs() const;
	[[nodiscard]] bool collapsedInChatLists() const {
		return _collapsedInChatLists;
	}
	void collapsedChanged();
	void ensureRowInChatList();

	void registerOne(not_null<History*> history);
	void unregisterOne(not_null<History*> history);
	void refreshOneMembership(not_null<History*> history);
	[[nodiscard]] auto histories() const
	-> const base::flat_set<not_null<History*>> & {
		return _histories;
	}

	[[nodiscard]] not_null<Dialogs::MainList*> chatsList();

	[[nodiscard]] TimeId chatsListDate() const {
		return _chatsListDate;
	}
	void oneChatsListDateChanged(TimeId was, TimeId now);
	void oneListMessageChanged();
	void oneUnreadStateChanged();

	[[nodiscard]] auto lastHistories() const
	-> const std::vector<not_null<History*>> & {
		return _lastHistories;
	}
	void validateListEntryCache();
	[[nodiscard]] const Ui::Text::String &listEntryCache() const {
		return _listEntryCache;
	}

private:
	void memberAdded(not_null<History*> history);
	void recountChatsListDate();
	void reorderLastHistories();
	void updateRowSortPosition();
	void repaintRow();

	const not_null<ChannelData*> _channel;
	std::vector<CommunityLinkedPeer> _linkedPeers;
	base::flat_set<not_null<PeerData*>> _hiddenPeers;
	rpl::event_stream<> _linkedPeersChanges;
	rpl::event_stream<> _refreshed;

	// Member chats (CommunityChatJoined()) — joined channels and bot
	// chats with messages; the source of the grouped row's aggregated
	// badge / date / preview. Non-member linked chats whose History is
	// loaded are tracked separately in _otherHistories so they never
	// leak into those aggregates.
	base::flat_set<not_null<History*>> _histories;
	base::flat_set<not_null<History*>> _otherHistories;
	std::vector<not_null<History*>> _lastHistories;
	Ui::Text::String _listEntryCache;
	int _listEntryCacheVersion = 0;
	int _chatListViewVersion = 0;
	TimeId _chatsListDate = 0;
	Dialogs::MainList _chatsList;
	bool _collapsedInChatLists = false;

	rpl::lifetime _lifetime;

};

} // namespace Data
