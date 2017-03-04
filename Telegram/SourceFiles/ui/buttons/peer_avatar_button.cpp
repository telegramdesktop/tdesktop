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
#include "ui/buttons/peer_avatar_button.h"

#include "structs.h"
#include "ui/effects/ripple_animation.h"
#include "styles/style_boxes.h"

namespace Ui {

PeerAvatarButton::PeerAvatarButton(QWidget *parent, PeerData *peer, const style::PeerAvatarButton &st) : AbstractButton(parent)
, _peer(peer)
, _st(st) {
	resize(_st.size, _st.size);
}

void PeerAvatarButton::paintEvent(QPaintEvent *e) {
	if (_peer) {
		Painter p(this);
		_peer->paintUserpic(p, (_st.size - _st.photoSize) / 2, (_st.size - _st.photoSize) / 2, _st.photoSize);
	}
}

NewAvatarButton::NewAvatarButton(QWidget *parent, int size, QPoint position) : RippleButton(parent, st::defaultActiveButton.ripple)
, _position(position) {
	resize(size, size);
}

void NewAvatarButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (!_image.isNull()) {
		p.drawPixmap(0, 0, _image);
		return;
	}

	p.setPen(Qt::NoPen);
	p.setBrush(isOver() ? st::defaultActiveButton.textBgOver : st::defaultActiveButton.textBg);
	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(rect());
	}

	paintRipple(p, 0, 0, getms());

	st::newGroupPhotoIcon.paint(p, _position, width());
}

void NewAvatarButton::setImage(const QImage &image) {
	auto small = image.scaled(size() * cIntRetinaFactor(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	Images::prepareCircle(small);
	_image = App::pixmapFromImageInPlace(std::move(small));
	_image.setDevicePixelRatio(cRetinaFactor());
	update();
}

QImage NewAvatarButton::prepareRippleMask() const {
	return Ui::RippleAnimation::ellipseMask(size());
}

} // namespace Ui
