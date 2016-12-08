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

#include "styles/style_settings.h"
#include "profile/profile_fixed_bar.h"
#include "profile/profile_inner_widget.h"
#include "profile/profile_section_memento.h"
#include "mainwindow.h"
#include "application.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"

namespace Profile {

Widget::Widget(QWidget *parent, PeerData *peer) : Window::SectionWidget(parent)
, _scroll(this, st::settingsScroll)
, _inner(this, peer)
, _fixedBar(this, peer)
, _fixedBarShadow(this, st::shadowColor) {
	_fixedBar->move(0, 0);
	_fixedBar->resizeToWidth(width());
	_fixedBar->show();

	_fixedBarShadow->setMode(Ui::ToggleableShadow::Mode::HiddenFast);
	_fixedBarShadow->raise();
	updateAdaptiveLayout();
	subscribe(Adaptive::Changed(), [this]() { updateAdaptiveLayout(); });

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

bool Widget::hasTopBarShadow() const {
	return _fixedBarShadow->isFullyShown();
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
		if (profileMemento->getPeer() == peer()) {
			restoreState(profileMemento);
			return true;
		}
	}
	return false;
}

void Widget::setInternalState(const QRect &geometry, const SectionMemento *memento) {
	setGeometry(geometry);
	myEnsureResized(this);
	restoreState(memento);
}

std_::unique_ptr<Window::SectionMemento> Widget::createMemento() const {
	auto result = std_::make_unique<SectionMemento>(peer());
	saveState(result.get());
	return std_::move(result);
}

void Widget::saveState(SectionMemento *memento) const {
	memento->setScrollTop(_scroll->scrollTop());
	_inner->saveState(memento);
}

void Widget::restoreState(const SectionMemento *memento) {
	_inner->restoreState(memento);
	auto scrollTop = memento->getScrollTop();
	_scroll->scrollToY(scrollTop);
	_fixedBarShadow->setMode((scrollTop > 0) ? Ui::ToggleableShadow::Mode::ShownFast : Ui::ToggleableShadow::Mode::HiddenFast);
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
		_inner->resizeToWidth(scrollSize.width(), _scroll->height());
	}
	_fixedBar->setHideShareContactButton(_inner->shareContactButtonShown());

	if (!_scroll->isHidden()) {
		if (topDelta()) {
			_scroll->scrollToY(newScrollTop);
		}
		int scrollTop = _scroll->scrollTop();
		_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
		_fixedBarShadow->setMode((scrollTop > 0) ? Ui::ToggleableShadow::Mode::Shown : Ui::ToggleableShadow::Mode::Hidden);
	}
}

void Widget::onScroll() {
	int scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	_fixedBarShadow->setMode((scrollTop > 0) ? Ui::ToggleableShadow::Mode::Shown : Ui::ToggleableShadow::Mode::Hidden);
}

void Widget::showAnimatedHook() {
	_fixedBar->setAnimatingMode(true);
}

void Widget::showFinishedHook() {
	_fixedBar->setAnimatingMode(false);
	_inner->showFinished();
}

} // namespace Profile
