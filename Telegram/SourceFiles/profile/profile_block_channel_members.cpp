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

#include "styles/style_profile.h"
#include "ui/widgets/buttons.h"
#include "boxes/members_box.h"
#include "observer_peer.h"
#include "lang.h"

namespace Profile {

using UpdateFlag = Notify::PeerUpdate::Flag;

ChannelMembersWidget::ChannelMembersWidget(QWidget *parent, PeerData *peer) : BlockWidget(parent, peer, lang(lng_profile_participants_section)) {
	auto observeEvents = UpdateFlag::ChannelCanViewAdmins
		| UpdateFlag::ChannelCanViewMembers
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

	if (update.flags & (UpdateFlag::ChannelCanViewAdmins | UpdateFlag::AdminsChanged)) {
		refreshAdmins();
	}
	if (update.flags & (UpdateFlag::ChannelCanViewMembers | UpdateFlag::MembersChanged)) {
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
	refreshAdmins();
	refreshMembers();

	refreshVisibility();
}

void ChannelMembersWidget::refreshAdmins() {
	auto getAdminsText = [this]() -> QString {
		if (auto channel = peer()->asChannel()) {
			if (!channel->isMegagroup() && channel->canViewAdmins()) {
				int adminsCount = qMax(channel->adminsCount(), 1);
				return lng_channel_admins_link(lt_count, adminsCount);
			}
		}
		return QString();
	};
	addButton(getAdminsText(), &_admins, SLOT(onAdmins()));
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

	resizeButton(_admins);
	resizeButton(_members);

	return newHeight;
}

void ChannelMembersWidget::onAdmins() {
	if (auto channel = peer()->asChannel()) {
		Ui::show(Box<MembersBox>(channel, MembersFilter::Admins));
	}
}

void ChannelMembersWidget::onMembers() {
	if (auto channel = peer()->asChannel()) {
		Ui::show(Box<MembersBox>(channel, MembersFilter::Recent));
	}
}

} // namespace Profile
