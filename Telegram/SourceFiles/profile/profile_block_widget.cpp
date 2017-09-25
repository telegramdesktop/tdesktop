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
#include "profile/profile_block_widget.h"

#include "styles/style_profile.h"
#include "styles/style_widgets.h"

namespace Profile {

BlockWidget::BlockWidget(
	QWidget *parent,
	PeerData *peer,
	const QString &title) : RpWidget(parent)
, _peer(peer)
, _title(title) {
}

int BlockWidget::contentTop() const {
	return emptyTitle() ? 0 : (st::profileBlockMarginTop + st::profileBlockTitleHeight);
}

void BlockWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	paintTitle(p);
	paintContents(p);
}

void BlockWidget::paintTitle(Painter &p) {
	if (emptyTitle()) return;

	p.setFont(st::profileBlockTitleFont);
	p.setPen(st::profileBlockTitleFg);
	int titleLeft = st::profileBlockTitlePosition.x();
	int titleTop = st::profileBlockMarginTop + st::profileBlockTitlePosition.y();
	p.drawTextLeft(titleLeft, titleTop, width(), _title);
}

int defaultOutlineButtonLeft() {
	return st::profileBlockTitlePosition.x() - st::defaultLeftOutlineButton.padding.left();
}

} // namespace Profile
