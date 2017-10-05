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
#include "media/player/media_player_panel.h"

#include "media/player/media_player_cover.h"
#include "media/player/media_player_list.h"
#include "media/player/media_player_instance.h"
#include "styles/style_overview.h"
#include "styles/style_widgets.h"
#include "styles/style_media_player.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/scroll_area.h"
#include "mainwindow.h"

namespace Media {
namespace Player {

Panel::Panel(QWidget *parent, Layout layout) : TWidget(parent)
, _layout(layout)
, _scroll(this, st::mediaPlayerScroll) {
	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(onHideStart()));

	_showTimer.setSingleShot(true);
	connect(&_showTimer, SIGNAL(timeout()), this, SLOT(onShowStart()));

	hide();
	updateSize();
}

bool Panel::overlaps(const QRect &globalRect) {
	if (isHidden() || _a_appearance.animating()) return false;

	auto marginLeft = rtl() ? contentRight() : contentLeft();
	auto marginRight = rtl() ? contentLeft() : contentRight();
	return rect().marginsRemoved(QMargins(marginLeft, contentTop(), marginRight, contentBottom())).contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
}

void Panel::onWindowActiveChanged() {
	if (!App::wnd()->windowHandle()->isActive() && !isHidden()) {
		leaveEvent(nullptr);
	}
}

void Panel::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void Panel::onListHeightUpdated() {
	if (auto widget = _scroll->widget()) {
		if (widget->height() > 0 || _cover) {
			updateSize();
		} else {
			hideIgnoringEnterEvents();
		}
	}
}

void Panel::updateControlsGeometry() {
	auto scrollTop = contentTop();
	auto width = contentWidth();
	if (_cover) {
		_cover->resizeToWidth(width);
		_cover->moveToRight(contentRight(), scrollTop);
		scrollTop += _cover->height();
		if (_scrollShadow) {
			_scrollShadow->resize(width, st::mediaPlayerScrollShadow.extend.bottom());
			_scrollShadow->moveToRight(contentRight(), scrollTop);
		}
	}
	auto scrollHeight = qMax(height() - scrollTop - contentBottom() - scrollMarginBottom(), 0);
	if (scrollHeight > 0) {
		_scroll->setGeometryToRight(contentRight(), scrollTop, width, scrollHeight);
	}
	if (auto widget = static_cast<TWidget*>(_scroll->widget())) {
		widget->resizeToWidth(width);
		onScroll();
	}
}

int Panel::bestPositionFor(int left) const {
	left -= contentLeft();
	left -= st::mediaPlayerFileLayout.songPadding.left();
	left -= st::mediaPlayerFileLayout.songThumbSize / 2;
	return left;
}

void Panel::scrollPlaylistToCurrentTrack() {
	if (auto list = static_cast<ListWidget*>(_scroll->widget())) {
		auto rect = list->getCurrentTrackGeometry();
		auto top = _scroll->scrollTop(), bottom = top + _scroll->height();
		_scroll->scrollToY(rect.y());
	}
}

void Panel::onScroll() {
	if (auto widget = static_cast<TWidget*>(_scroll->widget())) {
		int visibleTop = _scroll->scrollTop();
		int visibleBottom = visibleTop + _scroll->height();
		widget->setVisibleTopBottom(visibleTop, visibleBottom);
	}
}

void Panel::updateSize() {
	auto width = contentLeft() + st::mediaPlayerPanelWidth + contentRight();
	auto height = contentTop();
	if (_cover) {
		height += _cover->height();
	}
	auto listHeight = 0;
	if (auto widget = _scroll->widget()) {
		listHeight = widget->height();
	}
	auto scrollVisible = (listHeight > 0);
	auto scrollHeight = scrollVisible ? (qMin(listHeight, st::mediaPlayerListHeightMax) + st::mediaPlayerListMarginBottom) : 0;
	height += scrollHeight + contentBottom();
	resize(width, height);
	_scroll->setVisible(scrollVisible);
	if (_scrollShadow) {
		_scrollShadow->setVisible(scrollVisible);
	}
}

void Panel::paintEvent(QPaintEvent *e) {
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
	auto shadowedRect = myrtlrect(contentLeft(), contentTop(), contentWidth(), contentHeight());
	auto shadowedSides = (rtl() ? RectPart::Right : RectPart::Left) | RectPart::Bottom;
	if (_layout != Layout::Full) {
		shadowedSides |= (rtl() ? RectPart::Left : RectPart::Right) | RectPart::Top;
	}
	Ui::Shadow::paint(p, shadowedRect, width(), st::defaultRoundShadow, shadowedSides);
	auto parts = RectPart::Full;
	App::roundRect(p, shadowedRect, st::menuBg, MenuCorners, nullptr, parts);
}

void Panel::enterEventHook(QEvent *e) {
	if (_ignoringEnterEvents) return;

	_hideTimer.stop();
	if (_a_appearance.animating(getms())) {
		onShowStart();
	} else {
		_showTimer.start(0);
	}
	return TWidget::enterEventHook(e);
}

void Panel::leaveEventHook(QEvent *e) {
	_showTimer.stop();
	if (_a_appearance.animating(getms())) {
		onHideStart();
	} else {
		_hideTimer.start(300);
	}
	return TWidget::leaveEventHook(e);
}

void Panel::showFromOther() {
	_hideTimer.stop();
	if (_a_appearance.animating(getms())) {
		onShowStart();
	} else {
		_showTimer.start(300);
	}
}

void Panel::hideFromOther() {
	_showTimer.stop();
	if (_a_appearance.animating(getms())) {
		onHideStart();
	} else {
		_hideTimer.start(0);
	}
}

void Panel::ensureCreated() {
	if (_scroll->widget()) return;

	if (_layout == Layout::Full) {
		_cover.create(this);
		setPinCallback(std::move(_pinCallback));
		setCloseCallback(std::move(_closeCallback));

		_scrollShadow.create(this, st::mediaPlayerScrollShadow, RectPart::Bottom);
	}
	auto list = object_ptr<ListWidget>(this);
	connect(list, SIGNAL(heightUpdated()), this, SLOT(onListHeightUpdated()));
	_scroll->setOwnedWidget(std::move(list));

	if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
		if (auto window = App::wnd()) {
			connect(window->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onWindowActiveChanged()));
		}
	}

	updateSize();
	updateControlsGeometry();
	_ignoringEnterEvents = false;
}

void Panel::performDestroy() {
	if (!_scroll->widget()) return;

	_cover.destroy();
	_scroll->takeWidget<ListWidget>().destroyDelayed();

	if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
		if (auto window = App::wnd()) {
			disconnect(window->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onWindowActiveChanged()));
		}
	}
}

void Panel::setPinCallback(ButtonCallback &&callback) {
	_pinCallback = std::move(callback);
	if (_cover) {
		_cover->setPinCallback(ButtonCallback(_pinCallback));
	}
}

void Panel::setCloseCallback(ButtonCallback &&callback) {
	_closeCallback = std::move(callback);
	if (_cover) {
		_cover->setCloseCallback(ButtonCallback(_closeCallback));
	}
}

void Panel::onShowStart() {
	ensureCreated();
	if (auto widget = _scroll->widget()) {
		if (widget->height() <= 0 && !_cover) {
			return;
		}
	}

	if (isHidden()) {
		scrollPlaylistToCurrentTrack();
		show();
	} else if (!_hiding) {
		return;
	}
	_hiding = false;
	startAnimation();
}

void Panel::hideIgnoringEnterEvents() {
	_ignoringEnterEvents = true;
	if (isHidden()) {
		hideFinished();
	} else {
		onHideStart();
	}
}

void Panel::onHideStart() {
	if (_hiding || isHidden()) return;

	_hiding = true;
	startAnimation();
}

void Panel::startAnimation() {
	auto from = _hiding ? 1. : 0.;
	auto to = _hiding ? 0. : 1.;
	if (_cache.isNull()) {
		showChildren();
		_cache = myGrab(this);
	}
	hideChildren();
	_a_appearance.start([this] { appearanceCallback(); }, from, to, st::defaultInnerDropdown.duration);
}

void Panel::appearanceCallback() {
	if (!_a_appearance.animating() && _hiding) {
		_hiding = false;
		hideFinished();
	} else {
		update();
	}
}

void Panel::hideFinished() {
	hide();
	_cache = QPixmap();
	performDestroy();
}

int Panel::contentLeft() const {
	return st::mediaPlayerPanelMarginLeft;
}

int Panel::contentTop() const {
	return (_layout == Layout::Full) ? 0 : st::mediaPlayerPanelMarginLeft;
}

int Panel::contentRight() const {
	return (_layout == Layout::Full) ? 0 : st::mediaPlayerPanelMarginLeft;
}

int Panel::contentBottom() const {
	return st::mediaPlayerPanelMarginBottom;
}

int Panel::scrollMarginBottom() const {
	return st::mediaPlayerPanelMarginBottom;
}

} // namespace Player
} // namespace Media
