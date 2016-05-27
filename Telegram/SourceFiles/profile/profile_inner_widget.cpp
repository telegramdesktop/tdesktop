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
#include "apiwrap.h"

namespace Profile {

InnerWidget::InnerWidget(QWidget *parent, PeerData *peer) : TWidget(parent)
, _peer(peer)
, _cover(this, peer) {
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void InnerWidget::resizeToWidth(int newWidth, int minHeight) {
	int naturalHeight = resizeGetHeight(newWidth);
	_addedHeight = qMax(minHeight - naturalHeight, 0);
	resize(newWidth, naturalHeight + _addedHeight);
}

void InnerWidget::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;

	int notDisplayedAtBottom = height() - _visibleBottom;
	if (notDisplayedAtBottom > 0) {
//		decreaseAdditionalHeight(notDisplayedAtBottom); // testing
	}

	//loadProfilePhotos(_visibleTop);
	if (peer()->isMegagroup() && !peer()->asChannel()->mgInfo->lastParticipants.isEmpty() && peer()->asChannel()->mgInfo->lastParticipants.size() < peer()->asChannel()->count) {
		if (_visibleTop + (PreloadHeightsCount + 1) * (_visibleBottom - _visibleTop) > height()) {
			App::api()->requestLastParticipants(peer()->asChannel(), false);
		}
	}
}

bool InnerWidget::shareContactButtonShown() const {
	return _cover->shareContactButtonShown();
}

void InnerWidget::showFinished() {
	_cover->showFinished();
}

void InnerWidget::decreaseAdditionalHeight(int removeHeight) {
	resizeToWidth(width(), height() - removeHeight);
}

void InnerWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), st::profileBg);
}

void InnerWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		emit cancelled();
	}
}

int InnerWidget::resizeGetHeight(int newWidth) {
	_cover->resizeToWidth(newWidth);
	int newHeight = _cover->height();

	return newHeight;
}

} // namespace Profile
