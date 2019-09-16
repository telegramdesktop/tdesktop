/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/inner_dropdown.h"

#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/effects/panel_animation.h"
#include "ui/image/image_prepare.h"
#include "ui/ui_utility.h"

namespace {

constexpr float64 kFadeHeight = 1. / 3;
constexpr int kFadeAlphaMax = 160;

} // namespace

namespace Ui {

InnerDropdown::InnerDropdown(
	QWidget *parent,
	const style::InnerDropdown &st)
: RpWidget(parent)
, _st(st)
, _roundRect(ImageRoundRadius::Small, _st.bg)
, _scroll(this, _st.scroll) {
	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(onHideAnimated()));

	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));

	hide();

	shownValue(
	) | rpl::filter([](bool shown) {
		return shown;
	}) | rpl::take(1) | rpl::map([=] {
		// We can't invoke this before the window is created.
		// So instead we start handling them on the first show().
		return macWindowDeactivateEvents();
	}) | rpl::flatten_latest(
	) | rpl::filter([=] {
		return !isHidden();
	}) | rpl::start_with_next([=] {
		leaveEvent(nullptr);
	}, lifetime());
}

QPointer<RpWidget> InnerDropdown::doSetOwnedWidget(
		object_ptr<RpWidget> widget) {
	auto result = QPointer<RpWidget>(widget);
	widget->heightValue(
	) | rpl::skip(1) | rpl::start_with_next([=] {
		resizeToContent();
	}, widget->lifetime());
	auto container = _scroll->setOwnedWidget(
		object_ptr<Container>(
			_scroll,
			std::move(widget),
			_st));
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
	QPainter p(this);

	if (_a_show.animating()) {
		if (auto opacity = _a_opacity.value(_hiding ? 0. : 1.)) {
			// _a_opacity.current(ms)->opacityAnimationCallback()->_showAnimation.reset()
			if (_showAnimation) {
				_showAnimation->paintFrame(p, 0, 0, width(), _a_show.value(1.), opacity);
			}
		}
	} else if (_a_opacity.animating()) {
		p.setOpacity(_a_opacity.value(0.));
		p.drawPixmap(0, 0, _cache);
	} else if (_hiding || isHidden()) {
		hideFinished();
	} else if (_showAnimation) {
		_showAnimation->paintFrame(p, 0, 0, width(), 1., 1.);
		_showAnimation.reset();
		showChildren();
	} else {
		if (!_cache.isNull()) _cache = QPixmap();
		const auto inner = rect().marginsRemoved(_st.padding);
		Shadow::paint(p, inner, width(), _st.shadow);
		_roundRect.paint(p, inner);
	}
}

void InnerDropdown::enterEventHook(QEvent *e) {
	if (_autoHiding) {
		showAnimated(_origin);
	}
	return RpWidget::enterEventHook(e);
}

void InnerDropdown::leaveEventHook(QEvent *e) {
	if (_autoHiding) {
		if (_a_show.animating() || _a_opacity.animating()) {
			hideAnimated();
		} else {
			_hideTimer.start(300);
		}
	}
	return RpWidget::leaveEventHook(e);
}

void InnerDropdown::otherEnter() {
	if (_autoHiding) {
		showAnimated(_origin);
	}
}

void InnerDropdown::otherLeave() {
	if (_autoHiding) {
		if (_a_show.animating() || _a_opacity.animating()) {
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
		_a_show.stop();
		showAnimationCallback();
	}
	if (_showAnimation) {
		_showAnimation.reset();
		showChildren();
	}
	if (_a_opacity.animating()) {
		_a_opacity.stop();
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
	_a_show.stop();
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
	_cache = GrabWidget(this);
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

		const auto pixelRatio = style::DevicePixelRatio();
		_showAnimation = std::make_unique<PanelAnimation>(_st.animation, _origin);
		auto inner = rect().marginsRemoved(_st.padding);
		_showAnimation->setFinalImage(std::move(cache), QRect(inner.topLeft() * pixelRatio, inner.size() * pixelRatio));
		_showAnimation->setCornerMasks(
			Images::CornersMask(ImageRoundRadius::Small));
		_showAnimation->start();
	}
	hideChildren();
	_a_show.start([this] { showAnimationCallback(); }, 0., 1., _st.showDuration);
}

QImage InnerDropdown::grabForPanelAnimation() {
	SendPendingMoveResizeEvents(this);
	const auto pixelRatio = style::DevicePixelRatio();
	auto result = QImage(size() * pixelRatio, QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(pixelRatio);
	result.fill(Qt::transparent);
	{
		QPainter p(&result);
		_roundRect.paint(p, rect().marginsRemoved(_st.padding));
		for (const auto child : children()) {
			if (const auto widget = qobject_cast<QWidget*>(child)) {
				RenderWidget(p, widget, widget->pos());
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
