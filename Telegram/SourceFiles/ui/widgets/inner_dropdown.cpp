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
#include "ui/widgets/inner_dropdown.h"

#include "mainwindow.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/effects/panel_animation.h"

namespace {

constexpr float64 kFadeHeight = 1. / 3;
constexpr int kFadeAlphaMax = 160;

} // namespace

namespace Ui {

InnerDropdown::InnerDropdown(QWidget *parent, const style::InnerDropdown &st) : TWidget(parent)
, _st(st)
, _scroll(this, _st.scroll) {
	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(onHideAnimated()));

	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));

	if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
		connect(App::wnd()->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onWindowActiveChanged()));
	}

	hide();
}

QPointer<TWidget> InnerDropdown::doSetOwnedWidget(object_ptr<TWidget> widget) {
	auto result = QPointer<TWidget>(widget);
	connect(widget, SIGNAL(heightUpdated()), this, SLOT(onWidgetHeightUpdated()));
	auto container = _scroll->setOwnedWidget(object_ptr<Container>(_scroll, std::move(widget), _st));
	container->resizeToWidth(_scroll->width());
	container->moveToLeft(0, 0);
	container->show();
	result->show();
	return result;
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
		update();
		finishAnimating();
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

	auto ms = getms();
	if (_a_show.animating(ms)) {
		if (auto opacity = _a_opacity.current(ms, _hiding ? 0. : 1.)) {
			// _a_opacity.current(ms)->opacityAnimationCallback()->_showAnimation.reset()
			if (_showAnimation) {
				_showAnimation->paintFrame(p, 0, 0, width(), _a_show.current(1.), opacity);
			}
		}
	} else if (_a_opacity.animating(ms)) {
		p.setOpacity(_a_opacity.current(0.));
		p.drawPixmap(0, 0, _cache);
	} else if (_hiding || isHidden()) {
		hideFinished();
	} else if (_showAnimation) {
		_showAnimation->paintFrame(p, 0, 0, width(), 1., 1.);
		_showAnimation.reset();
		showChildren();
	} else {
		if (!_cache.isNull()) _cache = QPixmap();
		auto inner = rect().marginsRemoved(_st.padding);
		Shadow::paint(p, inner, width(), _st.shadow);
		App::roundRect(p, inner, _st.bg, ImageRoundRadius::Small);
	}
}

void InnerDropdown::enterEventHook(QEvent *e) {
	if (_autoHiding) {
		showAnimated(_origin);
	}
	return TWidget::enterEventHook(e);
}

void InnerDropdown::leaveEventHook(QEvent *e) {
	if (_autoHiding) {
		auto ms = getms();
		if (_a_show.animating(ms) || _a_opacity.animating(ms)) {
			hideAnimated();
		} else {
			_hideTimer.start(300);
		}
	}
	return TWidget::leaveEventHook(e);
}

void InnerDropdown::otherEnter() {
	if (_autoHiding) {
		showAnimated(_origin);
	}
}

void InnerDropdown::otherLeave() {
	if (_autoHiding) {
		auto ms = getms();
		if (_a_show.animating(ms) || _a_opacity.animating(ms)) {
			hideAnimated();
		} else {
			_hideTimer.start(0);
		}
	}
}

void InnerDropdown::setOrigin(PanelAnimation::Origin origin) {
	_origin = origin;
}

void InnerDropdown::showAnimated(PanelAnimation::Origin origin) {
	setOrigin(origin);
	showAnimated();
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
	startOpacityAnimation(true);
}

void InnerDropdown::finishAnimating() {
	if (_a_show.animating()) {
		_a_show.finish();
		showAnimationCallback();
	}
	if (_showAnimation) {
		_showAnimation.reset();
		showChildren();
	}
	if (_a_opacity.animating()) {
		_a_opacity.finish();
		opacityAnimationCallback();
	}
}

void InnerDropdown::showFast() {
	_hideTimer.stop();
	finishAnimating();
	if (isHidden()) {
		showChildren();
		show();
	}
	_hiding = false;
}

void InnerDropdown::hideFast() {
	if (isHidden()) return;

	_hideTimer.stop();
	finishAnimating();
	_hiding = false;
	hideFinished();
}

void InnerDropdown::hideFinished() {
	_a_show.finish();
	_showAnimation.reset();
	_cache = QPixmap();
	_ignoreShowEvents = false;
	if (!isHidden()) {
		if (_hiddenCallback) {
			_hiddenCallback();
		}
		hide();
	}
}

void InnerDropdown::prepareCache() {
	if (_a_opacity.animating()) return;

	auto showAnimation = base::take(_a_show);
	auto showAnimationData = base::take(_showAnimation);
	showChildren();
	_cache = myGrab(this);
	_showAnimation = base::take(showAnimationData);
	_a_show = base::take(showAnimation);
	if (_a_show.animating()) {
		hideChildren();
	}
}

void InnerDropdown::startOpacityAnimation(bool hiding) {
	if (hiding) {
		if (_hideStartCallback) {
			_hideStartCallback();
		}
	} else if (_showStartCallback) {
		_showStartCallback();
	}

	_hiding = false;
	prepareCache();
	_hiding = hiding;
	hideChildren();
	_a_opacity.start([this] { opacityAnimationCallback(); }, _hiding ? 1. : 0., _hiding ? 0. : 1., _st.duration);
}

void InnerDropdown::showStarted() {
	if (_ignoreShowEvents) return;
	if (isHidden()) {
		show();
		startShowAnimation();
		return;
	} else if (!_hiding) {
		return;
	}
	startOpacityAnimation(false);
}

void InnerDropdown::startShowAnimation() {
	if (_showStartCallback) {
		_showStartCallback();
	}
	if (!_a_show.animating()) {
		auto opacityAnimation = base::take(_a_opacity);
		showChildren();
		auto cache = grabForPanelAnimation();
		_a_opacity = base::take(opacityAnimation);

		_showAnimation = std::make_unique<PanelAnimation>(_st.animation, _origin);
		auto inner = rect().marginsRemoved(_st.padding);
		_showAnimation->setFinalImage(std::move(cache), QRect(inner.topLeft() * cIntRetinaFactor(), inner.size() * cIntRetinaFactor()));
		auto corners = App::cornersMask(ImageRoundRadius::Small);
		_showAnimation->setCornerMasks(corners[0], corners[1], corners[2], corners[3]);
		_showAnimation->start();
	}
	hideChildren();
	_a_show.start([this] { showAnimationCallback(); }, 0., 1., _st.showDuration);
}

QImage InnerDropdown::grabForPanelAnimation() {
	myEnsureResized(this);
	auto result = QImage(size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		App::roundRect(p, rect().marginsRemoved(_st.padding), _st.bg, ImageRoundRadius::Small);
		for (auto child : children()) {
			if (auto widget = qobject_cast<QWidget*>(child)) {
				widget->render(&p, widget->pos(), widget->rect(), QWidget::DrawChildren | QWidget::IgnoreMask);
			}
		}
	}
	return result;
}

void InnerDropdown::opacityAnimationCallback() {
	update();
	if (!_a_opacity.animating()) {
		if (_hiding) {
			_hiding = false;
			hideFinished();
		} else if (!_a_show.animating()) {
			showChildren();
		}
	}
}

void InnerDropdown::showAnimationCallback() {
	update();
}

bool InnerDropdown::eventFilter(QObject *obj, QEvent *e) {
	if (e->type() == QEvent::Enter) {
		otherEnter();
	} else if (e->type() == QEvent::Leave) {
		otherLeave();
	} else if (e->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton) {
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
		auto containerWidth = newWidth - _st.padding.left() - _st.padding.right() - _st.scrollMargin.left() - _st.scrollMargin.right();
		widget->resizeToWidth(containerWidth);
		newHeight += widget->height();
	}
	if (_maxHeight > 0) {
		accumulate_min(newHeight, _maxHeight);
	}
	return newHeight;
}

InnerDropdown::Container::Container(QWidget *parent, object_ptr<TWidget> child, const style::InnerDropdown &st) : TWidget(parent)
, _child(std::move(child))
, _st(st) {
	_child->setParent(this);
	_child->moveToLeft(_st.scrollPadding.left(), _st.scrollPadding.top());
}

void InnerDropdown::Container::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_child, visibleTop, visibleBottom);
}

void InnerDropdown::Container::resizeToContent() {
	auto newWidth = _st.scrollPadding.left() + _st.scrollPadding.right();
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
	auto innerWidth = newWidth - _st.scrollPadding.left() - _st.scrollPadding.right();
	auto result = _st.scrollPadding.top() + _st.scrollPadding.bottom();
	_child->resizeToWidth(innerWidth);
	_child->moveToLeft(_st.scrollPadding.left(), _st.scrollPadding.top());
	result += _child->height();
	return result;
}

} // namespace Ui
