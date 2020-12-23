/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_controllers.h"
#include "boxes/peers/edit_participants_box.h"

namespace Window {
class SessionNavigation;
} // namespace Window

class AddParticipantsBoxController : public ContactsBoxController {
public:
	static void Start(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChatData*> chat);
	static void Start(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel);
	static void Start(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel,
		base::flat_set<not_null<UserData*>> &&alreadyIn);

	explicit AddParticipantsBoxController(not_null<Main::Session*> session);
	explicit AddParticipantsBoxController(not_null<PeerData*> peer);
	AddParticipantsBoxController(
		not_null<PeerData*> peer,
		base::flat_set<not_null<UserData*>> &&alreadyIn);

	[[nodiscard]] not_null<PeerData*> peer() const {
		return _peer;
	}

	void rowClicked(not_null<PeerListRow*> row) override;
	void itemDeselectedHook(not_null<PeerData*> peer) override;

protected:
	void prepareViewHook() override;
	std::unique_ptr<PeerListRow> createRow(
		not_null<UserData*> user) override;

private:
	static void Start(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel,
		base::flat_set<not_null<UserData*>> &&alreadyIn,
		bool justCreated);

	bool inviteSelectedUsers(not_null<PeerListBox*> box) const;
	void subscribeToMigration();
	int alreadyInCount() const;
	bool isAlreadyIn(not_null<UserData*> user) const;
	int fullCount() const;
	void updateTitle();

	PeerData *_peer = nullptr;
	base::flat_set<not_null<UserData*>> _alreadyIn;

};

// Adding an admin, banned or restricted user from channel members
// with search + contacts search + global search.
class AddSpecialBoxController
	: public PeerListController
	, private base::Subscriber
	, public base::has_weak_ptr {
public:
	using Role = ParticipantsBoxController::Role;

	using AdminDoneCallback = Fn<void(
		not_null<UserData*> user,
		const MTPChatAdminRights &adminRights,
		const QString &rank)>;
	using BannedDoneCallback = Fn<void(
		not_null<UserData*> user,
		const MTPChatBannedRights &bannedRights)>;
	AddSpecialBoxController(
		not_null<PeerData*> peer,
		Role role,
		AdminDoneCallback adminDoneCallback,
		BannedDoneCallback bannedDoneCallback);

	[[nodiscard]] not_null<PeerData*> peer() const {
		return _peer;
	}
	[[nodiscard]] Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	[[nodiscard]] std::unique_ptr<PeerListRow> createSearchRow(
		not_null<PeerData*> peer) override;

private:
	template <typename Callback>
	bool checkInfoLoaded(not_null<UserData*> user, Callback callback);

	void prepareChatRows(not_null<ChatData*> chat);
	void rebuildChatRows(not_null<ChatData*> chat);

	void showAdmin(not_null<UserData*> user, bool sure = false);
	void editAdminDone(
		not_null<UserData*> user,
		const MTPChatAdminRights &rights,
		const QString &rank);
	void showRestricted(not_null<UserData*> user, bool sure = false);
	void editRestrictedDone(
		not_null<UserData*> user,
		const MTPChatBannedRights &rights);
	void kickUser(not_null<UserData*> user, bool sure = false);
	bool appendRow(not_null<UserData*> user);
	bool prependRow(not_null<UserData*> user);
	std::unique_ptr<PeerListRow> createRow(not_null<UserData*> user) const;

	void subscribeToMigration();
	void migrate(not_null<ChannelData*> channel);

	not_null<PeerData*> _peer;
	MTP::Sender _api;
	Role _role = Role::Admins;
	int _offset = 0;
	mtpRequestId _loadRequestId = 0;
	bool _allLoaded = false;
	ParticipantsAdditionalData _additional;
	std::unique_ptr<ParticipantsOnlineSorter> _onlineSorter;
	Ui::BoxPointer _editBox;
	QPointer<Ui::BoxContent> _editParticipantBox;
	AdminDoneCallback _adminDoneCallback;
	BannedDoneCallback _bannedDoneCallback;

protected:
	bool _excludeSelf = true;

};

// Finds chat/channel members, then contacts, then global search results.
class AddSpecialBoxSearchController
	: public PeerListSearchController
	, private base::Subscriber {
public:
	using Role = ParticipantsBoxController::Role;

	AddSpecialBoxSearchController(
		not_null<PeerData*> peer,
		not_null<ParticipantsAdditionalData*> additional);

	void searchQuery(const QString &query) override;
	bool isLoading() override;
	bool loadMoreRows() override;

private:
	struct CacheEntry {
		MTPchannels_ChannelParticipants result;
		int requestedCount = 0;
	};
	struct Query {
		QString text;
		int offset = 0;
	};

	void searchOnServer();
	bool searchParticipantsInCache();
	void searchParticipantsDone(
		mtpRequestId requestId,
		const MTPchannels_ChannelParticipants &result,
		int requestedCount);
	bool searchGlobalInCache();
	void searchGlobalDone(
		mtpRequestId requestId,
		const MTPcontacts_Found &result);
	void requestParticipants();
	void addChatMembers(not_null<ChatData*> chat);
	void addChatsContacts();
	void requestGlobal();

	void subscribeToMigration();

	not_null<PeerData*> _peer;
	not_null<ParticipantsAdditionalData*> _additional;
	MTP::Sender _api;

	base::Timer _timer;
	QString _query;
	mtpRequestId _requestId = 0;
	int _offset = 0;
	bool _participantsLoaded = false;
	bool _chatsContactsAdded = false;
	bool _chatMembersAdded = false;
	bool _globalLoaded = false;
	std::map<QString, CacheEntry> _participantsCache;
	std::map<mtpRequestId, Query> _participantsQueries;
	std::map<QString, MTPcontacts_Found> _globalCache;
	std::map<mtpRequestId, QString> _globalQueries;

};
