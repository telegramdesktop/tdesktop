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
#include "profile/profile_block_settings.h"

#include "styles/style_profile.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "boxes/confirm_box.h"
#include "boxes/contacts_box.h"
#include "boxes/peer_list_box.h"
#include "boxes/edit_participant_box.h"
#include "observer_peer.h"
#include "auth_session.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"

namespace Profile {
namespace {

constexpr auto kBannedPerPage = 40;
constexpr auto kAdminsPerPage = 200;
constexpr auto kParticipantsPerPage = 200;

// Viewing admins, banned or restricted users list with search.
class ParticipantsBoxController : public PeerListController, private base::Subscriber, private MTP::Sender, public base::enable_weak_from_this {
public:
	enum class Role {
		Admins,
		Restricted,
		Kicked,
	};
	static void Start(gsl::not_null<ChannelData*> channel, Role role);

	struct Additional {
		std::map<gsl::not_null<UserData*>, MTPChannelAdminRights> adminRights;
		std::set<gsl::not_null<UserData*>> adminCanEdit;
		std::map<gsl::not_null<UserData*>, gsl::not_null<UserData*>> adminPromotedBy;
		std::map<gsl::not_null<UserData*>, MTPChannelBannedRights> restrictedRights;
		std::set<gsl::not_null<UserData*>> kicked;
		std::set<gsl::not_null<UserData*>> external;
		std::set<gsl::not_null<UserData*>> infoNotLoaded;
		UserData *creator = nullptr;
	};

	ParticipantsBoxController(gsl::not_null<ChannelData*> channel, Role role);

	void addNewItem();

	void prepare() override;
	void rowClicked(gsl::not_null<PeerListRow*> row) override;
	void rowActionClicked(gsl::not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	void peerListSearchAddRow(gsl::not_null<PeerData*> peer) override;

	// Callback(gsl::not_null<UserData*>)
	template <typename Callback>
	static void HandleParticipant(const MTPChannelParticipant &participant, Role role, gsl::not_null<Additional*> additional, Callback callback);

private:
	void editAdmin(gsl::not_null<UserData*> user);
	void editAdminDone(gsl::not_null<UserData*> user, const MTPChannelAdminRights &rights);
	void editRestricted(gsl::not_null<UserData*> user);
	void editRestrictedDone(gsl::not_null<UserData*> user, const MTPChannelBannedRights &rights);
	void removeKicked(gsl::not_null<PeerListRow*> row, gsl::not_null<UserData*> user);
	bool appendRow(gsl::not_null<UserData*> user);
	bool prependRow(gsl::not_null<UserData*> user);
	std::unique_ptr<PeerListRow> createRow(gsl::not_null<UserData*> user) const;

	gsl::not_null<ChannelData*> _channel;
	Role _role = Role::Admins;
	int _offset = 0;
	mtpRequestId _loadRequestId = 0;
	bool _allLoaded = false;
	Additional _additional;
	QPointer<BoxContent> _editBox;
	QPointer<PeerListBox> _addBox;

};

// Banned and restricted users server side search.
class BannedBoxSearchController : public PeerListSearchController, private MTP::Sender {
public:
	using Role = ParticipantsBoxController::Role;
	using Additional = ParticipantsBoxController::Additional;

	BannedBoxSearchController(gsl::not_null<ChannelData*> channel, Role role, gsl::not_null<Additional*> additional);

	void searchQuery(const QString &query) override;
	bool isLoading() override;
	bool loadMoreRows() override;

private:
	bool searchInCache();
	void searchOnServer();
	void searchDone(const MTPchannels_ChannelParticipants &result, mtpRequestId requestId);

	gsl::not_null<ChannelData*> _channel;
	Role _role = Role::Restricted;
	gsl::not_null<Additional*> _additional;

	base::Timer _timer;
	QString _query;
	mtpRequestId _requestId = 0;
	int _offset = 0;
	bool _allLoaded = false;
	std::map<QString, MTPchannels_ChannelParticipants> _cache;
	std::map<mtpRequestId, std::pair<QString, int>> _queries; // query, offset

};

// Adding an admin, banned or restricted user from channel members with search + contacts search + global search.
class AddParticipantBoxController : public PeerListController, private base::Subscriber, private MTP::Sender, public base::enable_weak_from_this {
public:
	using Role = ParticipantsBoxController::Role;
	using Additional = ParticipantsBoxController::Additional;

	AddParticipantBoxController(gsl::not_null<ChannelData*> channel, Role role, base::lambda<void()> doneCallback);

	void prepare() override;
	void rowClicked(gsl::not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	void peerListSearchAddRow(gsl::not_null<PeerData*> peer) override;

	// Callback(gsl::not_null<UserData*>)
	template <typename Callback>
	static void HandleParticipant(const MTPChannelParticipant &participant, gsl::not_null<Additional*> additional, Callback callback);

private:
	template <typename Callback>
	bool checkInfoLoaded(gsl::not_null<UserData*> user, Callback callback);

	void editAdmin(gsl::not_null<UserData*> user, bool sure = false);
	void editAdminDone(gsl::not_null<UserData*> user, const MTPChannelAdminRights &rights);
	void editRestricted(gsl::not_null<UserData*> user, bool sure = false);
	void editRestrictedDone(gsl::not_null<UserData*> user, const MTPChannelBannedRights &rights);
	void kickUser(gsl::not_null<UserData*> user, bool sure = false);
	void restrictUserSure(gsl::not_null<UserData*> user, const MTPChannelBannedRights &rights);
	bool appendRow(gsl::not_null<UserData*> user);
	bool prependRow(gsl::not_null<UserData*> user);
	std::unique_ptr<PeerListRow> createRow(gsl::not_null<UserData*> user) const;

	gsl::not_null<ChannelData*> _channel;
	Role _role = Role::Admins;
	int _offset = 0;
	mtpRequestId _loadRequestId = 0;
	bool _allLoaded = false;
	Additional _additional;
	QPointer<BoxContent> _editBox;
	base::lambda<void()> _doneCallback;

};

// Finds channel members, then contacts, then global search results.
class AddParticipantBoxSearchController : public PeerListSearchController, private MTP::Sender {
public:
	using Role = ParticipantsBoxController::Role;
	using Additional = ParticipantsBoxController::Additional;

	AddParticipantBoxSearchController(gsl::not_null<ChannelData*> channel, Role role, gsl::not_null<Additional*> additional);

	void searchQuery(const QString &query) override;
	bool isLoading() override;
	bool loadMoreRows() override;

private:
	bool searchInCache();
	void searchOnServer();
	void searchDone(const MTPchannels_ChannelParticipants &result, mtpRequestId requestId);

	gsl::not_null<ChannelData*> _channel;
	Role _role = Role::Admins;
	gsl::not_null<Additional*> _additional;

	base::Timer _timer;
	QString _query;
	mtpRequestId _requestId = 0;
	int _offset = 0;
	bool _allLoaded = false;
	std::map<QString, MTPchannels_ChannelParticipants> _cache;
	std::map<mtpRequestId, std::pair<QString, int>> _queries; // query, offset

};

ParticipantsBoxController::ParticipantsBoxController(gsl::not_null<ChannelData*> channel, Role role) : PeerListController((role == Role::Admins) ? nullptr : std::make_unique<BannedBoxSearchController>(channel, role, &_additional))
, _channel(channel)
, _role(role) {
	if (_channel->mgInfo) {
		_additional.creator = _channel->mgInfo->creator;
	}
}

void ParticipantsBoxController::Start(gsl::not_null<ChannelData*> channel, Role role) {
	auto controller = std::make_unique<ParticipantsBoxController>(channel, role);
	auto initBox = [role, channel, controller = controller.get()](PeerListBox *box) {
		box->addButton(langFactory(lng_close), [box] { box->closeBox(); });
		auto buttonText = [role] {
			switch (role) {
			case Role::Admins: return langFactory(lng_channel_add_admin);
			case Role::Restricted: return langFactory(lng_channel_add_restricted);
			case Role::Kicked: return langFactory(lng_channel_add_banned);
			}
			Unexpected("Role value in ParticipantsBoxController::Start()");
		};
		if ((role == Role::Admins && channel->canAddAdmins()) || ((role == Role::Restricted || role == Role::Kicked) && channel->canBanMembers())) {
			box->addLeftButton(buttonText(), [controller] { controller->addNewItem(); });
		}
	};
	Ui::show(Box<PeerListBox>(std::move(controller), std::move(initBox)), KeepOtherLayers);
}

void ParticipantsBoxController::addNewItem() {
	auto weak = base::weak_unique_ptr<ParticipantsBoxController>(this);
	_addBox = Ui::show(Box<PeerListBox>(std::make_unique<AddParticipantBoxController>(_channel, _role, [weak] {
		if (weak && weak->_addBox) {
			weak->_addBox->closeBox();
		}
	}), [](PeerListBox *box) {
		box->addButton(langFactory(lng_cancel), [box] { box->closeBox(); });
	}), KeepOtherLayers);
}

void ParticipantsBoxController::peerListSearchAddRow(gsl::not_null<PeerData*> peer) {
	Expects(_role != Role::Admins);
	PeerListController::peerListSearchAddRow(peer);
	if (_role == Role::Restricted && delegate()->peerListFullRowsCount() > 0) {
		setDescriptionText(QString());
	}
}

template <typename Callback>
void ParticipantsBoxController::HandleParticipant(const MTPChannelParticipant &participant, Role role, gsl::not_null<Additional*> additional, Callback callback) {
	if (role == Role::Admins && participant.type() == mtpc_channelParticipantAdmin) {
		auto &admin = participant.c_channelParticipantAdmin();
		if (auto user = App::userLoaded(admin.vuser_id.v)) {
			additional->adminRights[user] = admin.vadmin_rights;
			if (admin.is_can_edit()) {
				additional->adminCanEdit.emplace(user);
			} else {
				additional->adminCanEdit.erase(user);
			}
			if (auto promoted = App::userLoaded(admin.vpromoted_by.v)) {
				auto it = additional->adminPromotedBy.find(user);
				if (it == additional->adminPromotedBy.end()) {
					additional->adminPromotedBy.emplace(user, promoted);
				} else {
					it->second = promoted;
				}
			} else {
				LOG(("API Error: No user %1 for admin promoted by.").arg(admin.vpromoted_by.v));
			}
			callback(user);
		}
	} else if (role == Role::Admins && participant.type() == mtpc_channelParticipantCreator) {
		auto &creator = participant.c_channelParticipantCreator();
		if (auto user = App::userLoaded(creator.vuser_id.v)) {
			additional->creator = user;
			callback(user);
		}
	} else if ((role == Role::Restricted || role == Role::Kicked) && participant.type() == mtpc_channelParticipantBanned) {
		auto &banned = participant.c_channelParticipantBanned();
		if (auto user = App::userLoaded(banned.vuser_id.v)) {
			additional->restrictedRights[user] = banned.vbanned_rights;
			callback(user);
		}
	} else {
		LOG(("API Error: Bad participant type got while requesting for participants: %1").arg(participant.type()));
	}
}

void ParticipantsBoxController::prepare() {
	if (_role == Role::Admins) {
		delegate()->peerListSetSearchMode(PeerListSearchMode::Local);
		delegate()->peerListSetTitle(langFactory(lng_channel_admins));
	} else {
		delegate()->peerListSetSearchMode(PeerListSearchMode::Complex);
		delegate()->peerListSetTitle(langFactory((_role == Role::Restricted) ? lng_restricted_list_title : lng_banned_list_title));
	}
	setDescriptionText(lang(lng_contacts_loading));
	setSearchNoResultsText(lang(lng_blocked_list_not_found));
	delegate()->peerListRefreshRows();

	loadMoreRows();
}

void ParticipantsBoxController::loadMoreRows() {
	if (searchController() && searchController()->loadMoreRows()) {
		return;
	}
	if (_loadRequestId || _allLoaded) {
		return;
	}

	auto filter = [this] {
		if (_role == Role::Admins) {
			return MTP_channelParticipantsAdmins();
		} else if (_role == Role::Restricted) {
			return MTP_channelParticipantsBanned(MTP_string(QString()));
		}
		return MTP_channelParticipantsKicked(MTP_string(QString()));
	};
	auto perPage = (_role == Role::Admins) ? kAdminsPerPage : kBannedPerPage;
	_loadRequestId = request(MTPchannels_GetParticipants(_channel->inputChannel, filter(), MTP_int(_offset), MTP_int(perPage))).done([this](const MTPchannels_ChannelParticipants &result) {
		Expects(result.type() == mtpc_channels_channelParticipants);

		_loadRequestId = 0;

		if (!_offset) {
			setDescriptionText((_role == Role::Restricted) ? lang(lng_group_blocked_list_about) : QString());
		}
		auto &participants = result.c_channels_channelParticipants();
		App::feedUsers(participants.vusers);

		auto &list = participants.vparticipants.v;
		if (list.isEmpty()) {
			_allLoaded = true;
		} else {
			for_const (auto &participant, list) {
				++_offset;
				HandleParticipant(participant, _role, &_additional, [this](gsl::not_null<UserData*> user) {
					appendRow(user);
				});
			}
		}
		delegate()->peerListRefreshRows();
	}).fail([this](const RPCError &error) {
		_loadRequestId = 0;
	}).send();
}

void ParticipantsBoxController::rowClicked(gsl::not_null<PeerListRow*> row) {
	Ui::showPeerHistoryAsync(row->peer()->id, ShowAtUnreadMsgId);
}

void ParticipantsBoxController::rowActionClicked(gsl::not_null<PeerListRow*> row) {
	auto user = row->peer()->asUser();
	Expects(user != nullptr);

	if (_role == Role::Admins) {
		editAdmin(user);
	} else if (_role == Role::Restricted) {
		editRestricted(user);
	} else {
		removeKicked(row, user);
	}
}

void ParticipantsBoxController::editAdmin(gsl::not_null<UserData*> user) {
	if (_additional.adminCanEdit.find(user) == _additional.adminCanEdit.end()) {
		return;
	}

	auto it = _additional.adminRights.find(user);
	t_assert(it != _additional.adminRights.cend());
	auto weak = base::weak_unique_ptr<ParticipantsBoxController>(this);
	_editBox = Ui::show(Box<EditAdminBox>(_channel, user, it->second, [megagroup = _channel.get(), user, weak](const MTPChannelAdminRights &rights) {
		MTP::send(MTPchannels_EditAdmin(megagroup->inputChannel, user->inputUser, rights), rpcDone([megagroup, user, weak, rights](const MTPUpdates &result) {
			if (App::main()) App::main()->sentUpdatesReceived(result);
			megagroup->applyEditAdmin(user, rights);
			if (weak) {
				weak->editAdminDone(user, rights);
			}
		}));
	}), KeepOtherLayers);
}

void ParticipantsBoxController::editAdminDone(gsl::not_null<UserData*> user, const MTPChannelAdminRights &rights) {
	if (_editBox) _editBox->closeBox();
	if (rights.c_channelAdminRights().vflags.v == 0) {
		if (auto row = delegate()->peerListFindRow(user->id)) {
			delegate()->peerListRemoveRow(row);
			if (!delegate()->peerListFullRowsCount()) {
				setDescriptionText(lang(lng_blocked_list_not_found));
			}
			delegate()->peerListRefreshRows();
		}
	} else {
		_additional.adminRights[user] = rights;
	}
}

void ParticipantsBoxController::editRestricted(gsl::not_null<UserData*> user) {
	auto it = _additional.restrictedRights.find(user);
	t_assert(it != _additional.restrictedRights.cend());
	auto weak = base::weak_unique_ptr<ParticipantsBoxController>(this);
	_editBox = Ui::show(Box<EditRestrictedBox>(_channel, user, it->second, [megagroup = _channel.get(), user, weak](const MTPChannelBannedRights &rights) {
		MTP::send(MTPchannels_EditBanned(megagroup->inputChannel, user->inputUser, rights), rpcDone([megagroup, user, weak, rights](const MTPUpdates &result) {
			if (App::main()) App::main()->sentUpdatesReceived(result);
			megagroup->applyEditBanned(user, rights);
			if (weak) {
				weak->editRestrictedDone(user, rights);
			}
		}));
	}), KeepOtherLayers);
}

void ParticipantsBoxController::editRestrictedDone(gsl::not_null<UserData*> user, const MTPChannelBannedRights &rights) {
	if (_editBox) _editBox->closeBox();
	if (rights.c_channelBannedRights().vflags.v == 0 || rights.c_channelBannedRights().is_view_messages()) {
		if (auto row = delegate()->peerListFindRow(user->id)) {
			delegate()->peerListRemoveRow(row);
			if (!delegate()->peerListFullRowsCount()) {
				setDescriptionText(lang(lng_blocked_list_not_found));
			}
			delegate()->peerListRefreshRows();
		}
	} else {
		_additional.restrictedRights[user] = rights;
	}
}

void ParticipantsBoxController::removeKicked(gsl::not_null<PeerListRow*> row, gsl::not_null<UserData*> user) {
	delegate()->peerListRemoveRow(row);
	delegate()->peerListRefreshRows();

	AuthSession::Current().api().unblockParticipant(_channel, user);
}

bool ParticipantsBoxController::appendRow(gsl::not_null<UserData*> user) {
	if (delegate()->peerListFindRow(user->id)) {
		return false;
	}
	delegate()->peerListAppendRow(createRow(user));
	if (_role != Role::Kicked) {
		setDescriptionText(QString());
	}
	return true;
}

bool ParticipantsBoxController::prependRow(gsl::not_null<UserData*> user) {
	if (delegate()->peerListFindRow(user->id)) {
		return false;
	}
	delegate()->peerListPrependRow(createRow(user));
	if (_role != Role::Kicked) {
		setDescriptionText(QString());
	}
	return true;
}

std::unique_ptr<PeerListRow> ParticipantsBoxController::createRow(gsl::not_null<UserData*> user) const {
	auto row = std::make_unique<PeerListRowWithLink>(user);
	if (_role == Role::Admins) {
		auto promotedBy = _additional.adminPromotedBy.find(user);
		if (promotedBy == _additional.adminPromotedBy.end()) {
			row->setCustomStatus(lang(lng_channel_admin_status_creator));
		} else {
			row->setCustomStatus(lng_channel_admin_status_promoted_by(lt_user, App::peerName(promotedBy->second)));
		}
	}
	if (_role == Role::Restricted || (_role == Role::Admins && _additional.adminCanEdit.find(user) != _additional.adminCanEdit.end())) {
		row->setActionLink(lang(lng_profile_edit_permissions));
	} else if (_role == Role::Kicked) {
		row->setActionLink(lang(lng_blocked_list_unblock));
	}
	return std::move(row);
}

BannedBoxSearchController::BannedBoxSearchController(gsl::not_null<ChannelData*> channel, Role role, gsl::not_null<Additional*> additional)
: _channel(channel)
, _role(role)
, _additional(additional) {
	Expects(role != Role::Admins);
	_timer.setCallback([this] { searchOnServer(); });
}

void BannedBoxSearchController::searchQuery(const QString &query) {
	if (_query != query) {
		_query = query;
		_offset = 0;
		_requestId = 0;
		_allLoaded = false;
		if (!_query.isEmpty() && !searchInCache()) {
			_timer.callOnce(AutoSearchTimeout);
		} else {
			_timer.cancel();
		}
	}
}

bool BannedBoxSearchController::searchInCache() {
	auto it = _cache.find(_query);
	if (it != _cache.cend()) {
		_requestId = 0;
		searchDone(it->second, _requestId);
		return true;
	}
	return false;
}

void BannedBoxSearchController::searchOnServer() {
	Expects(!_query.isEmpty());
	loadMoreRows();
}

void BannedBoxSearchController::searchDone(const MTPchannels_ChannelParticipants &result, mtpRequestId requestId) {
	Expects(result.type() == mtpc_channels_channelParticipants);

	auto &participants = result.c_channels_channelParticipants();
	auto query = _query;
	if (requestId) {
		App::feedUsers(participants.vusers);
		auto it = _queries.find(requestId);
		if (it != _queries.cend()) {
			query = it->second.first; // query
			if (it->second.second == 0) { // offset
				_cache[query] = result;
			}
			_queries.erase(it);
		}
	}

	if (_requestId == requestId) {
		_requestId = 0;
		auto &list = participants.vparticipants.v;
		if (list.isEmpty()) {
			_allLoaded = true;
		} else {
			for_const (auto &participant, list) {
				++_offset;
				ParticipantsBoxController::HandleParticipant(participant, _role, _additional, [this](gsl::not_null<UserData*> user) {
					delegate()->peerListSearchAddRow(user);
				});
			}
		}
		delegate()->peerListSearchRefreshRows();
	}
}

bool BannedBoxSearchController::isLoading() {
	return _timer.isActive() || _requestId;
}

bool BannedBoxSearchController::loadMoreRows() {
	if (_query.isEmpty()) {
		return false;
	}
	if (!_allLoaded && !isLoading()) {
		auto filter = (_role == Role::Restricted) ? MTP_channelParticipantsBanned(MTP_string(_query)) : MTP_channelParticipantsKicked(MTP_string(_query));
		_requestId = request(MTPchannels_GetParticipants(_channel->inputChannel, filter, MTP_int(_offset), MTP_int(kBannedPerPage))).done([this](const MTPchannels_ChannelParticipants &result, mtpRequestId requestId) {
			searchDone(result, requestId);
		}).fail([this](const RPCError &error, mtpRequestId requestId) {
			if (_requestId == requestId) {
				_requestId = 0;
				_allLoaded = true;
				delegate()->peerListSearchRefreshRows();
			}
		}).send();
		_queries.emplace(_requestId, std::make_pair(_query, _offset));
	}
	return true;
}

AddParticipantBoxController::AddParticipantBoxController(gsl::not_null<ChannelData*> channel, Role role, base::lambda<void()> doneCallback)
: _channel(channel)
, _role(role)
, _doneCallback(std::move(doneCallback)) {
	if (_channel->mgInfo) {
		_additional.creator = _channel->mgInfo->creator;
	}
}

void AddParticipantBoxController::peerListSearchAddRow(gsl::not_null<PeerData*> peer) {
	if (peer->isSelf()) {
		return;
	}
	PeerListController::peerListSearchAddRow(peer);
}

void AddParticipantBoxController::prepare() {
	delegate()->peerListSetSearchMode(PeerListSearchMode::Complex);
	auto title = [this] {
		switch (_role) {
		case Role::Admins: return langFactory(lng_channel_add_admin);
		case Role::Restricted: return langFactory(lng_channel_add_restricted);
		case Role::Kicked: return langFactory(lng_channel_add_banned);
		}
		Unexpected("Role in AddParticipantBoxController::prepare()");
	};
	delegate()->peerListSetTitle(title());
	setDescriptionText(lang(lng_contacts_loading));
	setSearchNoResultsText(lang(lng_blocked_list_not_found));
	delegate()->peerListRefreshRows();

	loadMoreRows();
}

void AddParticipantBoxController::loadMoreRows() {
	if (searchController() && searchController()->loadMoreRows()) {
		return;
	}
	if (_loadRequestId || _allLoaded) {
		return;
	}

	_loadRequestId = request(MTPchannels_GetParticipants(_channel->inputChannel, MTP_channelParticipantsRecent(), MTP_int(_offset), MTP_int(kParticipantsPerPage))).done([this](const MTPchannels_ChannelParticipants &result) {
		Expects(result.type() == mtpc_channels_channelParticipants);

		_loadRequestId = 0;

		auto &participants = result.c_channels_channelParticipants();
		App::feedUsers(participants.vusers);

		auto &list = participants.vparticipants.v;
		if (list.isEmpty()) {
			_allLoaded = true;
		} else {
			for_const (auto &participant, list) {
				++_offset;
				HandleParticipant(participant, &_additional, [this](gsl::not_null<UserData*> user) {
					appendRow(user);
				});
			}
		}
		if (delegate()->peerListFullRowsCount() > 0) {
			setDescriptionText(QString());
		}
		delegate()->peerListRefreshRows();
	}).fail([this](const RPCError &error) {
		_loadRequestId = 0;
	}).send();
}

void AddParticipantBoxController::rowClicked(gsl::not_null<PeerListRow*> row) {
	auto user = row->peer()->asUser();
	switch (_role) {
	case Role::Admins: return editAdmin(user);
	case Role::Restricted: return editRestricted(user);
	case Role::Kicked: return kickUser(user);
	}
	Unexpected("Role in AddParticipantBoxController::rowClicked()");
}

template <typename Callback>
bool AddParticipantBoxController::checkInfoLoaded(gsl::not_null<UserData*> user, Callback callback) {
	if (_additional.infoNotLoaded.find(user) == _additional.infoNotLoaded.end()) {
		return true;
	}

	// We don't know what this user status is in the group.
	request(MTPchannels_GetParticipant(_channel->inputChannel, user->inputUser)).done([this, user, callback](const MTPchannels_ChannelParticipant &result) {
		Expects(result.type() == mtpc_channels_channelParticipant);
		auto &participant = result.c_channels_channelParticipant();
		App::feedUsers(participant.vusers);
		HandleParticipant(participant.vparticipant, &_additional, [](gsl::not_null<UserData*>) {});
		_additional.infoNotLoaded.erase(user);
		callback();
	}).fail([this, user, callback](const RPCError &error) {
		_additional.infoNotLoaded.erase(user);
		_additional.external.emplace(user);
		callback();
	}).send();
	return false;
}

void AddParticipantBoxController::editAdmin(gsl::not_null<UserData*> user, bool sure) {
	if (!checkInfoLoaded(user, [this, user] { editAdmin(user); })) {
		return;
	}

	if (sure && _editBox) {
		// Close the confirmation box.
		_editBox->closeBox();
	}

	// Check restrictions.
	auto weak = base::weak_unique_ptr<AddParticipantBoxController>(this);
	auto alreadyIt = _additional.adminRights.find(user);
	auto currentRights = EditAdminBox::DefaultRights(_channel);
	if (alreadyIt != _additional.adminRights.end() || _additional.creator == user) {
		// The user is already an admin.
		if (_additional.adminCanEdit.find(user) == _additional.adminCanEdit.end() || _additional.creator == user) {
			Ui::show(Box<InformBox>(lang(lng_error_cant_edit_admin)), KeepOtherLayers);
			return;
		}
		currentRights = alreadyIt->second;
	} else if (_additional.restrictedRights.find(user) != _additional.restrictedRights.end() || _additional.kicked.find(user) != _additional.kicked.end()) {
		// The user is banned or restricted.
		if (_channel->canBanMembers()) {
			if (!sure) {
				_editBox = Ui::show(Box<ConfirmBox>(lang(lng_sure_add_admin_unban), [weak, user] { weak->editAdmin(user, true); }), KeepOtherLayers);
				return;
			}
		} else {
			Ui::show(Box<InformBox>(lang(lng_error_cant_add_admin_unban)), KeepOtherLayers);
			return;
		}
	} else if (_additional.external.find(user) != _additional.external.end()) {
		// The user is not in the group yet.
		if (_channel->canAddMembers()) {
			if (!sure) {
				_editBox = Ui::show(Box<ConfirmBox>(lang(lng_sure_add_admin_invite), [weak, user] { weak->editAdmin(user, true); }), KeepOtherLayers);
				return;
			}
		} else {
			Ui::show(Box<InformBox>(lang(lng_error_cant_add_admin_invite)), KeepOtherLayers);
			return;
		}
	}

	// Finally edit the admin.
	_editBox = Ui::show(Box<EditAdminBox>(_channel, user, currentRights, [megagroup = _channel.get(), user, weak](const MTPChannelAdminRights &rights) {
		MTP::send(MTPchannels_EditAdmin(megagroup->inputChannel, user->inputUser, rights), rpcDone([megagroup, user, weak, rights](const MTPUpdates &result) {
			if (App::main()) App::main()->sentUpdatesReceived(result);
			megagroup->applyEditAdmin(user, rights);
			if (weak) {
				weak->editAdminDone(user, rights);
			}
		}));
	}), KeepOtherLayers);
}

void AddParticipantBoxController::editAdminDone(gsl::not_null<UserData*> user, const MTPChannelAdminRights &rights) {
	if (_editBox) _editBox->closeBox();
	_additional.restrictedRights.erase(user);
	_additional.kicked.erase(user);
	_additional.external.erase(user);
	if (rights.c_channelAdminRights().vflags.v == 0) {
		_additional.adminRights.erase(user);
		_additional.adminPromotedBy.erase(user);
		_additional.adminCanEdit.erase(user);
	} else {
		_additional.adminRights[user] = rights;
		_additional.adminCanEdit.emplace(user);
		auto it = _additional.adminPromotedBy.find(user);
		if (it == _additional.adminPromotedBy.end()) {
			_additional.adminPromotedBy.emplace(user, App::self());
		}
	}
	_doneCallback();
}

void AddParticipantBoxController::editRestricted(gsl::not_null<UserData*> user, bool sure) {
	if (!checkInfoLoaded(user, [this, user] { editRestricted(user); })) {
		return;
	}

	if (sure && _editBox) {
		// Close the confirmation box.
		_editBox->closeBox();
	}

	// Check restrictions.
	auto weak = base::weak_unique_ptr<AddParticipantBoxController>(this);
	auto alreadyIt = _additional.restrictedRights.find(user);
	auto currentRights = EditRestrictedBox::DefaultRights(_channel);
	if (alreadyIt != _additional.restrictedRights.end()) {
		// The user is already banned or restricted.
		currentRights = alreadyIt->second;
	} else if (_additional.adminRights.find(user) != _additional.adminRights.end() || _additional.creator == user) {
		// The user is an admin or creator.
		if (_additional.adminCanEdit.find(user) != _additional.adminCanEdit.end()) {
			if (!sure) {
				_editBox = Ui::show(Box<ConfirmBox>(lang(lng_sure_ban_admin), [weak, user] { weak->editRestricted(user, true); }), KeepOtherLayers);
				return;
			}
		} else {
			Ui::show(Box<InformBox>(lang(lng_error_cant_ban_admin)), KeepOtherLayers);
			return;
		}
	}

	// Finally edit the restricted.
	_editBox = Ui::show(Box<EditRestrictedBox>(_channel, user, currentRights, [user, weak](const MTPChannelBannedRights &rights) {
		if (weak) {
			weak->restrictUserSure(user, rights);
		}
	}), KeepOtherLayers);
}

void AddParticipantBoxController::restrictUserSure(gsl::not_null<UserData*> user, const MTPChannelBannedRights &rights) {
	auto weak = base::weak_unique_ptr<AddParticipantBoxController>(this);
	MTP::send(MTPchannels_EditBanned(_channel->inputChannel, user->inputUser, rights), rpcDone([megagroup = _channel.get(), user, weak, rights](const MTPUpdates &result) {
		if (App::main()) App::main()->sentUpdatesReceived(result);
		megagroup->applyEditBanned(user, rights);
		if (weak) {
			weak->editRestrictedDone(user, rights);
		}
	}));
}

void AddParticipantBoxController::editRestrictedDone(gsl::not_null<UserData*> user, const MTPChannelBannedRights &rights) {
	if (_editBox) _editBox->closeBox();
	_additional.adminRights.erase(user);
	_additional.adminCanEdit.erase(user);
	_additional.adminPromotedBy.erase(user);
	if (rights.c_channelBannedRights().vflags.v == 0) {
		_additional.restrictedRights.erase(user);
		_additional.kicked.erase(user);
	} else if (rights.c_channelBannedRights().vflags.v & MTPDchannelBannedRights::Flag::f_view_messages) {
		_additional.restrictedRights.erase(user);
		_additional.kicked.emplace(user);
	} else {
		_additional.restrictedRights[user] = rights;
		_additional.kicked.erase(user);
	}
	_doneCallback();
}

void AddParticipantBoxController::kickUser(gsl::not_null<UserData*> user, bool sure) {
	if (!checkInfoLoaded(user, [this, user] { kickUser(user); })) {
		return;
	}

	// Check restrictions.
	auto weak = base::weak_unique_ptr<AddParticipantBoxController>(this);
	if (_additional.adminRights.find(user) != _additional.adminRights.end() || _additional.creator == user) {
		// The user is an admin or creator.
		if (_additional.adminCanEdit.find(user) != _additional.adminCanEdit.end()) {
			if (!sure) {
				Ui::show(Box<ConfirmBox>(lang(lng_sure_ban_admin), [weak, user] { weak->kickUser(user, true); }), KeepOtherLayers);
				return;
			}
		} else {
			Ui::show(Box<InformBox>(lang(lng_error_cant_ban_admin)), KeepOtherLayers);
			return;
		}
	}

	// Finally kick him.
	if (!sure) {
		auto text = lng_sure_ban_user_group(lt_user, App::peerName(user));
		Ui::show(Box<ConfirmBox>(text, [weak, user] { weak->kickUser(user, true); }), KeepOtherLayers);
		return;
	}
	restrictUserSure(user, ChannelData::KickedRestrictedRights());
}

bool AddParticipantBoxController::appendRow(gsl::not_null<UserData*> user) {
	if (delegate()->peerListFindRow(user->id) || user->isSelf()) {
		return false;
	}
	delegate()->peerListAppendRow(createRow(user));
	return true;
}

bool AddParticipantBoxController::prependRow(gsl::not_null<UserData*> user) {
	if (delegate()->peerListFindRow(user->id)) {
		return false;
	}
	delegate()->peerListPrependRow(createRow(user));
	return true;
}

std::unique_ptr<PeerListRow> AddParticipantBoxController::createRow(gsl::not_null<UserData*> user) const {
	auto row = std::make_unique<PeerListRow>(user);
	return std::move(row);
}

template <typename Callback>
void AddParticipantBoxController::HandleParticipant(const MTPChannelParticipant &participant, gsl::not_null<Additional*> additional, Callback callback) {
	switch (participant.type()) {
	case mtpc_channelParticipantAdmin: {
		auto &admin = participant.c_channelParticipantAdmin();
		if (auto user = App::userLoaded(admin.vuser_id.v)) {
			additional->infoNotLoaded.erase(user);
			additional->restrictedRights.erase(user);
			additional->kicked.erase(user);
			additional->adminRights[user] = admin.vadmin_rights;
			if (admin.is_can_edit()) {
				additional->adminCanEdit.emplace(user);
			} else {
				additional->adminCanEdit.erase(user);
			}
			if (auto promoted = App::userLoaded(admin.vpromoted_by.v)) {
				auto it = additional->adminPromotedBy.find(user);
				if (it == additional->adminPromotedBy.end()) {
					additional->adminPromotedBy.emplace(user, promoted);
				} else {
					it->second = promoted;
				}
			} else {
				LOG(("API Error: No user %1 for admin promoted by.").arg(admin.vpromoted_by.v));
			}
			callback(user);
		}
	} break;
	case mtpc_channelParticipantCreator: {
		auto &creator = participant.c_channelParticipantCreator();
		if (auto user = App::userLoaded(creator.vuser_id.v)) {
			additional->infoNotLoaded.erase(user);
			additional->creator = user;
			callback(user);
		}
	} break;
	case mtpc_channelParticipantBanned: {
		auto &banned = participant.c_channelParticipantBanned();
		if (auto user = App::userLoaded(banned.vuser_id.v)) {
			additional->infoNotLoaded.erase(user);
			additional->adminRights.erase(user);
			additional->adminCanEdit.erase(user);
			additional->adminPromotedBy.erase(user);
			if (banned.is_left()) {
				additional->kicked.emplace(user);
			} else {
				additional->kicked.erase(user);
			}
			additional->restrictedRights[user] = banned.vbanned_rights;
			callback(user);
		}
	} break;
	case mtpc_channelParticipant: {
		auto &data = participant.c_channelParticipant();
		if (auto user = App::userLoaded(data.vuser_id.v)) {
			additional->infoNotLoaded.erase(user);
			additional->adminRights.erase(user);
			additional->adminCanEdit.erase(user);
			additional->adminPromotedBy.erase(user);
			additional->restrictedRights.erase(user);
			additional->kicked.erase(user);
			callback(user);
		}
	} break;
	default: Unexpected("Participant type in AddParticipantBoxController::HandleParticipant()");
	}
}

AddParticipantBoxSearchController::AddParticipantBoxSearchController(gsl::not_null<ChannelData*> channel, Role role, gsl::not_null<Additional*> additional)
: _channel(channel)
, _role(role)
, _additional(additional) {
	_timer.setCallback([this] { searchOnServer(); });
}

void AddParticipantBoxSearchController::searchQuery(const QString &query) {
	if (_query != query) {
		_query = query;
		_offset = 0;
		_requestId = 0;
		_allLoaded = false;
		if (!_query.isEmpty() && !searchInCache()) {
			_timer.callOnce(AutoSearchTimeout);
		} else {
			_timer.cancel();
		}
	}
}

bool AddParticipantBoxSearchController::searchInCache() {
	auto it = _cache.find(_query);
	if (it != _cache.cend()) {
		_requestId = 0;
		searchDone(it->second, _requestId);
		return true;
	}
	return false;
}

void AddParticipantBoxSearchController::searchOnServer() {
	Expects(!_query.isEmpty());
	loadMoreRows();
}

void AddParticipantBoxSearchController::searchDone(const MTPchannels_ChannelParticipants &result, mtpRequestId requestId) {
	Expects(result.type() == mtpc_channels_channelParticipants);

	auto &participants = result.c_channels_channelParticipants();
	auto query = _query;
	if (requestId) {
		App::feedUsers(participants.vusers);
		auto it = _queries.find(requestId);
		if (it != _queries.cend()) {
			query = it->second.first; // query
			if (it->second.second == 0) { // offset
				_cache[query] = result;
			}
			_queries.erase(it);
		}
	}

	if (_requestId == requestId) {
		_requestId = 0;
		auto &list = participants.vparticipants.v;
		if (list.isEmpty()) {
			_allLoaded = true;
		} else {
			for_const (auto &participant, list) {
				++_offset;
				AddParticipantBoxController::HandleParticipant(participant, _additional, [this](gsl::not_null<UserData*> user) {
					delegate()->peerListSearchAddRow(user);
				});
			}
		}
		delegate()->peerListSearchRefreshRows();
	}
}

bool AddParticipantBoxSearchController::isLoading() {
	return _timer.isActive() || _requestId;
}

bool AddParticipantBoxSearchController::loadMoreRows() {
	if (_query.isEmpty()) {
		return false;
	}
	if (!_allLoaded && !isLoading()) {
		_requestId = request(MTPchannels_GetParticipants(_channel->inputChannel, MTP_channelParticipantsSearch(MTP_string(_query)), MTP_int(_offset), MTP_int(kBannedPerPage))).done([this](const MTPchannels_ChannelParticipants &result, mtpRequestId requestId) {
			searchDone(result, requestId);
		}).fail([this](const RPCError &error, mtpRequestId requestId) {
			if (_requestId == requestId) {
				_requestId = 0;
				_allLoaded = true;
				delegate()->peerListSearchRefreshRows();
			}
		}).send();
		_queries.emplace(_requestId, std::make_pair(_query, _offset));
	}
	return true;
}

} // namespace

using UpdateFlag = Notify::PeerUpdate::Flag;

SettingsWidget::SettingsWidget(QWidget *parent, PeerData *peer) : BlockWidget(parent, peer, lang(lng_profile_settings_section))
, _enableNotifications(this, lang(lng_profile_enable_notifications), true, st::defaultCheckbox) {
	connect(_enableNotifications, SIGNAL(changed()), this, SLOT(onNotificationsChange()));

	Notify::PeerUpdate::Flags observeEvents = UpdateFlag::NotificationsEnabled;
	if (auto chat = peer->asChat()) {
		if (chat->amCreator()) {
			observeEvents |= UpdateFlag::ChatCanEdit | UpdateFlag::InviteLinkChanged;
		}
	} else if (auto channel = peer->asChannel()) {
		observeEvents |= UpdateFlag::ChannelRightsChanged | UpdateFlag::BannedUsersChanged | UpdateFlag::UsernameChanged | UpdateFlag::InviteLinkChanged;
	}
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		notifyPeerUpdated(update);
	}));

	refreshButtons();
	_enableNotifications->finishAnimations();

	show();
}

void SettingsWidget::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (update.peer != peer()) {
		return;
	}

	if (update.flags & UpdateFlag::NotificationsEnabled) {
		refreshEnableNotifications();
	}
	if (update.flags & (UpdateFlag::ChannelRightsChanged | UpdateFlag::ChatCanEdit | UpdateFlag::UsernameChanged | UpdateFlag::InviteLinkChanged)) {
		refreshInviteLinkButton();
	}
	if (update.flags & (UpdateFlag::ChannelRightsChanged | UpdateFlag::ChatCanEdit)) {
		refreshManageAdminsButton();
	}
	if (update.flags & (UpdateFlag::ChannelRightsChanged | UpdateFlag::BannedUsersChanged)) {
		refreshManageBannedUsersButton();
	}

	contentSizeUpdated();
}

int SettingsWidget::resizeGetHeight(int newWidth) {
	int newHeight = contentTop() + st::profileEnableNotificationsTop;

	_enableNotifications->moveToLeft(st::profileBlockTitlePosition.x(), newHeight);
	newHeight += _enableNotifications->heightNoMargins() + st::profileSettingsBlockSkip;

	auto moveLink = [&newHeight, newWidth](Ui::LeftOutlineButton *button) {
		if (!button) return;

		int left = defaultOutlineButtonLeft();
		int availableWidth = newWidth - left - st::profileBlockMarginRight;
		accumulate_min(availableWidth, st::profileBlockOneLineWidthMax);
		button->resizeToWidth(availableWidth);
		button->moveToLeft(left, newHeight);
		newHeight += button->height();
	};
	moveLink(_manageAdmins);
	moveLink(_manageBannedUsers);
	moveLink(_manageRestrictedUsers);
	moveLink(_inviteLink);

	newHeight += st::profileBlockMarginBottom;
	return newHeight;
}

void SettingsWidget::refreshButtons() {
	refreshEnableNotifications();
	refreshManageAdminsButton();
	refreshManageBannedUsersButton();
	refreshInviteLinkButton();
}

void SettingsWidget::refreshEnableNotifications() {
	if (peer()->notify == UnknownNotifySettings) {
		App::api()->requestNotifySetting(peer());
	} else {
		auto &notifySettings = peer()->notify;
		bool enabled = (notifySettings == EmptyNotifySettings || notifySettings->mute < unixtime());
		_enableNotifications->setChecked(enabled, Ui::Checkbox::NotifyAboutChange::DontNotify);
	}
}

void SettingsWidget::refreshManageAdminsButton() {
	auto hasManageAdmins = [this] {
		if (auto chat = peer()->asChat()) {
			return (chat->amCreator() && chat->canEdit());
		} else if (auto channel = peer()->asMegagroup()) {
			return channel->hasAdminRights() || channel->amCreator();
		}
		return false;
	};
	_manageAdmins.destroy();
	if (hasManageAdmins()) {
		_manageAdmins.create(this, lang(lng_profile_manage_admins), st::defaultLeftOutlineButton);
		_manageAdmins->show();
		connect(_manageAdmins, SIGNAL(clicked()), this, SLOT(onManageAdmins()));
	}
}

void SettingsWidget::refreshManageBannedUsersButton() {
	auto hasManageBannedUsers = [this] {
		if (auto channel = peer()->asMegagroup()) {
			return channel->canBanMembers() && (channel->kickedCount() > 0);
		}
		return false;
	};
	_manageBannedUsers.destroy();
	if (hasManageBannedUsers()) {
		_manageBannedUsers.create(this, lang(lng_profile_manage_blocklist), st::defaultLeftOutlineButton);
		_manageBannedUsers->show();
		connect(_manageBannedUsers, SIGNAL(clicked()), this, SLOT(onManageBannedUsers()));
	}

	auto hasManageRestrictedUsers = [this] {
		if (auto channel = peer()->asMegagroup()) {
			return channel->canBanMembers() && (channel->restrictedCount() > 0);
		}
		return false;
	};
	_manageRestrictedUsers.destroy();
	if (hasManageRestrictedUsers()) {
		_manageRestrictedUsers.create(this, lang(lng_profile_manage_restrictedlist), st::defaultLeftOutlineButton);
		_manageRestrictedUsers->show();
		connect(_manageRestrictedUsers, SIGNAL(clicked()), this, SLOT(onManageRestrictedUsers()));
	}
}

void SettingsWidget::refreshInviteLinkButton() {
	auto getInviteLinkText = [this]() -> QString {
		if (auto chat = peer()->asChat()) {
			if (chat->amCreator() && chat->canEdit()) {
				return lang(chat->inviteLink().isEmpty() ? lng_group_invite_create : lng_group_invite_create_new);
			}
		} else if (auto channel = peer()->asChannel()) {
			if (channel->canHaveInviteLink() && !channel->isPublic()) {
				return lang(channel->inviteLink().isEmpty() ? lng_group_invite_create : lng_group_invite_create_new);
			}
		}
		return QString();
	};
	auto inviteLinkText = getInviteLinkText();
	if (inviteLinkText.isEmpty()) {
		_inviteLink.destroy();
	} else {
		_inviteLink.create(this, inviteLinkText, st::defaultLeftOutlineButton);
		_inviteLink->show();
		connect(_inviteLink, SIGNAL(clicked()), this, SLOT(onInviteLink()));
	}
}

void SettingsWidget::onNotificationsChange() {
	App::main()->updateNotifySetting(peer(), _enableNotifications->checked() ? NotifySettingSetNotify : NotifySettingSetMuted);
}

void SettingsWidget::onManageAdmins() {
	if (auto chat = peer()->asChat()) {
		Ui::show(Box<ContactsBox>(chat, MembersFilter::Admins));
	} else if (auto channel = peer()->asChannel()) {
		ParticipantsBoxController::Start(channel, ParticipantsBoxController::Role::Admins);
	}
}

void SettingsWidget::onManageBannedUsers() {
	if (auto channel = peer()->asMegagroup()) {
		ParticipantsBoxController::Start(channel, ParticipantsBoxController::Role::Kicked);
	}
}

void SettingsWidget::onManageRestrictedUsers() {
	if (auto channel = peer()->asMegagroup()) {
		ParticipantsBoxController::Start(channel, ParticipantsBoxController::Role::Restricted);
	}
}

void SettingsWidget::onInviteLink() {
	auto getInviteLink = [this]() {
		if (auto chat = peer()->asChat()) {
			return chat->inviteLink();
		} else if (auto channel = peer()->asChannel()) {
			return channel->inviteLink();
		}
		return QString();
	};
	auto link = getInviteLink();

	auto text = lang(link.isEmpty() ? lng_group_invite_about : lng_group_invite_about_new);
	Ui::show(Box<ConfirmBox>(text, base::lambda_guarded(this, [this] {
		Ui::hideLayer();
		App::api()->exportInviteLink(peer());
	})));
}

} // namespace Profile
