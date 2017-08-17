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
#include "profile/profile_widget.h"

#include "styles/style_settings.h"
#include "profile/profile_fixed_bar.h"
#include "profile/profile_inner_widget.h"
#include "profile/profile_section_memento.h"
#include "mainwindow.h"
#include "application.h"
#include "ui/effects/widget_fade_wrap.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"

namespace Profile {

Widget::Widget(QWidget *parent, not_null<Window::Controller*> controller, PeerData *peer) : Window::SectionWidget(parent, controller)
, _scroll(this, st::settingsScroll)
, _fixedBar(this, peer)
, _fixedBarShadow(this, object_ptr<Ui::PlainShadow>(this, st::shadowFg)) {
	_fixedBar->move(0, 0);
	_fixedBar->resizeToWidth(width());
	_fixedBar->show();

	_fixedBarShadow->hideFast();
	_fixedBarShadow->raise();
	updateAdaptiveLayout();
	subscribe(Adaptive::Changed(), [this]() { updateAdaptiveLayout(); });

	_inner = _scroll->setOwnedWidget(object_ptr<InnerWidget>(this, peer));
	_scroll->move(0, _fixedBar->height());
	_scroll->show();

	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
	connect(_inner, SIGNAL(cancelled()), _fixedBar, SLOT(onBack()));
}

void Widget::updateAdaptiveLayout() {
	_fixedBarShadow->moveToLeft(Adaptive::OneColumn() ? 0 : st::lineWidth, _fixedBar->height());
}

PeerData *Widget::peer() const {
	return _inner->peer();
}

bool Widget::hasTopBarShadow() const {
	return !_fixedBarShadow->isHidden() && !_fixedBarShadow->animating();
}

QPixmap Widget::grabForShowAnimation(const Window::SectionSlideParams &params) {
	if (params.withTopBarShadow || !_scroll->scrollTop()) _fixedBarShadow->hide();
	auto result = myGrab(this);
	if (params.withTopBarShadow) _fixedBarShadow->show();
	return result;
}

void Widget::doSetInnerFocus() {
	_inner->setFocus();
}

bool Widget::showInternal(not_null<Window::SectionMemento*> memento) {
	if (auto profileMemento = dynamic_cast<SectionMemento*>(memento.get())) {
		if (profileMemento->getPeer() == peer()) {
			restoreState(profileMemento);
			return true;
		}
	}
	return false;
}

void Widget::setInternalState(const QRect &geometry, not_null<SectionMemento*> memento) {
	setGeometry(geometry);
	myEnsureResized(this);
	restoreState(memento);
}

std::unique_ptr<Window::SectionMemento> Widget::createMemento() {
	auto result = std::make_unique<SectionMemento>(peer());
	saveState(result.get());
	return std::move(result);
}

void Widget::saveState(not_null<SectionMemento*> memento) {
	memento->setScrollTop(_scroll->scrollTop());
	_inner->saveState(memento);
}

void Widget::restoreState(not_null<SectionMemento*> memento) {
	_inner->restoreState(memento);
	auto scrollTop = memento->getScrollTop();
	_scroll->scrollToY(scrollTop);
	updateScrollState();
	_fixedBarShadow->finishAnimation();
}

void Widget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}

	int newScrollTop = _scroll->scrollTop() + topDelta();
	_fixedBar->resizeToWidth(width());
	_fixedBarShadow->entity()->resize(width(), st::lineWidth);

	QSize scrollSize(width(), height() - _fixedBar->height());
	if (_scroll->size() != scrollSize) {
		_scroll->resize(scrollSize);
		_inner->resizeToWidth(scrollSize.width(), _scroll->height());
	}
	_fixedBar->setHideShareContactButton(_inner->shareContactButtonShown());

	if (!_scroll->isHidden()) {
		if (topDelta()) {
			_scroll->scrollToY(newScrollTop);
		}
		updateScrollState();
	}
}

void Widget::updateScrollState() {
	auto scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	if (scrollTop > 0) {
		_fixedBarShadow->showAnimated();
	} else {
		_fixedBarShadow->hideAnimated();
	}
}

void Widget::onScroll() {
	updateScrollState();
}

void Widget::showAnimatedHook() {
	_fixedBar->setAnimatingMode(true);
}

void Widget::showFinishedHook() {
	_fixedBar->setAnimatingMode(false);
	if (!_scroll->scrollTop()) {
		_fixedBarShadow->hide();
	}
	_inner->showFinished();
}

bool Widget::wheelEventFromFloatPlayer(QEvent *e, Window::Column myColumn, Window::Column playerColumn) {
	return _scroll->viewportEvent(e);
}

QRect Widget::rectForFloatPlayer(Window::Column myColumn, Window::Column playerColumn) {
	return mapToGlobal(_scroll->geometry());
}

} // namespace Profile
