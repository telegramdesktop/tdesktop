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
#include "profile/profile_block_channel_members.h"

#include "profile/profile_channel_controllers.h"
#include "styles/style_profile.h"
#include "ui/widgets/buttons.h"
#include "observer_peer.h"
#include "mainwidget.h"
#include "history/history_admin_log_section.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "window/window_controller.h"

namespace Profile {

using UpdateFlag = Notify::PeerUpdate::Flag;

ChannelMembersWidget::ChannelMembersWidget(QWidget *parent, PeerData *peer) : BlockWidget(parent, peer, lang(lng_profile_participants_section)) {
	auto observeEvents = UpdateFlag::ChannelRightsChanged
		| UpdateFlag::AdminsChanged
		| UpdateFlag::MembersChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		notifyPeerUpdated(update);
	}));

	refreshButtons();
}

void ChannelMembersWidget::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (update.peer != peer()) {
		return;
	}

	if (update.flags & (UpdateFlag::ChannelRightsChanged | UpdateFlag::AdminsChanged)) {
		refreshAdmins();
	}
	if (update.flags & (UpdateFlag::ChannelRightsChanged | UpdateFlag::MembersChanged)) {
		refreshMembers();
	}
	refreshVisibility();

	contentSizeUpdated();
}

void ChannelMembersWidget::addButton(const QString &text, object_ptr<Ui::LeftOutlineButton> *button, const char *slot) {
	if (text.isEmpty()) {
		button->destroy();
	} else if (*button) {
		(*button)->setText(text);
	} else {
		button->create(this, text, st::defaultLeftOutlineButton);
		(*button)->show();
		connect(*button, SIGNAL(clicked()), this, slot);
	}
}

void ChannelMembersWidget::refreshButtons() {
	refreshMembers();
	refreshAdmins();

	refreshVisibility();
}

void ChannelMembersWidget::refreshAdmins() {
	auto getAdminsText = [this] {
		if (auto channel = peer()->asChannel()) {
			if (!channel->isMegagroup() && channel->canViewAdmins()) {
				auto adminsCount = qMax(channel->adminsCount(), 1);
				return lng_channel_admins_link(lt_count, adminsCount);
			}
		}
		return QString();
	};
	addButton(getAdminsText(), &_admins, SLOT(onAdmins()));

	auto getRecentActionsText = [this] {
		if (auto channel = peer()->asChannel()) {
			if (!channel->isMegagroup() && (channel->hasAdminRights() || channel->amCreator())) {
				return lang(lng_profile_recent_actions);
			}
		}
		return QString();
	};
	addButton(getRecentActionsText(), &_recentActions, SLOT(onRecentActions()));
}

void ChannelMembersWidget::refreshMembers() {
	auto getMembersText = [this]() -> QString {
		if (auto channel = peer()->asChannel()) {
			if (!channel->isMegagroup() && channel->canViewMembers()) {
				int membersCount = qMax(channel->membersCount(), 1);
				return lng_channel_members_link(lt_count, membersCount);
			}
		}
		return QString();
	};
	addButton(getMembersText(), &_members, SLOT(onMembers()));
}

void ChannelMembersWidget::refreshVisibility() {
	setVisible(_admins || _members);
}

int ChannelMembersWidget::resizeGetHeight(int newWidth) {
	int newHeight = contentTop();

	auto resizeButton = [this, &newHeight, newWidth](object_ptr<Ui::LeftOutlineButton> &button) {
		if (!button) {
			return;
		}

		int left = defaultOutlineButtonLeft();
		int availableWidth = newWidth - left - st::profileBlockMarginRight;
		accumulate_min(availableWidth, st::profileBlockOneLineWidthMax);
		button->resizeToWidth(availableWidth);
		button->moveToLeft(left, newHeight);
		newHeight += button->height();
	};

	resizeButton(_members);
	resizeButton(_admins);
	resizeButton(_recentActions);

	return newHeight;
}

void ChannelMembersWidget::onMembers() {
	if (auto channel = peer()->asChannel()) {
		ParticipantsBoxController::Start(
			App::wnd()->controller(),
			channel,
			ParticipantsBoxController::Role::Members);
	}
}

void ChannelMembersWidget::onAdmins() {
	if (auto channel = peer()->asChannel()) {
		ParticipantsBoxController::Start(
			App::wnd()->controller(),
			channel,
			ParticipantsBoxController::Role::Admins);
	}
}

void ChannelMembersWidget::onRecentActions() {
	if (auto channel = peer()->asChannel()) {
		if (auto main = App::main()) {
			main->showSection(
				AdminLog::SectionMemento(channel),
				Window::SectionShow());
		}
	}
}

} // namespace Profile
