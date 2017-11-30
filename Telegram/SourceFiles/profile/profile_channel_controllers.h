/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include <rpl/variable.h>
#include "boxes/peer_list_box.h"
#include "mtproto/sender.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "info/profile/info_profile_members_controllers.h"

namespace Window {
class Controller;
} // namespace Window

namespace Profile {

// Viewing admins, banned or restricted users list with search.
class ParticipantsBoxController
	: public PeerListController
	, private base::Subscriber
	, private MTP::Sender
	, public base::has_weak_ptr {
public:
	enum class Role {
		Profile,
		Members,
		Admins,
		Restricted,
		Kicked,
	};
	static void Start(
		not_null<Window::Controller*> window,
		not_null<ChannelData*> channel,
		Role role);

	struct Additional {
		std::map<not_null<UserData*>, MTPChannelAdminRights> adminRights;
		std::set<not_null<UserData*>> adminCanEdit;
		std::map<not_null<UserData*>, not_null<UserData*>> adminPromotedBy;
		std::map<not_null<UserData*>, MTPChannelBannedRights> restrictedRights;
		std::set<not_null<UserData*>> kicked;
		std::map<not_null<UserData*>, not_null<UserData*>> restrictedBy;
		std::set<not_null<UserData*>> external;
		std::set<not_null<UserData*>> infoNotLoaded;
		UserData *creator = nullptr;
	};

	ParticipantsBoxController(
		not_null<Window::Controller*> window,
		not_null<ChannelData*> channel,
		Role role);

	void addNewItem();

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowActionClicked(not_null<PeerListRow*> row) override;
	Ui::PopupMenu *rowContextMenu(
		not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	void peerListSearchAddRow(not_null<PeerData*> peer) override;
	std::unique_ptr<PeerListRow> createSearchRow(
		not_null<PeerData*> peer) override;
	std::unique_ptr<PeerListRow> createRestoredRow(
		not_null<PeerData*> peer) override;

	std::unique_ptr<PeerListState> saveState() override;
	void restoreState(std::unique_ptr<PeerListState> state) override;

	// Callback(not_null<UserData*>)
	template <typename Callback>
	static void HandleParticipant(
		const MTPChannelParticipant &participant,
		Role role,
		not_null<Additional*> additional,
		Callback callback);

	rpl::producer<int> onlineCountValue() const override;

protected:
	virtual std::unique_ptr<PeerListRow> createRow(not_null<UserData*> user) const;

private:
	using Row = Info::Profile::MemberListRow;
	using Type = Row::Type;
	using Rights = Row::Rights;
	struct SavedState : SavedStateBase {
		std::unique_ptr<PeerListSearchController::SavedStateBase> searchState;
		int offset = 0;
		bool allLoaded = false;
		bool wasLoading = false;
		Additional additional;
		rpl::lifetime lifetime;
	};

	static std::unique_ptr<PeerListSearchController> CreateSearchController(
		not_null<ChannelData*> channel,
		Role role,
		not_null<Additional*> additional);

	void setNonEmptyDescription();
	void setupSortByOnline();
	void setupListChangeViewers();
	void sortByOnlineDelayed();
	void sortByOnline();
	void showAdmin(not_null<UserData*> user);
	void editAdminDone(not_null<UserData*> user, const MTPChannelAdminRights &rights);
	void showRestricted(not_null<UserData*> user);
	void editRestrictedDone(not_null<UserData*> user, const MTPChannelBannedRights &rights);
	void removeKicked(not_null<PeerListRow*> row, not_null<UserData*> user);
	void kickMember(not_null<UserData*> user);
	void kickMemberSure(not_null<UserData*> user);
	bool appendRow(not_null<UserData*> user);
	bool prependRow(not_null<UserData*> user);
	bool removeRow(not_null<UserData*> user);
	void refreshCustomStatus(not_null<PeerListRow*> row) const;
	bool feedMegagroupLastParticipants();
	void refreshOnlineCount();
	Type computeType(not_null<UserData*> user) const;
	void recomputeTypeFor(not_null<UserData*> user);
	bool canEditAdmin(not_null<UserData*> user) const;
	bool canRestrictUser(not_null<UserData*> user) const;
	bool canEditAdminByRights(not_null<UserData*> user) const;

	not_null<Window::Controller*> _window;
	not_null<ChannelData*> _channel;
	Role _role = Role::Admins;
	int _offset = 0;
	mtpRequestId _loadRequestId = 0;
	bool _allLoaded = false;
	Additional _additional;
	QPointer<BoxContent> _editBox;
	QPointer<PeerListBox> _addBox;

	base::Timer _sortByOnlineTimer;
	rpl::variable<int> _onlineCount = 0;

};

// Members, banned and restricted users server side search.
class ParticipantsBoxSearchController
	: public PeerListSearchController
	, private MTP::Sender {
public:
	using Role = ParticipantsBoxController::Role;
	using Additional = ParticipantsBoxController::Additional;

	ParticipantsBoxSearchController(not_null<ChannelData*> channel, Role role, not_null<Additional*> additional);

	void searchQuery(const QString &query) override;
	bool isLoading() override;
	bool loadMoreRows() override;

	std::unique_ptr<SavedStateBase> saveState() override;
	void restoreState(std::unique_ptr<SavedStateBase> state) override;

private:
	struct SavedState : SavedStateBase {
		QString query;
		int offset = 0;
		bool allLoaded = false;
		bool wasLoading = false;
	};
	struct CacheEntry {
		MTPchannels_ChannelParticipants result;
		int requestedCount = 0;
	};
	struct Query {
		QString text;
		int offset = 0;
	};

	void searchOnServer();
	bool searchInCache();
	void searchDone(
		mtpRequestId requestId,
		const MTPchannels_ChannelParticipants &result,
		int requestedCount);

	not_null<ChannelData*> _channel;
	Role _role = Role::Restricted;
	not_null<Additional*> _additional;

	base::Timer _timer;
	QString _query;
	mtpRequestId _requestId = 0;
	int _offset = 0;
	bool _allLoaded = false;
	std::map<QString, CacheEntry> _cache;
	std::map<mtpRequestId, Query> _queries;

};

// Adding an admin, banned or restricted user from channel members with search + contacts search + global search.
class AddParticipantBoxController : public PeerListController, private base::Subscriber, private MTP::Sender, public base::has_weak_ptr {
public:
	using Role = ParticipantsBoxController::Role;
	using Additional = ParticipantsBoxController::Additional;

	using AdminDoneCallback = base::lambda<void(not_null<UserData*> user, const MTPChannelAdminRights &adminRights)>;
	using BannedDoneCallback = base::lambda<void(not_null<UserData*> user, const MTPChannelBannedRights &bannedRights)>;
	AddParticipantBoxController(not_null<ChannelData*> channel, Role role, AdminDoneCallback adminDoneCallback, BannedDoneCallback bannedDoneCallback);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	std::unique_ptr<PeerListRow> createSearchRow(not_null<PeerData*> peer) override;

	// Callback(not_null<UserData*>)
	template <typename Callback>
	static void HandleParticipant(const MTPChannelParticipant &participant, not_null<Additional*> additional, Callback callback);

private:
	template <typename Callback>
	bool checkInfoLoaded(not_null<UserData*> user, Callback callback);

	void showAdmin(not_null<UserData*> user, bool sure = false);
	void editAdminDone(not_null<UserData*> user, const MTPChannelAdminRights &rights);
	void showRestricted(not_null<UserData*> user, bool sure = false);
	void editRestrictedDone(not_null<UserData*> user, const MTPChannelBannedRights &rights);
	void kickUser(not_null<UserData*> user, bool sure = false);
	void restrictUserSure(not_null<UserData*> user, const MTPChannelBannedRights &oldRights, const MTPChannelBannedRights &newRights);
	bool appendRow(not_null<UserData*> user);
	bool prependRow(not_null<UserData*> user);
	std::unique_ptr<PeerListRow> createRow(not_null<UserData*> user) const;

	not_null<ChannelData*> _channel;
	Role _role = Role::Admins;
	int _offset = 0;
	mtpRequestId _loadRequestId = 0;
	bool _allLoaded = false;
	Additional _additional;
	QPointer<BoxContent> _editBox;
	AdminDoneCallback _adminDoneCallback;
	BannedDoneCallback _bannedDoneCallback;

};

// Finds channel members, then contacts, then global search results.
class AddParticipantBoxSearchController : public PeerListSearchController, private MTP::Sender {
public:
	using Role = ParticipantsBoxController::Role;
	using Additional = ParticipantsBoxController::Additional;

	AddParticipantBoxSearchController(not_null<ChannelData*> channel, not_null<Additional*> additional);

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
	void searchParticipantsDone(mtpRequestId requestId, const MTPchannels_ChannelParticipants &result, int requestedCount);
	bool searchGlobalInCache();
	void searchGlobalDone(mtpRequestId requestId, const MTPcontacts_Found &result);
	void requestParticipants();
	void addChatsContacts();
	void requestGlobal();

	not_null<ChannelData*> _channel;
	not_null<Additional*> _additional;

	base::Timer _timer;
	QString _query;
	mtpRequestId _requestId = 0;
	int _offset = 0;
	bool _participantsLoaded = false;
	bool _chatsContactsAdded = false;
	bool _globalLoaded = false;
	std::map<QString, CacheEntry> _participantsCache;
	std::map<mtpRequestId, Query> _participantsQueries;
	std::map<QString, MTPcontacts_Found> _globalCache;
	std::map<mtpRequestId, QString> _globalQueries;

};

} // namespace Profile
