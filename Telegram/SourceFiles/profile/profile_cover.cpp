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
#include "profile/profile_cover.h"

#include "styles/style_profile.h"
#include "lang.h"
#include "apiwrap.h"
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

		st::profileTopBarBackIcon.paint(p, st::profileTopBarBackIconPosition, width());

		p.setFont(st::profileTopBarBackFont);
		p.setPen(st::profileTopBarBackFg);
		p.drawTextLeft(st::profileTopBarBackPosition.x(), st::profileTopBarBackPosition.y(), width(), lang(lng_menu_back));
	}

private:

};

class PhotoButton final : public Button {
public:
	PhotoButton(QWidget *parent, PeerData *peer) : Button(parent), _peer(peer) {
		resize(st::profilePhotoSize, st::profilePhotoSize);
	}

protected:
	void paintEvent(QPaintEvent *e) {
		Painter p(this);

		_peer->paintUserpic(p, st::profilePhotoSize, 0, 0);
	}

private:
	PeerData *_peer;

};

CoverWidget::CoverWidget(QWidget *parent, PeerData *peer) : TWidget(parent)
, _peer(peer)
, _peerUser(peer->asUser())
, _peerChat(peer->asChat())
, _peerChannel(peer->asChannel())
, _peerMegagroup(peer->isMegagroup() ? _peerChannel : nullptr)
, _backButton(this)
, _photoButton(this, peer) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	_backButton->moveToLeft(0, 0);
	connect(_backButton, SIGNAL(clicked()), this, SLOT(onBack()));

	_nameText.setText(st::profileNameFont, App::peerName(_peer));
	updateStatusText();
}

void CoverWidget::onBack() {
	App::main()->showBackFromStack();
}

void CoverWidget::resizeToWidth(int newWidth) {
	int newHeight = 0;

	// Top bar
	_backButton->resizeToWidth(newWidth);
	newHeight += _backButton->height();

	// Cover content
	newHeight += st::profileMarginTop;
	_photoButton->moveToLeft(st::profilePhotoLeft, newHeight);

	int infoLeft = _photoButton->x() + _photoButton->width();
	_namePosition = QPoint(infoLeft + st::profileNameLeft, _photoButton->y() + st::profileNameTop);
	_statusPosition = QPoint(infoLeft + st::profileStatusLeft, _photoButton->y() + st::profileStatusTop);

	newHeight += st::profilePhotoSize;
	newHeight += st::profileMarginBottom;
	newHeight += st::profileSeparator;

	newHeight += st::profileBlocksTop;

	resize(newWidth, newHeight);
	update();
}

void CoverWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), st::white);

	int availWidth = width() - _namePosition.x() - _photoButton->x();
	p.setFont(st::profileNameFont);
	p.setPen(st::profileNameFg);
	_nameText.drawLeftElided(p, _namePosition.x(), _namePosition.y(), availWidth, width());

	p.setFont(st::profileStatusFont);
	p.setPen(st::profileStatusFg);
	p.drawTextLeft(_statusPosition.x(), _statusPosition.y(), width(), _statusText);
}

void CoverWidget::updateStatusText() {
	int currentTime = unixtime();
	if (_peerUser) {
		_statusText = App::onlineText(_peerUser, currentTime, true);
	} else if (_peerChat && _peerChat->amIn()) {
		if (_peerChat->noParticipantInfo()) {
			App::api()->requestFullPeer(_peer);
			if (_statusText.isEmpty()) {
				_statusText = lng_chat_status_members(lt_count, _peerChat->count);
			}
		} else {
			int onlineCount = 0;
			bool onlyMe = true;
			for (auto i = _peerChat->participants.cbegin(), e = _peerChat->participants.cend(); i != e; ++i) {
				auto onlineTill = App::onlineForSort(i.key(), currentTime);
				if (onlineTill > currentTime) {
					++onlineCount;
				}
			}
			if (onlineCount && !onlyMe) {
				_statusText = lng_chat_status_members_online(lt_count, _peerChat->participants.size(), lt_count_online, onlineCount);
			} else {
				_statusText = lng_chat_status_members(lt_count, _peerChat->participants.size());
			}
		}
	} else if (isUsingMegagroupOnlineCount()) {
		int onlineCount = 0;
		bool onlyMe = true;
		for_const (auto &user, _peerMegagroup->mgInfo->lastParticipants) {
			auto onlineTill = App::onlineForSort(user, currentTime);
			if (onlineTill > currentTime) {
				++onlineCount;
			}
		}
		if (onlineCount && !onlyMe) {
			_statusText = lng_chat_status_members_online(lt_count, _peerMegagroup->count, lt_count_online, onlineCount);
		} else {
			_statusText = lng_chat_status_members(lt_count, _peerMegagroup->count);
		}
	} else if (_peerChannel) {
		if (_peerChannel->count > 0) {
			_statusText = lng_chat_status_members(lt_count, _peerChannel->count);
		} else {
			_statusText = lang(_peerChannel->isMegagroup() ? lng_group_status : lng_channel_status);
		}
	} else {
		_statusText = lang(lng_chat_status_unaccessible);
	}
}

bool CoverWidget::isUsingMegagroupOnlineCount() const {
	if (!_peerMegagroup || !_peerMegagroup->amIn()) {
		return false;
	}

	if (_peerMegagroup->count <= 0 || _peerMegagroup->count > Global::ChatSizeMax()) {
		return false;
	}

	if (_peerMegagroup->mgInfo->lastParticipants.isEmpty() || _peerMegagroup->lastParticipantsCountOutdated()) {
		App::api()->requestLastParticipants(_peerMegagroup);
		return false;
	}

	return true;
}

} // namespace Profile
