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
#include "ui/buttons/round_button.h"
#include "lang.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "mainwindow.h"

namespace Profile {
namespace {

class OnlineCounter {
public:
	OnlineCounter() : _currentTime(unixtime()), _self(App::self()) {
	}
	void feedUser(UserData *user) {
		if (App::onlineForSort(user, _currentTime) > _currentTime) {
			++_result;
			if (user != _self) {
				_onlyMe = false;
			}
		}
	}
	QString result(int fullCount) const {
		if (_result > 0 && !_onlyMe) {
			return lng_chat_status_members_online(lt_count, fullCount, lt_count_online, _result);
		}
		return lng_chat_status_members(lt_count, fullCount);
	}

private:
	bool _onlyMe = true;
	int _result = 0;
	int _currentTime;
	UserData *_self;

};

} // namespace

class PhotoButton final : public Button {
public:
	PhotoButton(QWidget *parent, PeerData *peer) : Button(parent), _peer(peer) {
		resize(st::profilePhotoSize, st::profilePhotoSize);
	}
	void photoUpdated() {
		bool hasPhoto = (_peer->photoId && _peer->photoId != UnknownPeerPhotoId);
		setCursor(hasPhoto ? style::cur_pointer : style::cur_default);
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
, _photoButton(this, peer) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	_photoButton->photoUpdated();
	connect(_photoButton, SIGNAL(clicked()), this, SLOT(onPhotoShow()));

	_nameText.setText(st::profileNameFont, App::peerName(_peer));
	updateStatusText();

	_primaryButton = new Ui::RoundButton(this, "SEND MESSAGE", st::profilePrimaryButton);
	_secondaryButton = new Ui::RoundButton(this, "SHARE CONTACT", st::profileSecondaryButton);
}

void CoverWidget::onPhotoShow() {
	PhotoData *photo = (_peer->photoId && _peer->photoId != UnknownPeerPhotoId) ? App::photo(_peer->photoId) : nullptr;
	if ((_peer->photoId == UnknownPeerPhotoId) || (_peer->photoId && !photo->date)) {
		App::api()->requestFullPeer(_peer);
	}
	if (photo && photo->date) {
		App::wnd()->showPhoto(photo, _peer);
	}
}

void CoverWidget::onSetPhoto() {

}

void CoverWidget::onAddMember() {

}

void CoverWidget::onSendMessage() {

}

void CoverWidget::onShareContact() {

}

void CoverWidget::onJoin() {

}

void CoverWidget::onViewChannel() {

}


void CoverWidget::resizeToWidth(int newWidth) {
	int newHeight = 0;

	newHeight += st::profileMarginTop;
	_photoButton->moveToLeft(st::profilePhotoLeft, newHeight);

	int infoLeft = _photoButton->x() + _photoButton->width();
	_namePosition = QPoint(infoLeft + st::profileNameLeft, _photoButton->y() + st::profileNameTop);
	_statusPosition = QPoint(infoLeft + st::profileStatusLeft, _photoButton->y() + st::profileStatusTop);

	int buttonLeft = st::profilePhotoLeft + _photoButton->width() + st::profileButtonLeft;
	if (_primaryButton) {
		_primaryButton->moveToLeft(buttonLeft, st::profileButtonTop);
		buttonLeft += _primaryButton->width() + st::profileButtonSkip;
	}
	if (_secondaryButton) {
		_secondaryButton->moveToLeft(buttonLeft, st::profileButtonTop);
	}

	newHeight += st::profilePhotoSize;
	newHeight += st::profileMarginBottom;

	_dividerTop = newHeight;
	newHeight += st::profileDividerFill.height();

	newHeight += st::profileBlocksTop;

	resize(newWidth, newHeight);
	update();
}

void CoverWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), st::profileBg);

	int availWidth = width() - _namePosition.x() - _photoButton->x();
	p.setFont(st::profileNameFont);
	p.setPen(st::profileNameFg);
	_nameText.drawLeftElided(p, _namePosition.x(), _namePosition.y(), availWidth, width());

	p.setFont(st::profileStatusFont);
	p.setPen(st::profileStatusFg);
	p.drawTextLeft(_statusPosition.x(), _statusPosition.y(), width(), _statusText);

	paintDivider(p);
}

void CoverWidget::paintDivider(Painter &p) {
	st::profileDividerLeft.paint(p, QPoint(st::lineWidth, _dividerTop), width());

	int toFillLeft = st::lineWidth + st::profileDividerLeft.width();
	QRect toFill = rtlrect(toFillLeft, _dividerTop, width() - toFillLeft, st::profileDividerFill.height(), width());
	st::profileDividerFill.fill(p, toFill);
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
			OnlineCounter counter;
			auto &participants = _peerChat->participants;
			for (auto i = participants.cbegin(), e = participants.cend(); i != e; ++i) {
				counter.feedUser(i.key());
			}
			_statusText = counter.result(participants.size());
		}
	} else if (isUsingMegagroupOnlineCount()) {
		OnlineCounter counter;
		for_const (auto user, _peerMegagroup->mgInfo->lastParticipants) {
			counter.feedUser(user);
		}
		_statusText = counter.result(_peerMegagroup->count);
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
