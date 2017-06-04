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
#include "observer_peer.h"
#include "auth_session.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"

#include "mainwindow.h" // tmp

namespace Profile {
namespace {

constexpr auto kBlockedPerPage = 40;

class BlockedBoxController : public PeerListBox::Controller, private base::Subscriber, private MTP::Sender {
public:
	BlockedBoxController(ChannelData *channel) : _channel(channel) {
	}

	void prepare() override;
	void rowClicked(PeerListBox::Row *row) override;
	void rowActionClicked(PeerListBox::Row *row) override;
	void preloadRows() override;

private:
	bool appendRow(UserData *user);
	bool prependRow(UserData *user);
	std::unique_ptr<PeerListBox::Row> createRow(UserData *user) const;

	ChannelData *_channel = nullptr;
	int _offset = 0;
	mtpRequestId _loadRequestId = 0;
	bool _allLoaded = false;

};

void BlockedBoxController::prepare() {
	view()->setTitle(langFactory(lng_blocked_list_title));
	view()->addButton(langFactory(lng_close), [this] { view()->closeBox(); });
	view()->setAboutText(lang(lng_contacts_loading));
	view()->refreshRows();

	preloadRows();
}

void BlockedBoxController::preloadRows() {
	if (_loadRequestId || _allLoaded) {
		return;
	}

	_loadRequestId = request(MTPchannels_GetParticipants(_channel->inputChannel, MTP_channelParticipantsKicked(MTP_string("")), MTP_int(_offset), MTP_int(kBlockedPerPage))).done([this](const MTPchannels_ChannelParticipants &result) {
		Expects(result.type() == mtpc_channels_channelParticipants);

		_loadRequestId = 0;

		if (!_offset) {
			view()->setAboutText(lang(lng_group_blocked_list_about));
		}
		auto &participants = result.c_channels_channelParticipants();
		App::feedUsers(participants.vusers);

		auto &list = participants.vparticipants.v;
		if (list.isEmpty()) {
			_allLoaded = true;
		} else {
			for_const (auto &participant, list) {
				++_offset;
				if (participant.type() != mtpc_channelParticipantBanned) {
					LOG(("API Error: Non banned participant got while requesting for kicked participants: %1").arg(participant.type()));
					continue;
				}
				auto &kicked = participant.c_channelParticipantBanned();
				if (!kicked.is_left()) {
					LOG(("API Error: Non left participant got while requesting for kicked participants."));
					continue;
				}
				auto userId = kicked.vuser_id.v;
				if (auto user = App::userLoaded(userId)) {
					appendRow(user);
				}
			}
		}
		view()->refreshRows();
	}).fail([this](const RPCError &error) {
		_loadRequestId = 0;
	}).send();
}

void BlockedBoxController::rowClicked(PeerListBox::Row *row) {
	Ui::showPeerHistoryAsync(row->peer()->id, ShowAtUnreadMsgId);
}

void BlockedBoxController::rowActionClicked(PeerListBox::Row *row) {
	auto user = row->peer()->asUser();
	Expects(user != nullptr);

	view()->removeRow(row);
	view()->refreshRows();

	AuthSession::Current().api().unblockParticipant(_channel, user);
}

bool BlockedBoxController::appendRow(UserData *user) {
	if (view()->findRow(user->id)) {
		return false;
	}
	view()->appendRow(createRow(user));
	return true;
}

bool BlockedBoxController::prependRow(UserData *user) {
	if (view()->findRow(user->id)) {
		return false;
	}
	view()->prependRow(createRow(user));
	return true;
}

std::unique_ptr<PeerListBox::Row> BlockedBoxController::createRow(UserData *user) const {
	auto row = std::make_unique<PeerListRowWithLink>(user);
	row->setActionLink(lang(lng_blocked_list_unblock));
	auto status = [user]() -> QString {
		if (user->botInfo) {
			return lang(lng_status_bot);
		} else if (user->phone().isEmpty()) {
			return lang(lng_blocked_list_unknown_phone);
		}
		return App::formatPhone(user->phone());
	};
	row->setCustomStatus(status());
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
		if (channel->amCreator()) {
			observeEvents |= UpdateFlag::UsernameChanged | UpdateFlag::InviteLinkChanged;
		}
		observeEvents |= UpdateFlag::ChannelAmEditor | UpdateFlag::BlockedUsersChanged;
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
	if (update.flags & (UpdateFlag::ChatCanEdit | UpdateFlag::UsernameChanged | UpdateFlag::InviteLinkChanged)) {
		refreshInviteLinkButton();
	}
	if (update.flags & (UpdateFlag::ChatCanEdit)) {
		refreshManageAdminsButton();
	}
	if ((update.flags & UpdateFlag::ChannelAmEditor) || (update.flags & UpdateFlag::BlockedUsersChanged)) {
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
			return channel->amCreator();
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
}

void SettingsWidget::refreshInviteLinkButton() {
	auto getInviteLinkText = [this]() -> QString {
		if (auto chat = peer()->asChat()) {
			if (chat->amCreator() && chat->canEdit()) {
				return lang(chat->inviteLink().isEmpty() ? lng_group_invite_create : lng_group_invite_create_new);
			}
		} else if (auto channel = peer()->asChannel()) {
			if (channel->amCreator() && !channel->isPublic()) {
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
		Ui::show(Box<PeerListBox>(std::make_unique<BlockedBoxController>(channel)));
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
