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
#include "ui/widgets/inner_dropdown.h"

#include "mainwindow.h"
#include "ui/scrollarea.h"
#include "profile/profile_members_widget.h"

namespace Ui {

InnerDropdown::InnerDropdown(QWidget *parent, const style::InnerDropdown &st) : TWidget(parent)
, _st(st)
, _shadow(_st.shadow)
, _scroll(this, _st.scroll) {
	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(onHideAnimated()));

	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));

	if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
		connect(App::wnd()->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onWindowActiveChanged()));
	}

	hide();
}

void InnerDropdown::setOwnedWidget(TWidget *widget) {
	auto container = new Container(_scroll, widget, _st);
	connect(widget, SIGNAL(heightUpdated()), this, SLOT(onWidgetHeightUpdated()));
	_scroll->setOwnedWidget(container);
	container->resizeToWidth(_scroll->width());
	container->moveToLeft(0, 0);
	container->show();
	widget->show();
}

void InnerDropdown::setMaxHeight(int newMaxHeight) {
	_maxHeight = newMaxHeight;
	resizeToContent();
}

void InnerDropdown::resizeToContent() {
	auto newWidth = _st.padding.left() + _st.scrollMargin.left() + _st.scrollMargin.right() + _st.padding.right();
	auto newHeight = _st.padding.top() + _st.scrollMargin.top() + _st.scrollMargin.bottom() + _st.padding.bottom();
	if (auto widget = static_cast<Container*>(_scroll->widget())) {
		widget->resizeToContent();
		newWidth += widget->width();
		newHeight += widget->height();
	}
	if (_maxHeight > 0) {
		accumulate_min(newHeight, _maxHeight);
	}
	if (newWidth != width() || newHeight != height()) {
		resize(newWidth, newHeight);
	}
}

void InnerDropdown::onWindowActiveChanged() {
	if (!App::wnd()->windowHandle()->isActive() && !isHidden()) {
		leaveEvent(nullptr);
	}
}

void InnerDropdown::resizeEvent(QResizeEvent *e) {
	_scroll->setGeometry(rect().marginsRemoved(_st.padding).marginsRemoved(_st.scrollMargin));
	if (auto widget = static_cast<TWidget*>(_scroll->widget())) {
		widget->resizeToWidth(_scroll->width());
		onScroll();
	}
}

void InnerDropdown::onScroll() {
	if (auto widget = static_cast<TWidget*>(_scroll->widget())) {
		int visibleTop = _scroll->scrollTop();
		int visibleBottom = visibleTop + _scroll->height();
		widget->setVisibleTopBottom(visibleTop, visibleBottom);
	}
}

void InnerDropdown::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (!_cache.isNull()) {
		bool animating = _a_appearance.animating(getms());
		if (animating) {
			p.setOpacity(_a_appearance.current(_hiding ? 0. : 1.));
		} else if (_hiding || isHidden()) {
			hideFinished();
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
	auto shadowedRect = rect().marginsRemoved(_st.padding);
	_shadow.paint(p, shadowedRect, _st.shadowShift);
	p.fillRect(shadowedRect, st::windowBg);
}

void InnerDropdown::enterEvent(QEvent *e) {
	showAnimated();
	return TWidget::enterEvent(e);
}

void InnerDropdown::leaveEvent(QEvent *e) {
	if (_a_appearance.animating(getms())) {
		hideAnimated();
	} else {
		_hideTimer.start(300);
	}
	return TWidget::leaveEvent(e);
}

void InnerDropdown::otherEnter() {
	showAnimated();
}

void InnerDropdown::otherLeave() {
	if (_a_appearance.animating(getms())) {
		hideAnimated();
	} else {
		_hideTimer.start(0);
	}
}

void InnerDropdown::showAnimated() {
	_hideTimer.stop();
	showStarted();
}

void InnerDropdown::hideAnimated(HideOption option) {
	if (isHidden()) return;
	if (option == HideOption::IgnoreShow) {
		_ignoreShowEvents = true;
	}
	if (_hiding) return;

	_hideTimer.stop();
	_hiding = true;
	startAnimation();
}

void InnerDropdown::hideFast() {
	if (isHidden()) return;

	_hideTimer.stop();
	_hiding = false;
	_a_appearance.finish();
	hideFinished();
}

void InnerDropdown::startAnimation() {
	auto from = _hiding ? 1. : 0.;
	auto to = _hiding ? 0. : 1.;
	if (!_a_appearance.animating()) {
		showChildren();
		_cache = myGrab(this);
	}
	hideChildren();
	_a_appearance.start([this] { repaintCallback(); }, from, to, _st.duration);
}

void InnerDropdown::hideFinished() {
	_cache = QPixmap();
	_ignoreShowEvents = false;
	if (!isHidden()) {
		emit beforeHidden();
		hide();
	}
}

void InnerDropdown::showStarted() {
	if (_ignoreShowEvents) return;
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
	if (!_a_appearance.animating() && _hiding) {
		_hiding = false;
		hideFinished();
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

int InnerDropdown::resizeGetHeight(int newWidth) {
	auto newHeight = _st.padding.top() + _st.scrollMargin.top() + _st.scrollMargin.bottom() + _st.padding.bottom();
	if (auto widget = static_cast<TWidget*>(_scroll->widget())) {
		widget->resizeToWidth(newWidth - _st.padding.left() - _st.padding.right() - _st.scrollMargin.left() - _st.scrollMargin.right());
		newHeight += widget->height();
	}
	if (_maxHeight > 0) {
		accumulate_min(newHeight, _maxHeight);
	}
	return newHeight;
}

InnerDropdown::Container::Container(QWidget *parent, TWidget *child, const style::InnerDropdown &st) : TWidget(parent), _st(st) {
	child->setParent(this);
	child->moveToLeft(_st.scrollPadding.left(), _st.scrollPadding.top());
}

void InnerDropdown::Container::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	if (auto child = static_cast<TWidget*>(children().front())) {
		child->setVisibleTopBottom(visibleTop - _st.scrollPadding.top(), visibleBottom - _st.scrollPadding.top());
	}
}

void InnerDropdown::Container::resizeToContent() {
	auto newWidth = _st.scrollPadding.top() + _st.scrollPadding.bottom();
	auto newHeight = _st.scrollPadding.top() + _st.scrollPadding.bottom();
	if (auto child = static_cast<TWidget*>(children().front())) {
		newWidth += child->width();
		newHeight += child->height();
	}
	if (newWidth != width() || newHeight != height()) {
		resize(newWidth, newHeight);
	}
}

int InnerDropdown::Container::resizeGetHeight(int newWidth) {
	int innerWidth = newWidth - _st.scrollPadding.left() - _st.scrollPadding.right();
	int result = _st.scrollPadding.top() + _st.scrollPadding.bottom();
	if (auto child = static_cast<TWidget*>(children().front())) {
		child->resizeToWidth(innerWidth);
		child->moveToLeft(_st.scrollPadding.left(), _st.scrollPadding.top());
		result += child->height();
	}
	return result;
}

} // namespace Ui
