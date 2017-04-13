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
#include "profile/profile_block_invite_link.h"

#include "styles/style_profile.h"
#include "ui/widgets/labels.h"
#include "ui/toast/toast.h"
#include "boxes/confirm_box.h"
#include "observer_peer.h"
#include "mainwindow.h"
#include "lang/lang_keys.h"

namespace Profile {

using UpdateFlag = Notify::PeerUpdate::Flag;

InviteLinkWidget::InviteLinkWidget(QWidget *parent, PeerData *peer) : BlockWidget(parent, peer, lang(lng_profile_invite_link_section)) {
	auto observeEvents = UpdateFlag::InviteLinkChanged | UpdateFlag::UsernameChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		notifyPeerUpdated(update);
	}));

	refreshLink();
	refreshVisibility();
}

void InviteLinkWidget::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (update.peer != peer()) {
		return;
	}

	if (update.flags & (UpdateFlag::InviteLinkChanged | UpdateFlag::UsernameChanged)) {
		refreshLink();
		refreshVisibility();

		contentSizeUpdated();
	}
}

int InviteLinkWidget::resizeGetHeight(int newWidth) {
	int newHeight = contentTop();

	int marginLeft = st::profileBlockTextPart.margin.left();
	int marginRight = st::profileBlockTextPart.margin.right();
	int left = st::profileBlockTitlePosition.x();
	if (_link) {
		int textWidth = _link->naturalWidth();
		int availableWidth = newWidth - left - st::profileBlockMarginRight;
		int maxWidth = st::msgMaxWidth;
		accumulate_min(textWidth, availableWidth);
		accumulate_min(textWidth, st::msgMaxWidth);
		_link->resizeToWidth(textWidth + marginLeft + marginRight);
		_link->moveToLeft(left - marginLeft, newHeight - st::profileBlockTextPart.margin.top());
		newHeight += _link->height();
	}

	newHeight += st::profileBlockMarginBottom;
	return newHeight;
}

void InviteLinkWidget::refreshVisibility() {
	setVisible(_link != nullptr);
}

QString InviteLinkWidget::getInviteLink() const {
	if (auto chat = peer()->asChat()) {
		return chat->inviteLink();
	} else if (auto channel = peer()->asChannel()) {
		return channel->isPublic() ? QString() : channel->inviteLink();
	}
	return QString();
};

void InviteLinkWidget::refreshLink() {
	_link.destroy();
	TextWithEntities linkData = { getInviteLink(), EntitiesInText() };
	if (linkData.text.isEmpty()) {
		_link.destroy();
	} else {
		_link.create(this, QString(), Ui::FlatLabel::InitType::Simple, st::profileInviteLinkText);
		_link->show();

		linkData.entities.push_back(EntityInText(EntityInTextUrl, 0, linkData.text.size()));
		_link->setMarkedText(linkData);
		_link->setSelectable(true);
		_link->setContextCopyText(QString());
		_link->setClickHandlerHook([this](const ClickHandlerPtr &handler, Qt::MouseButton button) {
			auto link = getInviteLink();
			if (link.isEmpty()) {
				return true;
			}

			QApplication::clipboard()->setText(link);
			Ui::Toast::Show(lang(lng_group_invite_copied));
			return false;
		});
	}
}

} // namespace Profile
