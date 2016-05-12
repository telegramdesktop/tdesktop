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
#include "profile/profile_inner_widget.h"

#include "styles/style_profile.h"
#include "profile/profile_cover.h"

namespace Profile {

InnerWidget::InnerWidget(QWidget *parent, PeerData *peer) : TWidget(parent)
, _peer(peer)
, _cover(this, peer) {
}

void InnerWidget::resizeToWidth(int newWidth, int minHeight) {
	int naturalHeight = resizeGetHeight(newWidth);
	_addedHeight = qMax(minHeight - naturalHeight, 0);
	resize(newWidth, naturalHeight + _addedHeight);
}

void InnerWidget::decreaseAdditionalHeight(int removeHeight) {
	if (removeHeight > 0) {
		resizeToWidth(width(), height() - removeHeight);
	}
}

void InnerWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), st::white);
}

int InnerWidget::resizeGetHeight(int newWidth) {
	_cover->resizeToWidth(newWidth);
	int newHeight = _cover->height();

	return newHeight;
}

} // namespace Profile
