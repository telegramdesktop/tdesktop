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

#include "mainwindow.h" // tmp

namespace Profile {
namespace {

constexpr auto kBlockedPerPage = 40;

class BlockedBoxSearchController : public PeerListSearchController, private MTP::Sender {
public:
	BlockedBoxSearchController(gsl::not_null<ChannelData*> channel, bool restricted, gsl::not_null<std::map<UserData*, MTPChannelBannedRights>*> rights);

	void searchQuery(const QString &query) override;
	bool isLoading() override;

private:
	bool searchInCache();
	void searchOnServer();
	void searchDone(const MTPchannels_ChannelParticipants &result, mtpRequestId requestId);

	gsl::not_null<ChannelData*> _channel;
	bool _restricted = false;
	gsl::not_null<std::map<UserData*, MTPChannelBannedRights>*> _rights;

	base::Timer _timer;
	QString _query;
	mtpRequestId _requestId = 0;
	int _offset = 0;
	bool _allLoaded = false;
	std::map<QString, MTPchannels_ChannelParticipants> _cache;
	std::map<mtpRequestId, QString> _queries;

};

BlockedBoxSearchController::BlockedBoxSearchController(gsl::not_null<ChannelData*> channel, bool restricted, gsl::not_null<std::map<UserData*, MTPChannelBannedRights>*> rights)
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
	auto filter = _restricted ? MTP_channelParticipantsBanned(MTP_string(_query)) : MTP_channelParticipantsKicked(MTP_string(_query));
	_requestId = request(MTPchannels_GetParticipants(_channel->inputChannel, filter , MTP_int(_offset), MTP_int(kBlockedPerPage))).done([this](const MTPchannels_ChannelParticipants &result, mtpRequestId requestId) {
		searchDone(result, requestId);
	}).fail([this](const RPCError &error, mtpRequestId requestId) {
		if (_requestId == requestId) {
			_requestId = 0;
			_allLoaded = true;
			delegate()->peerListSearchRefreshRows();
		}
	}).send();
	_queries.emplace(_requestId, _query);
}

void BlockedBoxSearchController::searchDone(const MTPchannels_ChannelParticipants &result, mtpRequestId requestId) {
	Expects(result.type() == mtpc_channels_channelParticipants);

	auto &participants = result.c_channels_channelParticipants();
	auto query = _query;
	if (requestId) {
		App::feedUsers(participants.vusers);
		auto it = _queries.find(requestId);
		if (it != _queries.cend()) {
			query = it->second;
			_cache[query] = result;
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
					LOG(("API Error: Non kicked participant got while requesting for kicked participants: %1").arg(participant.type()));
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

class BlockedBoxController : public PeerListController, private base::Subscriber, private MTP::Sender, public base::enable_weak_from_this {
public:
	BlockedBoxController(gsl::not_null<ChannelData*> channel, bool restricted);

	void prepare() override;
	void rowClicked(gsl::not_null<PeerListRow*> row) override;
	void rowActionClicked(gsl::not_null<PeerListRow*> row) override;
	void preloadRows() override;
	bool searchInLocal() override {
		return false;
	}

	void peerListSearchAddRow(gsl::not_null<PeerData*> peer) override;

private:
	bool appendRow(UserData *user);
	bool prependRow(UserData *user);
	std::unique_ptr<PeerListRow> createRow(UserData *user) const;

	gsl::not_null<ChannelData*> _channel;
	bool _restricted = false;
	int _offset = 0;
	mtpRequestId _loadRequestId = 0;
	bool _allLoaded = false;
	std::map<UserData*, MTPChannelBannedRights> _rights;
	QPointer<EditRestrictedBox> _editBox;

};

void BlockedBoxController::peerListSearchAddRow(gsl::not_null<PeerData*> peer) {
	PeerListController::peerListSearchAddRow(peer);
	if (_restricted && delegate()->peerListFullRowsCount() > 0) {
		setDescriptionText(QString());
	}
}

BlockedBoxController::BlockedBoxController(gsl::not_null<ChannelData*> channel, bool restricted) : PeerListController(std::make_unique<BlockedBoxSearchController>(channel, restricted, &_rights))
, _channel(channel)
, _restricted(restricted) {
}

void BlockedBoxController::prepare() {
	delegate()->peerListSetSearchMode(PeerListSearchMode::Complex);
	delegate()->peerListSetTitle(langFactory(_restricted ? lng_restricted_list_title : lng_blocked_list_title));
	setDescriptionText(lang(lng_contacts_loading));
	setSearchNoResultsText(lang(lng_blocked_list_not_found));
	delegate()->peerListRefreshRows();

	preloadRows();
}

void BlockedBoxController::preloadRows() {
	if (_loadRequestId || _allLoaded) {
		return;
	}

	auto filter = _restricted ? MTP_channelParticipantsBanned(MTP_string(QString())) : MTP_channelParticipantsKicked(MTP_string(QString()));
	_loadRequestId = request(MTPchannels_GetParticipants(_channel->inputChannel, filter, MTP_int(_offset), MTP_int(kBlockedPerPage))).done([this](const MTPchannels_ChannelParticipants &result) {
		Expects(result.type() == mtpc_channels_channelParticipants);

		_loadRequestId = 0;

		if (!_offset) {
			setDescriptionText(_restricted ? QString() : lang(lng_group_blocked_list_about));
		}
		auto &participants = result.c_channels_channelParticipants();
		App::feedUsers(participants.vusers);

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
						appendRow(user);
						_rights[user] = banned.vbanned_rights;
					}
				} else {
					LOG(("API Error: Non kicked participant got while requesting for kicked participants: %1").arg(participant.type()));
					continue;
				}
			}
		}
		delegate()->peerListRefreshRows();
	}).fail([this](const RPCError &error) {
		_loadRequestId = 0;
	}).send();
}

void BlockedBoxController::rowClicked(gsl::not_null<PeerListRow*> row) {
	Ui::showPeerHistoryAsync(row->peer()->id, ShowAtUnreadMsgId);
}

void BlockedBoxController::rowActionClicked(gsl::not_null<PeerListRow*> row) {
	auto user = row->peer()->asUser();
	Expects(user != nullptr);

	if (_restricted) {
		auto it = _rights.find(user);
		t_assert(it != _rights.cend());
		auto weak = base::weak_unique_ptr<BlockedBoxController>(this);
		_editBox = Ui::show(Box<EditRestrictedBox>(_channel, user, it->second, [megagroup = _channel.get(), user, weak](const MTPChannelBannedRights &rights) {
			MTP::send(MTPchannels_EditBanned(megagroup->inputChannel, user->inputUser, rights), rpcDone([megagroup, user, weak, rights](const MTPUpdates &result) {
				if (App::main()) App::main()->sentUpdatesReceived(result);
				megagroup->applyEditBanned(user, rights);
				if (weak) {
					weak->_editBox->closeBox();
					if (rights.c_channelBannedRights().vflags.v == 0 || rights.c_channelBannedRights().is_view_messages()) {
						if (auto row = weak->delegate()->peerListFindRow(user->id)) {
							weak->delegate()->peerListRemoveRow(row);
							weak->delegate()->peerListRefreshRows();
							if (!weak->delegate()->peerListFullRowsCount()) {
								weak->setDescriptionText(lang(lng_blocked_list_not_found));
							}
						}
					} else {
						weak->_rights[user] = rights;
					}
				}
			}));
		}), KeepOtherLayers);
	} else {
		delegate()->peerListRemoveRow(row);
		delegate()->peerListRefreshRows();

		AuthSession::Current().api().unblockParticipant(_channel, user);
	}
}

bool BlockedBoxController::appendRow(UserData *user) {
	if (delegate()->peerListFindRow(user->id)) {
		return false;
	}
	delegate()->peerListAppendRow(createRow(user));
	if (_restricted) {
		setDescriptionText(QString());
	}
	return true;
}

bool BlockedBoxController::prependRow(UserData *user) {
	if (delegate()->peerListFindRow(user->id)) {
		return false;
	}
	delegate()->peerListPrependRow(createRow(user));
	if (_restricted) {
		setDescriptionText(QString());
	}
	return true;
}

std::unique_ptr<PeerListRow> BlockedBoxController::createRow(UserData *user) const {
	auto row = std::make_unique<PeerListRowWithLink>(user);
	row->setActionLink(lang(_restricted ? lng_profile_edit_permissions : lng_blocked_list_unblock));
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
		Ui::show(Box<MembersBox>(channel, MembersFilter::Admins));
	}
}

void SettingsWidget::onManageBlockedUsers() {
	if (auto channel = peer()->asMegagroup()) {
		Ui::show(Box<PeerListBox>(std::make_unique<BlockedBoxController>(channel, false), [](PeerListBox *box) {
			box->addButton(langFactory(lng_close), [box] { box->closeBox(); });
		}));
	}
}

void SettingsWidget::onManageRestrictedUsers() {
	if (auto channel = peer()->asMegagroup()) {
		Ui::show(Box<PeerListBox>(std::make_unique<BlockedBoxController>(channel, true), [](PeerListBox *box) {
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
