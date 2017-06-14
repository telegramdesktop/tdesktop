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

constexpr auto kBlockedPerPage = 40;
constexpr auto kAdminsPerPage = 200;

class BlockedBoxSearchController : public PeerListSearchController, private MTP::Sender {
public:
	BlockedBoxSearchController(gsl::not_null<ChannelData*> channel, bool restricted, gsl::not_null<std::map<gsl::not_null<UserData*>, MTPChannelBannedRights>*> rights);

	void searchQuery(const QString &query) override;
	bool isLoading() override;
	bool loadMoreRows() override;

private:
	bool searchInCache();
	void searchOnServer();
	void searchDone(const MTPchannels_ChannelParticipants &result, mtpRequestId requestId);

	gsl::not_null<ChannelData*> _channel;
	bool _restricted = false;
	gsl::not_null<std::map<gsl::not_null<UserData*>, MTPChannelBannedRights>*> _rights;

	base::Timer _timer;
	QString _query;
	mtpRequestId _requestId = 0;
	int _offset = 0;
	bool _allLoaded = false;
	std::map<QString, MTPchannels_ChannelParticipants> _cache;
	std::map<mtpRequestId, std::pair<QString, int>> _queries; // query, offset

};

class ParticipantsBoxController : public PeerListController, private base::Subscriber, private MTP::Sender, public base::enable_weak_from_this {
public:
	enum class Role {
		Admins,
		Restricted,
		Kicked,
	};

	ParticipantsBoxController(gsl::not_null<ChannelData*> channel, Role role);

	void prepare() override;
	void rowClicked(gsl::not_null<PeerListRow*> row) override;
	void rowActionClicked(gsl::not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	void peerListSearchAddRow(gsl::not_null<PeerData*> peer) override;

private:
	void editAdmin(gsl::not_null<UserData*> user);
	void editRestricted(gsl::not_null<UserData*> user);
	void removeKicked(gsl::not_null<PeerListRow*> row, gsl::not_null<UserData*> user);
	bool appendRow(gsl::not_null<UserData*> user);
	bool prependRow(gsl::not_null<UserData*> user);
	std::unique_ptr<PeerListRow> createRow(gsl::not_null<UserData*> user) const;

	gsl::not_null<ChannelData*> _channel;
	Role _role = Role::Admins;
	int _offset = 0;
	mtpRequestId _loadRequestId = 0;
	bool _allLoaded = false;
	std::map<gsl::not_null<UserData*>, MTPChannelAdminRights> _adminRights;
	std::map<gsl::not_null<UserData*>, bool> _adminCanEdit;
	std::map<gsl::not_null<UserData*>, gsl::not_null<UserData*>> _adminPromotedBy;
	std::map<gsl::not_null<UserData*>, MTPChannelBannedRights> _restrictedRights;
	QPointer<BoxContent> _editBox;

};

BlockedBoxSearchController::BlockedBoxSearchController(gsl::not_null<ChannelData*> channel, bool restricted, gsl::not_null<std::map<gsl::not_null<UserData*>, MTPChannelBannedRights>*> rights)
: _channel(channel)
, _restricted(restricted)
, _rights(rights) {
	_timer.setCallback([this] { searchOnServer(); });
}

void BlockedBoxSearchController::searchQuery(const QString &query) {
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

bool BlockedBoxSearchController::searchInCache() {
	auto it = _cache.find(_query);
	if (it != _cache.cend()) {
		_requestId = 0;
		searchDone(it->second, _requestId);
		return true;
	}
	return false;
}

void BlockedBoxSearchController::searchOnServer() {
	Expects(!_query.isEmpty());
	loadMoreRows();
}

void BlockedBoxSearchController::searchDone(const MTPchannels_ChannelParticipants &result, mtpRequestId requestId) {
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
				if (participant.type() == mtpc_channelParticipantBanned) {
					auto &banned = participant.c_channelParticipantBanned();
					auto userId = banned.vuser_id.v;
					if (auto user = App::userLoaded(userId)) {
						delegate()->peerListSearchAddRow(user);
						(*_rights)[user] = banned.vbanned_rights;
					}
				} else {
					LOG(("API Error: Non banned participant got while requesting for kicked participants: %1").arg(participant.type()));
					continue;
				}
			}
		}
		delegate()->peerListSearchRefreshRows();
	}
}

bool BlockedBoxSearchController::isLoading() {
	return _timer.isActive() || _requestId;
}

bool BlockedBoxSearchController::loadMoreRows() {
	if (_query.isEmpty()) {
		return false;
	}
	if (!_allLoaded && !isLoading()) {
		auto filter = _restricted ? MTP_channelParticipantsBanned(MTP_string(_query)) : MTP_channelParticipantsKicked(MTP_string(_query));
		_requestId = request(MTPchannels_GetParticipants(_channel->inputChannel, filter, MTP_int(_offset), MTP_int(kBlockedPerPage))).done([this](const MTPchannels_ChannelParticipants &result, mtpRequestId requestId) {
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

ParticipantsBoxController::ParticipantsBoxController(gsl::not_null<ChannelData*> channel, Role role) : PeerListController((role == Role::Admins) ? nullptr : std::make_unique<BlockedBoxSearchController>(channel, (role == Role::Restricted), &_restrictedRights))
, _channel(channel)
, _role(role) {
}

void ParticipantsBoxController::peerListSearchAddRow(gsl::not_null<PeerData*> peer) {
	Expects(_role != Role::Admins);
	PeerListController::peerListSearchAddRow(peer);
	if (_role == Role::Restricted && delegate()->peerListFullRowsCount() > 0) {
		setDescriptionText(QString());
	}
}

void ParticipantsBoxController::prepare() {
	if (_role == Role::Admins) {
		delegate()->peerListSetSearchMode(PeerListSearchMode::Local);
		delegate()->peerListSetTitle(langFactory(lng_channel_admins));
	} else {
		delegate()->peerListSetSearchMode(PeerListSearchMode::Complex);
		if (_role == Role::Restricted) {
			delegate()->peerListSetTitle(langFactory(lng_restricted_list_title));
		} else {
			delegate()->peerListSetTitle(langFactory(lng_blocked_list_title));
		}
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
	auto perPage = (_role == Role::Admins) ? kAdminsPerPage : kBlockedPerPage;
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
				if (_role == Role::Admins && participant.type() == mtpc_channelParticipantAdmin) {
					auto &admin = participant.c_channelParticipantAdmin();
					if (auto user = App::userLoaded(admin.vuser_id.v)) {
						_adminRights.emplace(user, admin.vadmin_rights);
						if (admin.is_can_edit()) {
							_adminCanEdit.emplace(user, true);
						} else {
							_adminCanEdit.erase(user);
						}
						if (auto promoted = App::userLoaded(admin.vpromoted_by.v)) {
							_adminPromotedBy.emplace(user, promoted);
						} else {
							LOG(("API Error: No user %1 for admin promoted by.").arg(admin.vpromoted_by.v));
						}
						appendRow(user);
					}
				} else if (_role == Role::Admins && participant.type() == mtpc_channelParticipantCreator) {
					auto &creator = participant.c_channelParticipantCreator();
					if (auto user = App::userLoaded(creator.vuser_id.v)) {
						_adminCanEdit.erase(user);
						appendRow(user);
					}
				} else if ((_role == Role::Restricted || _role == Role::Kicked) && participant.type() == mtpc_channelParticipantBanned) {
					auto &banned = participant.c_channelParticipantBanned();
					if (auto user = App::userLoaded(banned.vuser_id.v)) {
						_restrictedRights.emplace(user, banned.vbanned_rights);
						appendRow(user);
					}
				} else {
					LOG(("API Error: Bad participant type got while requesting for participants: %1").arg(participant.type()));
					continue;
				}
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
	if (_adminCanEdit.find(user) == _adminCanEdit.end()) {
		return;
	}

	auto it = _adminRights.find(user);
	t_assert(it != _adminRights.cend());
	auto weak = base::weak_unique_ptr<ParticipantsBoxController>(this);
	_editBox = Ui::show(Box<EditAdminBox>(_channel, user, it->second, [megagroup = _channel.get(), user, weak](const MTPChannelAdminRights &rights) {
		MTP::send(MTPchannels_EditAdmin(megagroup->inputChannel, user->inputUser, rights), rpcDone([megagroup, user, weak, rights](const MTPUpdates &result) {
			if (App::main()) App::main()->sentUpdatesReceived(result);
			megagroup->applyEditAdmin(user, rights);
			if (weak) {
				weak->_editBox->closeBox();
				if (rights.c_channelAdminRights().vflags.v == 0) {
					if (auto row = weak->delegate()->peerListFindRow(user->id)) {
						weak->delegate()->peerListRemoveRow(row);
						if (!weak->delegate()->peerListFullRowsCount()) {
							weak->setDescriptionText(lang(lng_blocked_list_not_found));
						}
						weak->delegate()->peerListRefreshRows();
					}
				} else {
					weak->_adminRights[user] = rights;
				}
			}
		}));
	}), KeepOtherLayers);
}

void ParticipantsBoxController::editRestricted(gsl::not_null<UserData*> user) {
	auto it = _restrictedRights.find(user);
	t_assert(it != _restrictedRights.cend());
	auto weak = base::weak_unique_ptr<ParticipantsBoxController>(this);
	_editBox = Ui::show(Box<EditRestrictedBox>(_channel, user, it->second, [megagroup = _channel.get(), user, weak](const MTPChannelBannedRights &rights) {
		MTP::send(MTPchannels_EditBanned(megagroup->inputChannel, user->inputUser, rights), rpcDone([megagroup, user, weak, rights](const MTPUpdates &result) {
			if (App::main()) App::main()->sentUpdatesReceived(result);
			megagroup->applyEditBanned(user, rights);
			if (weak) {
				weak->_editBox->closeBox();
				if (rights.c_channelBannedRights().vflags.v == 0 || rights.c_channelBannedRights().is_view_messages()) {
					if (auto row = weak->delegate()->peerListFindRow(user->id)) {
						weak->delegate()->peerListRemoveRow(row);
						if (!weak->delegate()->peerListFullRowsCount()) {
							weak->setDescriptionText(lang(lng_blocked_list_not_found));
						}
						weak->delegate()->peerListRefreshRows();
					}
				} else {
					weak->_restrictedRights[user] = rights;
				}
			}
		}));
	}), KeepOtherLayers);
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
		auto promotedBy = _adminPromotedBy.find(user);
		if (promotedBy == _adminPromotedBy.end()) {
			row->setCustomStatus(lang(lng_channel_admin_status_creator));
		} else {
			row->setCustomStatus(lng_channel_admin_status_promoted_by(lt_user, App::peerName(promotedBy->second)));
		}
	}
	if (_role == Role::Restricted || (_role == Role::Admins && _adminCanEdit.find(user) != _adminCanEdit.end())) {
		row->setActionLink(lang(lng_profile_edit_permissions));
	} else if (_role == Role::Kicked) {
		row->setActionLink(lang(lng_blocked_list_unblock));
	}
	return std::move(row);
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
		observeEvents |= UpdateFlag::ChannelRightsChanged | UpdateFlag::BlockedUsersChanged | UpdateFlag::UsernameChanged | UpdateFlag::InviteLinkChanged;
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
	if (update.flags & (UpdateFlag::ChannelRightsChanged | UpdateFlag::BlockedUsersChanged)) {
		refreshManageBlockedUsersButton();
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
	moveLink(_manageBlockedUsers);
	moveLink(_manageRestrictedUsers);
	moveLink(_inviteLink);

	newHeight += st::profileBlockMarginBottom;
	return newHeight;
}

void SettingsWidget::refreshButtons() {
	refreshEnableNotifications();
	refreshManageAdminsButton();
	refreshManageBlockedUsersButton();
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

void SettingsWidget::refreshManageBlockedUsersButton() {
	auto hasManageBlockedUsers = [this] {
		if (auto channel = peer()->asMegagroup()) {
			return channel->canBanMembers() && (channel->kickedCount() > 0);
		}
		return false;
	};
	_manageBlockedUsers.destroy();
	if (hasManageBlockedUsers()) {
		_manageBlockedUsers.create(this, lang(lng_profile_manage_blocklist), st::defaultLeftOutlineButton);
		_manageBlockedUsers->show();
		connect(_manageBlockedUsers, SIGNAL(clicked()), this, SLOT(onManageBlockedUsers()));
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
		Ui::show(Box<PeerListBox>(std::make_unique<ParticipantsBoxController>(channel, ParticipantsBoxController::Role::Admins), [](PeerListBox *box) {
			box->addButton(langFactory(lng_close), [box] { box->closeBox(); });
		}));
	}
}

void SettingsWidget::onManageBlockedUsers() {
	if (auto channel = peer()->asMegagroup()) {
		Ui::show(Box<PeerListBox>(std::make_unique<ParticipantsBoxController>(channel, ParticipantsBoxController::Role::Kicked), [](PeerListBox *box) {
			box->addButton(langFactory(lng_close), [box] { box->closeBox(); });
		}));
	}
}

void SettingsWidget::onManageRestrictedUsers() {
	if (auto channel = peer()->asMegagroup()) {
		Ui::show(Box<PeerListBox>(std::make_unique<ParticipantsBoxController>(channel, ParticipantsBoxController::Role::Restricted), [](PeerListBox *box) {
			box->addButton(langFactory(lng_close), [box] { box->closeBox(); });
		}));
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
