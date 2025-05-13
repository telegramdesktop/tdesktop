/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_main_list.h"

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;
class SavedSublist;

class SavedMessages final {
public:
	explicit SavedMessages(
		not_null<Session*> owner,
		ChannelData *parentChat = nullptr);
	~SavedMessages();

	[[nodiscard]] bool supported() const;
	[[nodiscard]] ChannelData *parentChat() const;

	[[nodiscard]] Session &owner() const;
	[[nodiscard]] Main::Session &session() const;

	[[nodiscard]] not_null<Dialogs::MainList*> chatsList();
	[[nodiscard]] not_null<SavedSublist*> sublist(not_null<PeerData*> peer);
	[[nodiscard]] SavedSublist *sublistLoaded(not_null<PeerData*> peer);

	[[nodiscard]] rpl::producer<> chatsListChanges() const;
	[[nodiscard]] rpl::producer<> chatsListLoadedEvents() const;

	[[nodiscard]] rpl::producer<> destroyed() const;
	[[nodiscard]] auto sublistDestroyed() const
		-> rpl::producer<not_null<SavedSublist*>>;

	void preloadSublists();
	void loadMore();
	void loadMore(not_null<SavedSublist*> sublist);

	void apply(const MTPDupdatePinnedSavedDialogs &update);
	void apply(const MTPDupdateSavedDialogPinned &update);

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void loadPinned();
	void apply(const MTPmessages_SavedDialogs &result, bool pinned);

	void sendLoadMore();
	void sendLoadMore(not_null<SavedSublist*> sublist);
	void sendLoadMoreRequests();

	const not_null<Session*> _owner;
	ChannelData *_parentChat = nullptr;

	rpl::event_stream<not_null<SavedSublist*>> _sublistDestroyed;

	Dialogs::MainList _chatsList;
	base::flat_map<
		not_null<PeerData*>,
		std::unique_ptr<SavedSublist>> _sublists;

	base::flat_map<not_null<SavedSublist*>, mtpRequestId> _loadMoreRequests;
	mtpRequestId _loadMoreRequestId = 0;
	mtpRequestId _pinnedRequestId = 0;

	TimeId _offsetDate = 0;
	MsgId _offsetId = 0;
	PeerData *_offsetPeer = nullptr;

	SingleQueuedInvokation _loadMore;
	base::flat_set<not_null<SavedSublist*>> _loadMoreSublistsScheduled;
	bool _loadMoreScheduled = false;

	rpl::event_stream<> _chatsListChanges;
	rpl::event_stream<> _chatsListLoadedEvents;

	bool _pinnedLoaded = false;
	bool _unsupported = false;

	rpl::lifetime _lifetime;

};

} // namespace Data
