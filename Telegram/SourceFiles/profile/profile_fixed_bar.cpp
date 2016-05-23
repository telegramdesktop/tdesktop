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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "profile/profile_fixed_bar.h"

#include "styles/style_profile.h"
#include "lang.h"
#include "mainwidget.h"

namespace Profile {

class BackButton final : public Button {
public:
	BackButton(QWidget *parent) : Button(parent) {
		setCursor(style::cur_pointer);
	}

	void resizeToWidth(int newWidth) {
		resize(newWidth, st::profileTopBarHeight);
	}

protected:
	void paintEvent(QPaintEvent *e) {
		Painter p(this);

		p.fillRect(e->rect(), st::profileBg);
		st::profileTopBarBackIcon.paint(p, st::profileTopBarBackIconPosition, width());

		p.setFont(st::profileTopBarBackFont);
		p.setPen(st::profileTopBarBackFg);
		p.drawTextLeft(st::profileTopBarBackPosition.x(), st::profileTopBarBackPosition.y(), width(), lang(lng_menu_back));
	}

private:

};

FixedBar::FixedBar(QWidget *parent, PeerData *peer) : TWidget(parent)
, _peer(peer)
, _peerUser(peer->asUser())
, _peerChat(peer->asChat())
, _peerChannel(peer->asChannel())
, _peerMegagroup(peer->isMegagroup() ? _peerChannel : nullptr)
, _backButton(this) {
	_backButton->moveToLeft(0, 0);
	connect(_backButton, SIGNAL(clicked()), this, SLOT(onBack()));
}

void FixedBar::onBack() {
	App::main()->showBackFromStack();
}

void FixedBar::onEditChannel() {

}

void FixedBar::onEditGroup() {

}

void FixedBar::onLeaveGroup() {

}

void FixedBar::onAddContact() {

}

void FixedBar::onEditContact() {

}

void FixedBar::onDeleteContact() {

}


void FixedBar::resizeToWidth(int newWidth) {
	int newHeight = 0;

	_backButton->resizeToWidth(newWidth);
	newHeight += _backButton->height();

	resize(newWidth, newHeight);
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
		}
		show();
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
