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
#include "media/player/media_player_widget.h"

#include "media/player/media_player_cover.h"
#include "media/player/media_player_list.h"
#include "styles/style_media_player.h"
#include "mainwindow.h"

namespace Media {
namespace Player {

Widget::Widget(QWidget *parent) : TWidget(parent)
, _shadow(st::defaultInnerDropdown.shadow)
, _cover(this) {
	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(onHideStart()));

	_showTimer.setSingleShot(true);
	connect(&_showTimer, SIGNAL(timeout()), this, SLOT(onShowStart()));

	if (_scroll) {
		connect(_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
	}

	if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
		connect(App::wnd()->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onWindowActiveChanged()));
	}

	hide();
	resize(contentLeft() + st::mediaPlayerWidth, st::mediaPlayerCoverHeight + st::mediaPlayerMarginBottom);
}

bool Widget::overlaps(const QRect &globalRect) {
	if (isHidden() || _a_appearance.animating()) return false;

	auto marginLeft = rtl() ? 0 : contentLeft();
	auto marginRight = rtl() ? contentLeft() : 0;
	return rect().marginsRemoved(QMargins(marginLeft, 0, marginRight, st::mediaPlayerMarginBottom)).contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
}

void Widget::onWindowActiveChanged() {
	if (!App::wnd()->windowHandle()->isActive() && !isHidden()) {
		leaveEvent(nullptr);
	}
}

void Widget::resizeEvent(QResizeEvent *e) {
	_cover->resize(width() - contentLeft(), st::mediaPlayerCoverHeight);
	_cover->moveToRight(0, 0);
	if (_scroll) {
		_scroll->resize(width(), height() - _cover->height());
		_scroll->moveToRight(0, _cover->height());
		_list->resizeToWidth(width());
	}
	//_scroll->setGeometry(rect().marginsRemoved(_st.padding).marginsRemoved(_st.scrollMargin));
	//if (auto widget = static_cast<ScrolledWidget*>(_scroll->widget())) {
	//	widget->resizeToWidth(_scroll->width());
	//	onScroll();
	//}
}

void Widget::onScroll() {
	//if (auto widget = static_cast<ScrolledWidget*>(_scroll->widget())) {
	//	int visibleTop = _scroll->scrollTop();
	//	int visibleBottom = visibleTop + _scroll->height();
	//	widget->setVisibleTopBottom(visibleTop, visibleBottom);
	//}
}

void Widget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (!_cache.isNull()) {
		bool animating = _a_appearance.animating(getms());
		if (animating) {
			p.setOpacity(_a_appearance.current(_hiding));
		} else if (_hiding) {
			hidingFinished();
			return;
		}
		p.drawPixmap(0, 0, _cache);
		if (!animating) {
			showChildren();
			_cache = QPixmap();
		}
		return;
	}

	// draw shadow
	auto shadowedRect = myrtlrect(contentLeft(), 0, contentWidth(), height() - st::mediaPlayerMarginBottom);
	auto shadowedSides = (rtl() ? Ui::RectShadow::Side::Right : Ui::RectShadow::Side::Left) | Ui::RectShadow::Side::Bottom;
	_shadow.paint(p, shadowedRect, st::defaultInnerDropdown.shadowShift, shadowedSides);
	p.fillRect(shadowedRect, st::windowBg);
}

void Widget::enterEvent(QEvent *e) {
	_hideTimer.stop();
	if (_a_appearance.animating(getms())) {
		onShowStart();
	} else {
		_showTimer.start(0);
	}
	return TWidget::enterEvent(e);
}

void Widget::leaveEvent(QEvent *e) {
	_showTimer.stop();
	if (_a_appearance.animating(getms())) {
		onHideStart();
	} else {
		_hideTimer.start(300);
	}
	return TWidget::leaveEvent(e);
}

void Widget::otherEnter() {
	_hideTimer.stop();
	if (_a_appearance.animating(getms())) {
		onShowStart();
	} else {
		_showTimer.start(300);
	}
}

void Widget::otherLeave() {
	_showTimer.stop();
	if (_a_appearance.animating(getms())) {
		onHideStart();
	} else {
		_hideTimer.start(0);
	}
}

void Widget::onShowStart() {
	if (isHidden()) {
		show();
	} else if (!_hiding) {
		return;
	}
	_hiding = false;
	startAnimation();
}

void Widget::onHideStart() {
	if (_hiding) return;

	_hiding = true;
	startAnimation();
}

void Widget::startAnimation() {
	auto from = _hiding ? 1. : 0.;
	auto to = _hiding ? 0. : 1.;
	if (!_a_appearance.animating()) {
		showChildren();
		_cache = myGrab(this);
	}
	hideChildren();
	_a_appearance.start([this]() {
		update();
		if (!_a_appearance.animating(getms()) && _hiding) {
			_hiding = false;
			hidingFinished();
		}
	}, from, to, st::defaultInnerDropdown.duration);
}

void Widget::hidingFinished() {
	hide();
	showChildren();
}

int Widget::contentLeft() const {
	return st::mediaPlayerMarginLeft;
}

bool Widget::eventFilter(QObject *obj, QEvent *e) {
	if (e->type() == QEvent::Enter) {
		otherEnter();
	} else if (e->type() == QEvent::Leave) {
		otherLeave();
	}
	return false;
}

} // namespace Player
} // namespace Media
