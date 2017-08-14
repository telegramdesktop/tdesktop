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

#include "profile/profile_channel_controllers.h"
#include "history/history_admin_log_section.h"
#include "styles/style_profile.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/confirm_box.h"
#include "observer_peer.h"
#include "auth_session.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"

namespace Profile {

using UpdateFlag = Notify::PeerUpdate::Flag;

SettingsWidget::SettingsWidget(QWidget *parent, PeerData *peer) : BlockWidget(parent, peer, lang(lng_profile_settings_section))
, _enableNotifications(this, lang(lng_profile_enable_notifications), true, st::defaultCheckbox) {
	subscribe(_enableNotifications->checkedChanged, [this](bool checked) { onNotificationsChange(); });

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
	moveLink(_recentActions);
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
		Auth().api().requestNotifySetting(peer());
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

	auto hasRecentActions = [this] {
		if (auto channel = peer()->asMegagroup()) {
			return channel->hasAdminRights() || channel->amCreator();
		}
		return false;
	};
	_recentActions.destroy();
	if (hasRecentActions()) {
		_recentActions.create(this, lang(lng_profile_recent_actions), st::defaultLeftOutlineButton);
		_recentActions->show();
		connect(_recentActions, SIGNAL(clicked()), this, SLOT(onRecentActions()));
	}
}

void SettingsWidget::refreshManageBannedUsersButton() {
	auto hasManageBannedUsers = [this] {
		if (auto channel = peer()->asMegagroup()) {
			return channel->canViewBanned() && (channel->kickedCount() > 0);
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
			return channel->canViewBanned() && (channel->restrictedCount() > 0);
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
		EditChatAdminsBoxController::Start(chat);
	} else if (auto channel = peer()->asChannel()) {
		ParticipantsBoxController::Start(channel, ParticipantsBoxController::Role::Admins);
	}
}

void SettingsWidget::onRecentActions() {
	if (auto channel = peer()->asChannel()) {
		if (auto main = App::main()) {
			main->showWideSection(AdminLog::SectionMemento(channel));
		}
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
		Auth().api().exportInviteLink(peer());
	})));
}

} // namespace Profile
