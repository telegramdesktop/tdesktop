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
#include "profile/profile_userpic_button.h"

#include "styles/style_profile.h"
#include "observer_peer.h"
#include "auth_session.h"

namespace Profile {

UserpicButton::UserpicButton(QWidget *parent, PeerData *peer, int size) : AbstractButton(parent)
, _size(size ? size : st::profilePhotoSize)
, _peer(peer) {
	resize(_size, _size);

	processPeerPhoto();
	_notShownYet = _waiting;
	if (!_waiting) {
		_userpic = prepareUserpicPixmap();
	}

	auto observeEvents = Notify::PeerUpdate::Flag::PhotoChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		notifyPeerUpdated(update);
	}));
	subscribe(Auth().downloaderTaskFinished(), [this] {
		if (_waiting && _peer->userpicLoaded()) {
			_waiting = false;
			startNewPhotoShowing();
		}
	});
}

void UserpicButton::showFinished() {
	if (_notShownYet) {
		_notShownYet = false;
		if (!_waiting) {
			_a_appearance.finish();
			_a_appearance.start([this] { update(); }, 0, 1, st::profilePhotoDuration);
		}
	}
}

void UserpicButton::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (_a_appearance.animating(getms())) {
		p.drawPixmap(0, 0, _oldUserpic);
		p.setOpacity(_a_appearance.current());
	}
	p.drawPixmap(0, 0, _userpic);
}

void UserpicButton::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (update.peer != _peer) {
		return;
	}

	processNewPeerPhoto();
	this->update();
}

void UserpicButton::processPeerPhoto() {
	bool hasPhoto = (_peer->photoId && _peer->photoId != UnknownPeerPhotoId);
	setCursor(hasPhoto ? style::cur_pointer : style::cur_default);
	_waiting = !_peer->userpicLoaded();
	if (_waiting) {
		_peer->loadUserpic(true);
	}
}

void UserpicButton::processNewPeerPhoto() {
	processPeerPhoto();
	if (!_waiting) {
		startNewPhotoShowing();
	}
}

void UserpicButton::startNewPhotoShowing() {
	_oldUserpic = myGrab(this);
	_userpic = prepareUserpicPixmap();

	if (_notShownYet) {
		return;
	}

	_a_appearance.finish();
	_a_appearance.start([this] { update(); }, 0, 1, st::profilePhotoDuration);
	update();
}

QPixmap UserpicButton::prepareUserpicPixmap() const {
	auto retina = cIntRetinaFactor();
	auto size = width() * retina;
	QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(cRetinaFactor());
	image.fill(Qt::transparent);
	{
		Painter p(&image);
		_peer->paintUserpic(p, 0, 0, width());
	}
	return App::pixmapFromImageInPlace(std::move(image));
}

} // namespace Profile
