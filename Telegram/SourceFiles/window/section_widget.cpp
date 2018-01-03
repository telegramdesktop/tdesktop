/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/section_widget.h"

#include <rpl/range.h>
#include "application.h"
#include "window/section_memento.h"
#include "window/window_slide_animation.h"

namespace Window {

SectionWidget::SectionWidget(
	QWidget *parent,
	not_null<Window::Controller*> controller)
: AbstractSectionWidget(parent, controller) {
}

void SectionWidget::setGeometryWithTopMoved(
		const QRect &newGeometry,
		int topDelta) {
	_topDelta = topDelta;
	bool willBeResized = (size() != newGeometry.size());
	if (geometry() != newGeometry) {
		auto weak = make_weak(this);
		setGeometry(newGeometry);
		if (!weak) {
			return;
		}
	}
	if (!willBeResized) {
		resizeEvent(nullptr);
	}
	_topDelta = 0;
}

void SectionWidget::showAnimated(
		SlideDirection direction,
		const SectionSlideParams &params) {
	if (_showAnimation) return;

	showChildren();
	auto myContentCache = grabForShowAnimation(params);
	hideChildren();
	showAnimatedHook(params);

	_showAnimation = std::make_unique<SlideAnimation>();
	_showAnimation->setDirection(direction);
	_showAnimation->setRepaintCallback([this] { update(); });
	_showAnimation->setFinishedCallback([this] { showFinished(); });
	_showAnimation->setPixmaps(
		params.oldContentCache,
		myContentCache);
	_showAnimation->setTopBarShadow(params.withTopBarShadow);
	_showAnimation->setWithFade(params.withFade);
	_showAnimation->start();

	show();
}

std::unique_ptr<SectionMemento> SectionWidget::createMemento() {
	return nullptr;
}

void SectionWidget::showFast() {
	show();
	showFinished();
}

void SectionWidget::paintEvent(QPaintEvent *e) {
	if (_showAnimation) {
		Painter p(this);
		_showAnimation->paintContents(p, e->rect());
	}
}

void SectionWidget::showFinished() {
	_showAnimation.reset();
	if (isHidden()) return;

	showChildren();
	showFinishedHook();

	setInnerFocus();
}

rpl::producer<int> SectionWidget::desiredHeight() const {
	return rpl::single(height());
}

SectionWidget::~SectionWidget() = default;

} // namespace Window
