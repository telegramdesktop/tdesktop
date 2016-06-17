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
#include "ui/inner_dropdown.h"

#include "mainwindow.h"
#include "ui/scrollarea.h"
#include "profile/profile_members_widget.h"

namespace Ui {

InnerDropdown::InnerDropdown(QWidget *parent, const style::dropdown &st, const style::flatScroll &scrollSt) : TWidget(parent)
, _st(st)
, _shadow(_st.shadow)
, _scroll(this, scrollSt) {
	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(onHideStart()));

	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));

	if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
		connect(App::wnd()->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onWindowActiveChanged()));
	}

	hide();
}

void InnerDropdown::setOwnedWidget(ScrolledWidget *widget) {
	_scroll->setOwnedWidget(widget);
	widget->show();
	widget->move(0, 0);
	widget->resizeToWidth(_scroll->width() - st::scrollDef.width);
}

void InnerDropdown::onWindowActiveChanged() {
	if (!App::wnd()->windowHandle()->isActive() && !isHidden()) {
		leaveEvent(nullptr);
	}
}

void InnerDropdown::resizeEvent(QResizeEvent *e) {
	_scroll->setGeometry(rect().marginsRemoved(_st.padding));
	if (auto widget = static_cast<ScrolledWidget*>(_scroll->widget())) {
		widget->resizeToWidth(_scroll->width() - st::scrollDef.width);
		onScroll();
	}
}

void InnerDropdown::onScroll() {
	if (auto widget = static_cast<ScrolledWidget*>(_scroll->widget())) {
		int visibleTop = _scroll->scrollTop();
		int visibleBottom = visibleTop + _scroll->height();
		widget->setVisibleTopBottom(visibleTop, visibleBottom);
	}
}

void InnerDropdown::paintEvent(QPaintEvent *e) {
	QPainter p(this);

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
	QRect shadowedRect = rect().marginsRemoved(_st.padding);
	_shadow.paint(p, shadowedRect, _st.shadowShift);
	p.fillRect(shadowedRect, st::windowBg);
}

void InnerDropdown::enterEvent(QEvent *e) {
	_hideTimer.stop();
	if (_hiding) showingStarted();
	return TWidget::enterEvent(e);
}

void InnerDropdown::leaveEvent(QEvent *e) {
	if (_a_appearance.animating(getms())) {
		onHideStart();
	} else {
		_hideTimer.start(300);
	}
	return TWidget::leaveEvent(e);
}

void InnerDropdown::otherEnter() {
	_hideTimer.stop();
	showingStarted();
}

void InnerDropdown::otherLeave() {
	if (_a_appearance.animating(getms())) {
		onHideStart();
	} else {
		_hideTimer.start(0);
	}
}

void InnerDropdown::onHideStart() {
	if (_hiding) return;

	_hiding = true;
	startAnimation();
}

void InnerDropdown::startAnimation() {
	auto from = _hiding ? 1. : 0.;
	auto to = _hiding ? 0. : 1.;
	if (_a_appearance.isNull()) {
		showChildren();
		_cache = myGrab(this);
	}
	hideChildren();
	START_ANIMATION(_a_appearance, func(this, &InnerDropdown::repaintCallback), from, to, _st.duration, anim::linear);
}

void InnerDropdown::hidingFinished() {
	hide();
//	showChildren();
	emit hidden();
}

void InnerDropdown::showingStarted() {
	if (isHidden()) {
		show();
	} else if (!_hiding) {
		return;
	}
	_hiding = false;
	startAnimation();
}

void InnerDropdown::repaintCallback() {
	update();
	if (!_a_appearance.animating(getms()) && _hiding) {
		_hiding = false;
		hidingFinished();
	}
}

bool InnerDropdown::eventFilter(QObject *obj, QEvent *e) {
	if (e->type() == QEvent::Enter) {
		otherEnter();
	} else if (e->type() == QEvent::Leave) {
		otherLeave();
	} else if (e->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton) {
		if (isHidden() || _hiding) {
			otherEnter();
		} else {
			otherLeave();
		}
	}
	return false;
}

} // namespace Ui
