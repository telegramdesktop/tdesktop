/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_main_list.h"

namespace Dialogs {
struct UnreadState;
} // namespace Dialogs

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;
class SavedSublist;

struct SavedMessagesOffsets {
	TimeId date = 0;
	MsgId id = 0;
	PeerData *peer = nullptr;

	friend inline constexpr auto operator<=>(
		SavedMessagesOffsets,
		SavedMessagesOffsets) = default;
};

class SavedMessages final {
public:
	explicit SavedMessages(
		not_null<Session*> owner,
		ChannelData *parentChat = nullptr);
	~SavedMessages();

	[[nodiscard]] bool supported() const;
	void markUnsupported();

	[[nodiscard]] ChannelData *parentChat() const;
	[[nodiscard]] not_null<History*> owningHistory() const;

	[[nodiscard]] Session &owner() const;
	[[nodiscard]] Main::Session &session() const;

	[[nodiscard]] not_null<Dialogs::MainList*> chatsList();
	[[nodiscard]] not_null<SavedSublist*> sublist(not_null<PeerData*> peer);
	[[nodiscard]] SavedSublist *sublistLoaded(not_null<PeerData*> peer);
	void requestSublist(not_null<PeerData*> peer, Fn<void()> done = nullptr);

	[[nodiscard]] rpl::producer<> chatsListChanges() const;
	[[nodiscard]] rpl::producer<> chatsListLoadedEvents() const;

	[[nodiscard]] rpl::producer<> destroyed() const;
	[[nodiscard]] auto sublistDestroyed() const
		-> rpl::producer<not_null<SavedSublist*>>;

	void preloadSublists();
	void loadMore();
	void clearAllUnreadReactions();

	void apply(const MTPDupdatePinnedSavedDialogs &update);
	void apply(const MTPDupdateSavedDialogPinned &update);
	void applySublistDeleted(not_null<PeerData*> sublistPeer);

	void listMessageChanged(HistoryItem *from, HistoryItem *to);
	[[nodiscard]] int recentSublistsListVersion() const;
	void recentSublistsInvalidate(not_null<SavedSublist*> sublist);
	[[nodiscard]] auto recentSublists() const
		-> const std::vector<not_null<SavedSublist*>> &;

	void markUnreadCountsUnknown(MsgId readTillId);
	void updateUnreadCounts(
		MsgId readTillId,
		const base::flat_map<not_null<SavedSublist*>, int> &counts);

	void clear();

	void saveActiveSubsectionThread(not_null<Thread*> thread);
	Thread *activeSubsectionThread() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	struct SublistRequest {
		mtpRequestId id = 0;
		std::vector<Fn<void()>> callbacks;
	};
	struct ApplyResult {
		SavedMessagesOffsets offset;
		bool allLoaded = false;
	};

	void loadPinned();
	ApplyResult applyReceivedSublists(
		const MTPmessages_SavedDialogs &result,
		SavedMessagesOffsets &updateOffsets);
	ApplyResult applyReceivedSublists(
		const MTPmessages_SavedDialogs &result,
		bool pinned = false);

	void reorderLastSublists();
	void requestSomeStale();
	void finishSublistRequest(not_null<PeerData*> peer);

	void sendLoadMore();
	void sendLoadMoreRequests();

	const not_null<Session*> _owner;
	ChannelData *_parentChat = nullptr;
	History *_owningHistory = nullptr;

	rpl::event_stream<not_null<SavedSublist*>> _sublistDestroyed;

	Dialogs::MainList _chatsList;
	base::flat_map<
		not_null<PeerData*>,
		std::unique_ptr<SavedSublist>> _sublists;
	base::flat_map<not_null<PeerData*>, SublistRequest> _sublistRequests;
	base::flat_set<not_null<PeerData*>> _stalePeers;
	mtpRequestId _staleRequestId = 0;

	mtpRequestId _loadMoreRequestId = 0;
	mtpRequestId _pinnedRequestId = 0;

	SavedMessagesOffsets _offset;

	SingleQueuedInvokation _loadMore;
	bool _loadMoreScheduled = false;

	std::vector<not_null<SavedSublist*>> _lastSublists;
	int _lastSublistsVersion = 0;

	rpl::event_stream<> _chatsListChanges;
	rpl::event_stream<> _chatsListLoadedEvents;

	SavedSublist *_activeSubsectionSublist = nullptr;

	bool _pinnedLoaded = false;
	bool _unsupported = false;

	rpl::lifetime _lifetime;

};

} // namespace Data
