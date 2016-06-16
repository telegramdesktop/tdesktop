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
#include "profile/profile_widget.h"

#include "profile/profile_fixed_bar.h"
#include "profile/profile_inner_widget.h"
#include "profile/profile_section_memento.h"
#include "mainwindow.h"
#include "application.h"

namespace Profile {

Widget::Widget(QWidget *parent, PeerData *peer) : Window::SectionWidget(parent)
, _scroll(this, st::setScroll)
, _inner(this, peer)
, _fixedBar(this, peer)
, _fixedBarShadow(this, st::shadowColor) {
	_fixedBar->move(0, 0);
	_fixedBar->resizeToWidth(width());
	_fixedBar->show();

	_fixedBarShadow->setMode(ToggleableShadow::Mode::HiddenFast);
	_fixedBarShadow->raise();
	updateAdaptiveLayout();

	_scroll->setOwnedWidget(_inner);
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

QPixmap Widget::grabForShowAnimation(const Window::SectionSlideParams &params) {
	if (params.withTopBarShadow) _fixedBarShadow->hide();
	auto result = myGrab(this);
	if (params.withTopBarShadow) _fixedBarShadow->show();
	return result;
}

void Widget::setInnerFocus() {
	_inner->setFocus();
}

bool Widget::showInternal(const Window::SectionMemento *memento) {
	if (auto profileMemento = dynamic_cast<const SectionMemento*>(memento)) {
		if (profileMemento->_peer == peer()) {
			// Perhaps no need to do that?..
			_scroll->scrollToY(profileMemento->_scrollTop);

			return true;
		}
	}
	return false;
}

void Widget::setInternalState(const SectionMemento *memento) {
	myEnsureResized(this);
	_scroll->scrollToY(memento->_scrollTop);
	_fixedBarShadow->setMode(memento->_scrollTop > 0 ? ToggleableShadow::Mode::ShownFast : ToggleableShadow::Mode::HiddenFast);
}

std_::unique_ptr<Window::SectionMemento> Widget::createMemento() const {
	auto result = std_::make_unique<SectionMemento>(peer());
	result->_scrollTop = _scroll->scrollTop();
	return std_::move(result);
}

void Widget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}

	int newScrollTop = _scroll->scrollTop() + topDelta();
	_fixedBar->resizeToWidth(width());
	_fixedBarShadow->resize(width(), st::lineWidth);

	QSize scrollSize(width(), height() - _fixedBar->height());
	if (_scroll->size() != scrollSize) {
		_scroll->resize(scrollSize);
//		_inner->resizeToWidth(scrollSize.width(), _scroll->height());
		_inner->resizeToWidth(scrollSize.width(), _scroll->height() * 2); // testing
	}
	_fixedBar->setHideShareContactButton(_inner->shareContactButtonShown());

	if (!_scroll->isHidden()) {
		if (topDelta()) {
			_scroll->scrollToY(newScrollTop);
		}
		int scrollTop = _scroll->scrollTop();
		_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
		_fixedBarShadow->setMode((scrollTop > 0) ? ToggleableShadow::Mode::Shown : ToggleableShadow::Mode::Hidden);
	}
}

void Widget::onScroll() {
	int scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	_fixedBarShadow->setMode((scrollTop > 0) ? ToggleableShadow::Mode::Shown : ToggleableShadow::Mode::Hidden);
}

void Widget::showAnimatedHook() {
	_fixedBar->setAnimatingMode(true);
}

void Widget::showFinishedHook() {
	_fixedBar->setAnimatingMode(false);
	_inner->showFinished();
}

} // namespace Profile
