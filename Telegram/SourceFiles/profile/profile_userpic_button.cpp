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
#include "profile/profile_userpic_button.h"

#include "styles/style_profile.h"
#include "observer_peer.h"
#include "mtproto/file_download.h"

namespace Profile {

UserpicButton::UserpicButton(QWidget *parent, PeerData *peer) : Button(parent), _peer(peer) {
	resize(st::profilePhotoSize, st::profilePhotoSize);

	processPeerPhoto();
	_notShownYet = _waiting;
	if (!_waiting) {
		_userpic = prepareUserpicPixmap();
	}

	Notify::registerPeerObserver(Notify::PeerUpdate::Flag::PhotoChanged, this, &UserpicButton::notifyPeerUpdated);
	FileDownload::registerImageLoadedObserver(this, &UserpicButton::notifyImageLoaded);
}

void UserpicButton::showFinished() {
	if (_notShownYet) {
		_notShownYet = false;
		if (!_waiting) {
			_a_appearance.finish();
			START_ANIMATION(_a_appearance, func(this, &UserpicButton::refreshCallback), 0, 1, st::profilePhotoDuration, anim::linear);
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

void UserpicButton::notifyImageLoaded() {
	if (_waiting && _peer->userpicLoaded()) {
		_waiting = false;
		startNewPhotoShowing();
	}
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
	START_ANIMATION(_a_appearance, func(this, &UserpicButton::refreshCallback), 0, 1, st::profilePhotoDuration, anim::linear);
	update();
}

QPixmap UserpicButton::prepareUserpicPixmap() const {
	auto retina = cIntRetinaFactor();
	auto size = st::profilePhotoSize * retina;
	QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&image);
		p.fillRect(0, 0, st::profilePhotoSize, st::profilePhotoSize, st::profileBg);
		_peer->paintUserpic(p, st::profilePhotoSize, 0, 0);
	}
	return App::pixmapFromImageInPlace(std_::move(image));
}

} // namespace Profile
