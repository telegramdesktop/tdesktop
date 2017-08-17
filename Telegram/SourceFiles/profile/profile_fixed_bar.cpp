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
#include "profile/profile_fixed_bar.h"

#include "styles/style_profile.h"
#include "styles/style_window.h"
#include "ui/widgets/buttons.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "boxes/add_contact_box.h"
#include "boxes/confirm_box.h"
#include "observer_peer.h"
#include "styles/style_boxes.h"
#include "profile/profile_back_button.h"

namespace Profile {
namespace {

using UpdateFlag = Notify::PeerUpdate::Flag;
const auto ButtonsUpdateFlags = UpdateFlag::UserCanShareContact
	| UpdateFlag::UserIsContact
	| UpdateFlag::ChatCanEdit
	| UpdateFlag::ChannelRightsChanged;

} // namespace

FixedBar::FixedBar(QWidget *parent, PeerData *peer) : TWidget(parent)
, _peer(peer)
, _peerUser(peer->asUser())
, _peerChat(peer->asChat())
, _peerChannel(peer->asChannel())
, _peerMegagroup(peer->isMegagroup() ? _peerChannel : nullptr)
, _backButton(this, lang(lng_menu_back)) {
	subscribe(Lang::Current().updated(), [this] { refreshLang(); });

	_backButton->moveToLeft(0, 0);
	connect(_backButton, SIGNAL(clicked()), this, SLOT(onBack()));

	auto observeEvents = ButtonsUpdateFlags
		| UpdateFlag::MigrationChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		notifyPeerUpdate(update);
	}));

	refreshRightActions();
}

void FixedBar::notifyPeerUpdate(const Notify::PeerUpdate &update) {
	if (update.peer != _peer) {
		return;
	}
	if ((update.flags & ButtonsUpdateFlags) != 0) {
		refreshRightActions();
	}
	if (update.flags & UpdateFlag::MigrationChanged) {
		if (_peerChat && _peerChat->migrateTo()) {
			auto channel = _peerChat->migrateTo();
			onBack();
			Ui::showPeerProfile(channel);
		}
	}
}

void FixedBar::refreshRightActions() {
	_currentAction = 0;
	if (_peerUser) {
		setUserActions();
	} else if (_peerChat) {
		setChatActions();
	} else if (_peerMegagroup) {
		setMegagroupActions();
	} else if (_peerChannel) {
		setChannelActions();
	}
	while (_rightActions.size() > _currentAction) {
		delete _rightActions.back().button;
		_rightActions.pop_back();
	}
	resizeToWidth(width());
}

void FixedBar::setUserActions() {
	if (_peerUser->canShareThisContact()) {
		addRightAction(RightActionType::ShareContact, langFactory(lng_profile_top_bar_share_contact), SLOT(onShareContact()));
	}
	if (_peerUser->isContact()) {
		addRightAction(RightActionType::EditContact, langFactory(lng_profile_edit_contact), SLOT(onEditContact()));
		addRightAction(RightActionType::DeleteContact, langFactory(lng_profile_delete_contact), SLOT(onDeleteContact()));
	} else if (_peerUser->canAddContact()) {
		addRightAction(RightActionType::AddContact, langFactory(lng_profile_add_contact), SLOT(onAddContact()));
	}
}

void FixedBar::setChatActions() {
	if (_peerChat->canEdit()) {
		addRightAction(RightActionType::EditGroup, langFactory(lng_profile_edit_contact), SLOT(onEditGroup()));
	}
	addRightAction(RightActionType::LeaveGroup, langFactory(lng_profile_delete_and_exit), SLOT(onLeaveGroup()));
}

void FixedBar::setMegagroupActions() {
	if (_peerMegagroup->canEditInformation()) {
		addRightAction(RightActionType::EditChannel, langFactory(lng_profile_edit_contact), SLOT(onEditChannel()));
	}
}

void FixedBar::setChannelActions() {
	if (_peerChannel->canEditInformation()) {
		addRightAction(RightActionType::EditChannel, langFactory(lng_profile_edit_contact), SLOT(onEditChannel()));
	}
}

void FixedBar::addRightAction(RightActionType type, base::lambda<QString()> textFactory, const char *slot) {
	if (_rightActions.size() > _currentAction) {
		if (_rightActions.at(_currentAction).type == type) {
			++_currentAction;
			return;
		}
	} else {
		Assert(_rightActions.size() == _currentAction);
		_rightActions.push_back(RightAction());
	}
	_rightActions[_currentAction].type = type;
	delete _rightActions[_currentAction].button;
	_rightActions[_currentAction].button = new Ui::RoundButton(this, std::move(textFactory), st::profileFixedBarButton);
	connect(_rightActions[_currentAction].button, SIGNAL(clicked()), this, slot);
	bool showButton = !_animatingMode && (type != RightActionType::ShareContact || !_hideShareContactButton);
	_rightActions[_currentAction].button->setVisible(showButton);
	++_currentAction;
}

void FixedBar::onBack() {
	App::main()->showBackFromStack();
}

void FixedBar::onEditChannel() {
	Ui::show(Box<EditChannelBox>(_peerMegagroup ? _peerMegagroup : _peerChannel));
}

void FixedBar::onEditGroup() {
	Ui::show(Box<EditNameTitleBox>(_peerChat));
}

void FixedBar::onAddContact() {
	auto firstName = _peerUser->firstName;
	auto lastName = _peerUser->lastName;
	auto phone = _peerUser->phone().isEmpty() ? App::phoneFromSharedContact(peerToUser(_peer->id)) : _peerUser->phone();
	Ui::show(Box<AddContactBox>(firstName, lastName, phone));
}

void FixedBar::onEditContact() {
	Ui::show(Box<AddContactBox>(_peerUser));
}

void FixedBar::onShareContact() {
	App::main()->shareContactLayer(_peerUser);
}

void FixedBar::onDeleteContact() {
	auto text = lng_sure_delete_contact(lt_contact, App::peerName(_peerUser));
	Ui::show(Box<ConfirmBox>(text, lang(lng_box_delete), base::lambda_guarded(this, [this] {
		Ui::showChatsList();
		Ui::hideLayer();
		MTP::send(MTPcontacts_DeleteContact(_peerUser->inputUser), App::main()->rpcDone(&MainWidget::deletedContact, _peerUser));
	})));
}

void FixedBar::onLeaveGroup() {
	auto text = lng_sure_delete_and_exit(lt_group, App::peerName(_peerChat));
	Ui::show(Box<ConfirmBox>(text, lang(lng_box_leave), st::attentionBoxButton, base::lambda_guarded(this, [this] {
		Ui::showChatsList();
		Ui::hideLayer();
		App::main()->deleteAndExit(_peerChat);
	})));
}

int FixedBar::resizeGetHeight(int newWidth) {
	int newHeight = 0;

	updateButtonsGeometry(newWidth);

	_backButton->resizeToWidth(newWidth);
	_backButton->moveToLeft(0, 0);
	newHeight += _backButton->height();

	return newHeight;
}

void FixedBar::updateButtonsGeometry(int newWidth) {
	int buttonLeft = newWidth;
	for (auto i = _rightActions.cend(), b = _rightActions.cbegin(); i != b;) {
		--i;
		buttonLeft -= i->button->width();
		i->button->moveToLeft(buttonLeft, 0);
	}
}

void FixedBar::refreshLang() {
	InvokeQueued(this, [this] { updateButtonsGeometry(width()); });
}

void FixedBar::setAnimatingMode(bool enabled) {
	if (_animatingMode != enabled) {
		_animatingMode = enabled;
		setCursor(_animatingMode ? style::cur_pointer : style::cur_default);
		if (_animatingMode) {
			setAttribute(Qt::WA_OpaquePaintEvent, false);
			hideChildren();
		} else {
			setAttribute(Qt::WA_OpaquePaintEvent);
			showChildren();
			if (_hideShareContactButton) {
				applyHideShareContactButton();
			}
		}
		show();
	}
}

void FixedBar::setHideShareContactButton(bool hideButton) {
	_hideShareContactButton = hideButton;
	if (!_animatingMode) {
		applyHideShareContactButton();
	}
}

void FixedBar::applyHideShareContactButton() {
	for_const (auto &action, _rightActions) {
		if (action.type == RightActionType::ShareContact) {
			action.button->setVisible(!_hideShareContactButton);
		}
	}
}

void FixedBar::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		onBack();
	} else {
		TWidget::mousePressEvent(e);
	}
}

} // namespace Profile
