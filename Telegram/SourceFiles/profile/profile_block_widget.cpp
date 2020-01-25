/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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

} // namespace Profile
