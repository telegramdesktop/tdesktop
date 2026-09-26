/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/sliding_tabs.h"

#include "ui/effects/slide_animation.h"
#include "ui/ui_utility.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_basic.h"

namespace Ui {

SlidingTabs::SlidingTabs(QWidget *parent, int count, style::color bg)
: RpWidget(parent)
, _bg(bg) {
	Expects(count > 0);

	_tabs.reserve(count);
	for (auto i = 0; i != count; ++i) {
		const auto tab = CreateChild<VerticalLayout>(this);
		tab->setVisible(i == 0);
		tab->heightValue(
		) | rpl::on_next([=](int height) {
			if ((_shown == i) && !_animation) {
				resize(width(), height);
			}
		}, tab->lifetime());
		_tabs.push_back(tab);
	}
}

SlidingTabs::~SlidingTabs() = default;

not_null<VerticalLayout*> SlidingTabs::tab(int index) const {
	Expects(index >= 0 && index < int(_tabs.size()));

	return _tabs[index];
}

void SlidingTabs::showTab(int index, anim::type animated) {
	Expects(index >= 0 && index < int(_tabs.size()));

	if (_shown == index) {
		return;
	}
	finishAnimating();

	const auto was = _tabs[_shown];
	const auto now = _tabs[index];
	const auto slideLeft = (index < _shown);
	const auto rect = (animated == anim::type::instant)
		? QRect()
		: visibleRegion().boundingRect();
	const auto slide = !rect.isEmpty();
	auto wasCache = slide ? GrabOpaque(was, rect, _bg->c) : QPixmap();

	_shown = index;
	was->hide();
	now->show();
	now->resizeToWidth(width());
	applyChildrenVisibleRange();
	if (!slide) {
		resize(width(), now->height());
		return;
	}

	_slideRect = rect;
	_animationHeight = std::max(was->height(), now->height());
	_animation = std::make_unique<SlideAnimation>();
	_animation->setSnapshots(
		std::move(wasCache),
		GrabOpaque(now, rect, _bg->c));
	_animation->start(slideLeft, [=] {
		if (_animation && !_animation->animating()) {
			finishAnimating();
		} else {
			update(_slideRect);
		}
	}, st::slideDuration);
	now->hide();
	resize(width(), _animationHeight);
	update(_slideRect);
}

void SlidingTabs::finishAnimating() {
	if (!_animation) {
		return;
	}
	_animation = nullptr;
	_animationHeight = 0;
	const auto shown = _tabs[_shown];
	shown->show();
	resize(width(), shown->height());
	update();
}

int SlidingTabs::resizeGetHeight(int newWidth) {
	for (const auto &tab : _tabs) {
		tab->resizeToWidth(newWidth);
	}
	return std::max(_tabs[_shown]->height(), _animationHeight);
}

void SlidingTabs::paintEvent(QPaintEvent *e) {
	if (!_animation) {
		return;
	}
	auto p = QPainter(this);
	p.fillRect(_slideRect, _bg);
	_animation->paintFrame(p, _slideRect.x(), _slideRect.y(), width());
}

void SlidingTabs::visibleTopBottomUpdated(int visibleTop, int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
	applyChildrenVisibleRange();
}

void SlidingTabs::applyChildrenVisibleRange() {
	// All tabs are placed at (0, 0), so a hidden tab would consider
	// itself visible and start preloading its content (photos, next
	// pages) in the background. Give the real range only to the shown
	// tab and an empty one to the rest.
	for (auto i = 0, count = int(_tabs.size()); i != count; ++i) {
		const auto tab = _tabs[i].get();
		if (i == _shown) {
			setChildVisibleTopBottom(tab, _visibleTop, _visibleBottom);
		} else {
			setChildVisibleTopBottom(tab, 0, 0);
		}
	}
}

} // namespace Ui
